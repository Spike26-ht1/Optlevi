#include "ShaderRenderBridge.h"
#include "ShaderWorldRuntime.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pl/memory/Hook.hpp>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace modmenu::android::shaderbridge {
namespace {
using namespace shaderworld;
std::mutex reportMu;
std::string lastReport="Native draw bridge not initialized";
std::atomic<unsigned> hookCount{0};
struct Frame {
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    bool inPresent=false;
    std::array<GLuint,128> seen{};
    unsigned seenCount=0,markerCount=0,captureCount=0,terrainCount=0;
    std::string details;
};
thread_local Frame frame;
thread_local unsigned drawNesting=0;

void note(std::string const& message){
    if(frame.details.size()+message.size()<3500){
        frame.details+=message;
        frame.details+='\n';
    }
}
int indexOf(std::string_view name,std::string_view prefix,unsigned max){
    if(!name.starts_with(prefix))return -1;
    auto suffix=name.substr(prefix.size());
    unsigned index=0;
    auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),index);
    if(parsed.ec!=std::errc{}||parsed.ptr!=suffix.data()+suffix.size()||index>=max)return -1;
    return int(index);
}
char const* formatName(GLint format){
    switch(format){
    case GL_RGBA8:return "RGBA8";
    case GL_RGB8:return "RGB8";
    case GL_R8:return "R8";
    case GL_RG8:return "RG8";
    case GL_RGB10_A2:return "RGB10_A2";
    case GL_R11F_G11F_B10F:return "R11F_G11F_B10F";
    case 0x805B:return "RGBA16";
    case GL_RGB16F:return "RGB16F";
    case GL_RGBA16F:return "RGBA16F";
    case GL_R16F:return "R16F";
    case GL_RG16F:return "RG16F";
    case GL_RGBA32F:return "RGBA32F";
    default:return nullptr;
    }
}
struct Resource {
    enum class Kind { SceneDepth,SceneColor,ShadowDepth,ShadowColor };
    Kind kind;
    unsigned index;
    GLint location;
    std::string name;
};
void captureBeforeDraw(){
    if(frame.inPresent||frame.context==EGL_NO_CONTEXT||!captureRequested())return;
    if(eglGetCurrentContext()!=frame.context||eglGetCurrentSurface(EGL_DRAW)!=frame.surface)return;
    GLint program=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(program<=0||frame.seenCount>=frame.seen.size())return;
    auto end=frame.seen.begin()+frame.seenCount;
    if(std::find(frame.seen.begin(),end,GLuint(program))!=end)return;
    frame.seen[frame.seenCount++]=GLuint(program);

    GLint count=0;
    glGetProgramiv(GLuint(program),GL_ACTIVE_UNIFORMS,&count);
    if(count<0||count>512)return;
    std::vector<Resource> resources;
    std::vector<UniformMapping> uniforms;
    bool viewProjection=false,viewPosition=false,terrainAtlas=false;
    for(GLint i=0;i<count;++i){
        GLchar raw[256]{};
        GLsizei length=0;GLint size=0;GLenum type=0;
        glGetActiveUniform(GLuint(program),GLuint(i),sizeof(raw),&length,&size,&type,raw);
        if(length<=0||size!=1)continue;
        std::string name(raw,size_t(length));
        viewProjection|=name=="u_viewProj";
        viewPosition|=name=="ViewPositionAndTime";
        terrainAtlas|=name=="s_MatTexture";
        constexpr std::string_view prefix="OptLeviWorld_";
        if(name.starts_with(prefix)&&name.size()>prefix.size()){
            uniforms.push_back({name,name.substr(prefix.size()),type});continue;
        }
        auto add=[&](std::string_view key,unsigned max,Resource::Kind kind,GLenum wanted){
            int index=indexOf(name,key,max);
            if(index<0)return false;
            GLint location=glGetUniformLocation(GLuint(program),name.c_str());
            if(location>=0&&type==wanted)
                resources.push_back({kind,unsigned(index),location,name});
            else note("Wrong marker type: "+name);
            return true;
        };
        if(add("OptLeviDepth",3,Resource::Kind::SceneDepth,GL_INT_VEC3))continue;
        if(add("OptLeviColor",16,Resource::Kind::SceneColor,GL_INT_VEC4))continue;
        if(add("OptLeviShadowDepth",2,Resource::Kind::ShadowDepth,GL_INT_VEC3))continue;
        add("OptLeviShadowColor",2,Resource::Kind::ShadowColor,GL_INT_VEC4);
    }
    // The supplied 26.52 RenderChunk example has these identifiers. Their
    // presence is diagnostic only: a combined view-projection matrix is not
    // the separate Java matrices, and a terrain draw isn't a phase barrier.
    if(viewProjection&&viewPosition&&terrainAtlas){
        if(frame.terrainCount++==0)note("Native terrain signature observed (u_viewProj, ViewPositionAndTime, s_MatTexture)");
    }
    if(resources.empty()&&uniforms.empty())return;
    ++frame.markerCount;
    GLint framebuffer=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
    std::string error;
    if(!uniforms.empty()){
        if(captureProgramUniforms(GLuint(program),uniforms,error))frame.captureCount+=unsigned(uniforms.size());
        else note("World uniform capture: "+error);
    }
    for(auto const& resource:resources){
        GLint v[4]{};
        glGetUniformiv(GLuint(program),resource.location,v);
        bool ok=false;error.clear();
        // Depth: (width,height,actual GL internal format).
        // Color: (attachment index,width,height,actual GL internal format).
        // All metadata is supplied by the explicit native material contract.
        switch(resource.kind){
        case Resource::Kind::SceneDepth:
            ok=captureSceneDepth(DepthPhase(resource.index),unsigned(framebuffer),unsigned(v[2]),v[0],v[1],error);break;
        case Resource::Kind::ShadowDepth:
            ok=captureShadowDepth(resource.index,unsigned(framebuffer),unsigned(v[2]),v[0],v[1],error);break;
        case Resource::Kind::SceneColor:
        case Resource::Kind::ShadowColor:{
            auto* format=formatName(v[3]);
            if(!format||v[0]<0){error="Invalid marker color format/attachment";break;}
            if(resource.kind==Resource::Kind::SceneColor)
                ok=captureSceneColor(resource.index,unsigned(framebuffer),unsigned(v[0]),format,v[1],v[2],error);
            else ok=captureShadowColor(resource.index,unsigned(framebuffer),unsigned(v[0]),format,v[1],v[2],error);
            break;
        }
        }
        if(ok)++frame.captureCount;
        else note(resource.name+": "+error);
    }
}

