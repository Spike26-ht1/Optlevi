#include "ShaderRenderBridge.h"
#include "ShaderWorldRuntime.h"
#include "ShaderCameraMath.h"
#include "ShaderDiagnostics.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <pl/memory/Hook.hpp>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
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
struct DeferredSampler { GLuint program; GLint location; unsigned index; };
struct Frame {
    EGLContext context=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    bool inPresent=false;
    bool terrainCandidate=false;
    GLuint terrainFramebuffer=0;
    int terrainWidth=0,terrainHeight=0;
    std::vector<GLuint> terrainPrograms;
    std::string terrainEffectReport,programSignatures;
    std::array<GLuint,128> seen{};
    unsigned seenCount=0,markerCount=0,captureCount=0,terrainCount=0;
    std::string details;
    // Saved across frames until EGL context/surface replacement.
    std::string firstTerrainLayout;
    std::vector<DeferredSampler> deferredSamplers;
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
    GLint viewProjection=-1,viewPosition=-1;
    bool terrainAtlas=false;
    std::string signature;
    GLint deferredMarker=-1;
    for(GLint i=0;i<count;++i){
        GLchar raw[256]{};
        GLsizei length=0;GLint size=0;GLenum type=0;
        glGetActiveUniform(GLuint(program),GLuint(i),sizeof(raw),&length,&size,&type,raw);
        if(length<=0||size!=1)continue;
        std::string name(raw,size_t(length));
        if(signature.size()<280){if(!signature.empty())signature+=", ";signature+=name;}
        if(name=="u_viewProj"&&type==GL_FLOAT_MAT4)viewProjection=glGetUniformLocation(GLuint(program),raw);
        if(name=="ViewPositionAndTime"&&type==GL_FLOAT_VEC4)viewPosition=glGetUniformLocation(GLuint(program),raw);
        terrainAtlas|=name=="s_MatTexture"&&type==GL_SAMPLER_2D;
        if(name=="OptLeviDeferred"&&type==GL_INT_VEC2){deferredMarker=glGetUniformLocation(GLuint(program),raw);continue;}
        int deferredIndex=indexOf(name,"s_OptLeviDeferred",16);
        if(deferredIndex>=0&&type==GL_SAMPLER_2D){
            auto loc=glGetUniformLocation(GLuint(program),raw);
            if(loc>=0&&frame.deferredSamplers.size()<256)
                frame.deferredSamplers.push_back({GLuint(program),loc,unsigned(deferredIndex)});
            continue;
        }
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
    if(frame.programSignatures.size()<1500){
        frame.programSignatures+="\nProgram "+std::to_string(program)+": "+signature;
    }
    // Record the real VAO layout of the first recognized terrain draw.
    // This is needed to build the future native mesh adapter without
    // hardcoding offsets for a different Minecraft build. It makes no draw.
    if(terrainAtlas&&viewProjection>=0&&frame.terrainPrograms.size()<128)
        frame.terrainPrograms.push_back(GLuint(program));
    if(terrainAtlas&&viewProjection>=0&&frame.firstTerrainLayout.empty()){
        GLint count=0,vao=0,elementBuffer=0;
        glGetProgramiv(GLuint(program),GL_ACTIVE_ATTRIBUTES,&count);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&elementBuffer);
        std::string info="Unverified terrain candidate VAO="+std::to_string(vao)+
            " elementBuffer="+std::to_string(elementBuffer);
        for(GLint i=0;i<count&&i<12&&info.size()<1800;++i){
            GLchar raw[128]{};GLsizei size=0;GLint elements=0;GLenum type=0;
            glGetActiveAttrib(GLuint(program),GLuint(i),sizeof(raw),&size,&elements,&type,raw);
            if(size<=0)continue;
            GLint location=glGetAttribLocation(GLuint(program),raw);
            if(location<0)continue;
            GLint enabled=0,components=0,componentType=0,normalized=0,stride=0,
                  buffer=0,divisor=0,integer=0;
            void* pointer=nullptr;
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_SIZE,&components);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_TYPE,&componentType);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&divisor);
            glGetVertexAttribiv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_INTEGER,&integer);
            glGetVertexAttribPointerv(GLuint(location),GL_VERTEX_ATTRIB_ARRAY_POINTER,&pointer);
            info+="\n  "+std::string(raw,size_t(size))+" at "+std::to_string(location)+
                  " enabled="+std::to_string(enabled)+
                  " comps="+std::to_string(components)+
                  " type="+std::to_string(componentType)+
                  " normalized="+std::to_string(normalized)+
                  " stride="+std::to_string(stride)+
                  " VBO="+std::to_string(buffer)+
                  " divisor="+std::to_string(divisor)+
                  " integer="+std::to_string(integer)+
                  // In a VBO this is an offset, never dereference a client pointer.
                  (buffer?" offset="+std::to_string(reinterpret_cast<std::uintptr_t>(pointer)):" client-array/unbound");
        }
        if(glGetError()==GL_NO_ERROR)frame.firstTerrainLayout=std::move(info);
    }
    // Recover only the projection component of the observed terrain draw.
    // A terrain draw is not a phase barrier, and native world coordinates
    // must not be passed off as Java cameraPosition or model-view values.
    if(viewProjection>=0&&viewPosition>=0&&terrainAtlas){
        if(frame.terrainCount++==0){
            camera::Matrix vp{};
            std::array<float,4> eye{};
            glGetUniformfv(GLuint(program),viewProjection,vp.data());
            glGetUniformfv(GLuint(program),viewPosition,eye.data());
            camera::Projection p;
            if(camera::recoverProjection(vp,eye,p)){
                std::vector<WorldUniform> values(2);
                values[0].name="gbufferProjection";values[0].glType=GL_FLOAT_MAT4;values[0].floats=p.matrix;
                values[1].name="gbufferProjectionInverse";values[1].glType=GL_FLOAT_MAT4;values[1].floats=p.inverse;
                std::string error;
                if(publishWorldUniforms(values,error)){
                    frame.captureCount+=2;
                    note("Native terrain perspective recovered (projection and inverse)");
                }else note("Native camera capture: "+error);
            }else note("Native terrain signature observed; projection/eye validation failed");
        }
    }
    if(resources.empty()&&uniforms.empty()&&deferredMarker<0)return;
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
    if(deferredMarker>=0){
        GLint dimensions[2]{};glGetUniformiv(GLuint(program),deferredMarker,dimensions);
        if(renderDeferred(dimensions[0],dimensions[1],error))note("Opaque deferred stage executed");
        else note("Opaque deferred stage: "+error);
    }
}