template<class Fn,class... Args>
void draw(Fn original,Args... args){
    struct Guard { Guard(){++drawNesting;} ~Guard(){--drawNesting;} } guard;
    if(drawNesting==1)captureBeforeDraw();
    original(args...);
}

// Both the public GLES export and the resolved EGL dispatch address may
// be used by a native renderer. Hook each distinct address once.
// Use the comma-containing signatures through parenthesized argument tuples.
#define OPTLEVI_UNWRAP(...) __VA_ARGS__
#define OPTLEVI_DRAW_ENTRY(NAME,PARAMS,ARGS) \
    using NAME##Fn=void (*) PARAMS; \
    NAME##Fn NAME##Original[2]{}; \
    void NAME##Direct PARAMS { draw(NAME##Original[0],OPTLEVI_UNWRAP ARGS); } \
    void NAME##Resolved PARAMS { draw(NAME##Original[1],OPTLEVI_UNWRAP ARGS); }
OPTLEVI_DRAW_ENTRY(DrawElements,(GLenum m,GLsizei n,GLenum t,void const* p),(m,n,t,p))
OPTLEVI_DRAW_ENTRY(DrawArrays,(GLenum m,GLint f,GLsizei n),(m,f,n))
OPTLEVI_DRAW_ENTRY(DrawElementsInstanced,(GLenum m,GLsizei n,GLenum t,void const* p,GLsizei c),(m,n,t,p,c))
OPTLEVI_DRAW_ENTRY(DrawArraysInstanced,(GLenum m,GLint f,GLsizei n,GLsizei c),(m,f,n,c))
#undef OPTLEVI_DRAW_ENTRY
#undef OPTLEVI_UNWRAP

template<class Fn>
unsigned hookEntry(void* library,char const* name,Fn direct,Fn resolved,Fn (&original)[2]){
    void* targets[2]={dlsym(library,name),reinterpret_cast<void*>(eglGetProcAddress(name))};
    Fn detours[2]={direct,resolved};
    unsigned count=0;
    for(unsigned i=0;i<2;++i){
        if(!targets[i]||(i==1&&targets[i]==targets[0]))continue;
        void* trampoline=nullptr;
        if(pl::memory::hook(targets[i],reinterpret_cast<void*>(detours[i]),&trampoline)==0&&trampoline){
            original[i]=reinterpret_cast<Fn>(trampoline);++count;
        }
    }
    return count;
}
}

bool install(){
    static bool attempted=false;
    if(attempted)return hookCount.load()!=0;
    attempted=true;
    void* library=dlopen("libGLESv3.so",RTLD_NOW);
    if(!library){std::lock_guard lock(reportMu);lastReport="Native draw bridge: GLES library unavailable";return false;}
    unsigned count=0;
    count+=hookEntry(library,"glDrawElements",DrawElementsDirect,DrawElementsResolved,DrawElementsOriginal);
    count+=hookEntry(library,"glDrawArrays",DrawArraysDirect,DrawArraysResolved,DrawArraysOriginal);
    count+=hookEntry(library,"glDrawElementsInstanced",DrawElementsInstancedDirect,DrawElementsInstancedResolved,DrawElementsInstancedOriginal);
    count+=hookEntry(library,"glDrawArraysInstanced",DrawArraysInstancedDirect,DrawArraysInstancedResolved,DrawArraysInstancedOriginal);
    hookCount.store(count);
    std::lock_guard lock(reportMu);
    lastReport="Native GLES draw hook entrypoints: "+std::to_string(count);
    return count!=0;
}

PresentGuard::PresentGuard(){
    frame.inPresent=true;
    auto ctx=eglGetCurrentContext();auto surface=eglGetCurrentSurface(EGL_DRAW);
    if(captureRequested()&&frame.context==ctx&&frame.surface==surface){
        std::string current="Native GLES draw hook entrypoints: "+std::to_string(hookCount.load())+
            "\nPrograms observed: "+std::to_string(frame.seenCount)+
            "\nExplicit phase marker programs: "+std::to_string(frame.markerCount)+
            "\nWorld resources captured: "+std::to_string(frame.captureCount);
        if(!frame.markerCount)current+="\nNo verified OptLevi phase-marker material was observed.";
        if(!frame.details.empty())current+="\n"+frame.details;
        std::lock_guard lock(reportMu);lastReport=std::move(current);
    }
    frame.context=ctx;frame.surface=surface;
    frame.seenCount=frame.markerCount=frame.captureCount=frame.terrainCount=0;
    frame.details.clear();
}
PresentGuard::~PresentGuard(){frame.inPresent=false;}
std::string report(){std::lock_guard lock(reportMu);return lastReport;}
}