// Explicit native material samplers may borrow the deferred outputs during
// every translucent draw. Restore the game's texture and sampler bindings
// immediately afterward; a missing output never substitutes a fake image.
struct DeferredInputs {
    struct Saved { unsigned unit; GLint texture,sampler; };
    std::vector<Saved> saved;
    GLint active=GL_TEXTURE0;
    DeferredInputs(){
        if(drawNesting!=1||frame.inPresent||frame.deferredSamplers.empty()||
            !captureRequested()||frame.context!=eglGetCurrentContext()||
            frame.surface!=eglGetCurrentSurface(EGL_DRAW))return;
        GLint program=0,maxUnits=0;
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&maxUnits);
        struct Input { unsigned unit; GLuint texture; };
        std::vector<Input> inputs;
        for(auto const& binding:frame.deferredSamplers)if(binding.program==GLuint(program)){
            GLuint texture=deferredTexture(binding.index);
            GLint unit=-1;glGetUniformiv(GLuint(program),binding.location,&unit);
            if(!texture||unit<0||unit>=maxUnits){
                note("Deferred material input unavailable: "+std::to_string(binding.index));return;
            }
            if(std::any_of(inputs.begin(),inputs.end(),[&](auto const& i){return i.unit==unsigned(unit);})){
                note("Conflicting deferred sampler units");return;
            }
            inputs.push_back({unsigned(unit),texture});
        }
        if(inputs.empty())return;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        for(auto const& input:inputs){
            glActiveTexture(GL_TEXTURE0+input.unit);
            Saved old{input.unit,0,0};
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&old.texture);
            glGetIntegerv(GL_SAMPLER_BINDING,&old.sampler);
            saved.push_back(old);
            glBindSampler(input.unit,0);glBindTexture(GL_TEXTURE_2D,input.texture);
        }
        glActiveTexture(GLenum(active));
    }
    ~DeferredInputs(){
        for(auto const& old:saved){
            glActiveTexture(GL_TEXTURE0+old.unit);
            glBindTexture(GL_TEXTURE_2D,GLuint(old.texture));glBindSampler(old.unit,GLuint(old.sampler));
        }
        if(!saved.empty())glActiveTexture(GLenum(active));
    }
};
void noteTerrainDraw(){
    if(!captureRequested()||frame.inPresent||
       frame.context!=eglGetCurrentContext()||frame.surface!=eglGetCurrentSurface(EGL_DRAW))return;
    GLint p=0;glGetIntegerv(GL_CURRENT_PROGRAM,&p);
    if(std::find(frame.terrainPrograms.begin(),frame.terrainPrograms.end(),GLuint(p))==frame.terrainPrograms.end())return;
    GLboolean write=GL_FALSE;glGetBooleanv(GL_DEPTH_WRITEMASK,&write);
    if(!write||!glIsEnabled(GL_DEPTH_TEST)||glIsEnabled(GL_BLEND))return;
    GLint viewport[4]{},fbo=0;glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    if(viewport[0]||viewport[1]||viewport[2]<=0||viewport[3]<=0)return;
    frame.terrainCandidate=true;frame.terrainFramebuffer=GLuint(fbo);
    frame.terrainWidth=viewport[2];frame.terrainHeight=viewport[3];
}
// A uniform-name match plus an FBO switch cannot establish phase ownership
// or prove that HUD/postprocessing has not already drawn. Observe only;
// a verified world adapter must explicitly call renderNativeTerrainPhase
// or renderJavaWorldScene. Never write to a guessed native framebuffer.
void describeTerrainCandidate(){
    if(!frame.terrainCandidate)return;
    GLint samples=0,draw=0;glGetIntegerv(GL_SAMPLES,&samples);
    glGetIntegerv(GL_DRAW_BUFFER0,&draw);
    frame.terrainEffectReport="Candidate FBO="+std::to_string(frame.terrainFramebuffer)+
        " viewport="+std::to_string(frame.terrainWidth)+"x"+std::to_string(frame.terrainHeight)+
        " samples="+std::to_string(samples)+" draw0="+std::to_string(draw)+
        " (observation only; no verified world phase)";
    frame.terrainCandidate=false;
}
template<class Fn,class... Args>
void draw(Fn original,Args... args){
    struct Guard { Guard(){++drawNesting;} ~Guard(){--drawNesting;} } guard;
    if(drawNesting==1)captureBeforeDraw();
    DeferredInputs inputs;
    original(args...);
    if(drawNesting==1)noteTerrainDraw();
}

using BindFramebufferFn=void(*)(GLenum,GLuint);
BindFramebufferFn BindFramebufferOriginal[2]{};
void bindFramebuffer(BindFramebufferFn original,GLenum target,GLuint id){
    if(!drawNesting&&!frame.inPresent&&frame.terrainCandidate&&
       frame.context==eglGetCurrentContext()&&frame.surface==eglGetCurrentSurface(EGL_DRAW)&&
       (target==GL_FRAMEBUFFER||target==GL_DRAW_FRAMEBUFFER)&&id!=frame.terrainFramebuffer){
        GLint current=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&current);
        if(GLuint(current)==frame.terrainFramebuffer)describeTerrainCandidate();
    }
    original(target,id);
}
void BindFramebufferDirect(GLenum target,GLuint id){bindFramebuffer(BindFramebufferOriginal[0],target,id);}
void BindFramebufferResolved(GLenum target,GLuint id){bindFramebuffer(BindFramebufferOriginal[1],target,id);}

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
    count+=hookEntry(library,"glBindFramebuffer",BindFramebufferDirect,BindFramebufferResolved,BindFramebufferOriginal);
    hookCount.store(count);
    std::lock_guard lock(reportMu);
    lastReport="Native GLES draw hook entrypoints: "+std::to_string(count);
    return count!=0;
}

PresentGuard::PresentGuard(){
    frame.inPresent=true;
    auto ctx=eglGetCurrentContext();auto surface=eglGetCurrentSurface(EGL_DRAW);
    // Record a remaining candidate for diagnostics, not as a phase barrier.
    // The default framebuffer can already contain HUD or postprocessing.
    if(frame.context==ctx&&frame.surface==surface&&frame.terrainCandidate){
        GLint current=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&current);
        if(GLuint(current)==frame.terrainFramebuffer)describeTerrainCandidate();
    }
    if(captureRequested()&&frame.context==ctx&&frame.surface==surface){
        std::string current="Native GLES draw hook entrypoints: "+std::to_string(hookCount.load())+
            "\nPrograms observed: "+std::to_string(frame.seenCount)+
            "\nExplicit phase marker programs: "+std::to_string(frame.markerCount)+
            "\nWorld resources captured: "+std::to_string(frame.captureCount)+
            "\nTerrain candidate programs (unverified): "+std::to_string(frame.terrainPrograms.size());
        if(!frame.markerCount)current+="\nNo verified OptLevi phase-marker material was observed.";
        if(!frame.firstTerrainLayout.empty())current+="\n"+frame.firstTerrainLayout;
        if(!frame.terrainEffectReport.empty())current+="\n"+frame.terrainEffectReport;
        if(!frame.programSignatures.empty())
            current+="\nObserved native shader uniforms (unclassified):"+frame.programSignatures;
        if(!frame.details.empty())current+="\n"+frame.details;
        if(!frame.terrainPrograms.empty()||frame.markerCount){
            auto vendor=reinterpret_cast<char const*>(glGetString(GL_VENDOR));
            auto renderer=reinterpret_cast<char const*>(glGetString(GL_RENDERER));
            auto version=reinterpret_cast<char const*>(glGetString(GL_VERSION));
            current+="\nGLES vendor: "+std::string(vendor?vendor:"unknown")+
                "\nGLES renderer: "+std::string(renderer?renderer:"unknown")+
                "\nGLES version: "+std::string(version?version:"unknown");
            shaderdiagnostics::record("World candidate observation",current,true);
        }
        std::lock_guard lock(reportMu);lastReport=std::move(current);
    }
    if(frame.context!=ctx||frame.surface!=surface)frame.firstTerrainLayout.clear();
    frame.context=ctx;frame.surface=surface;
    frame.seenCount=frame.markerCount=frame.captureCount=frame.terrainCount=0;
    frame.details.clear();frame.deferredSamplers.clear();frame.terrainPrograms.clear();
    frame.terrainCandidate=false;frame.terrainEffectReport.clear();
    frame.programSignatures.clear();
}
PresentGuard::~PresentGuard(){frame.inPresent=false;}
std::string report(){std::lock_guard lock(reportMu);return lastReport;}
}
