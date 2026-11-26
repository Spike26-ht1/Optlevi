#include "ShaderWorldRuntime.h"
#include <cstddef>
#include "ShaderCameraMath.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace modmenu::android::shaderworld {
namespace {
std::mutex mu;
std::vector<Pass> pendingPasses;
std::string pendingLabel;
std::string lastStatus="No world shader active";
int pendingAction=0; // 0 none, 1 enable, 2 disable
std::atomic<bool> requestedActive{false};
std::atomic<bool> wantsNativeCapture{false};
bool failed=false;
RenderMetrics renderMetrics;

// GPU state is touched exclusively from the selected game's EGL swap thread.
EGLContext context=EGL_NO_CONTEXT;
constexpr unsigned kBuffers=16;
constexpr unsigned kMaxPasses=24;
struct FormatInfo {
    GLint internal=GL_RGBA8;
    GLenum channels=GL_RGBA,type=GL_UNSIGNED_BYTE;
    unsigned bytes=4;
};
std::array<FormatInfo,kBuffers> colorFormats{};
std::array<std::array<float,2>,kBuffers> bufferScales{};
std::array<int,kBuffers> targetWidths{},targetHeights{};
void resetBufferScales(){
    for(auto& scale:bufferScales)scale={1,1};
    targetWidths={};targetHeights={};
}
int scaledWidth(unsigned n,int width){return std::max(1,int(std::lround(width*bufferScales[n][0])));}
int scaledHeight(unsigned n,int height){return std::max(1,int(std::lround(height*bufferScales[n][1])));}

FormatInfo resolveColorFormat(std::string const& name){
    // Memory accounting conservatively rounds RGB storage to four channels.
    if(name=="RGBA"||name=="RGBA8")return {};
    if(name=="RGB"||name=="RGB8")return {GL_RGB8,GL_RGB,GL_UNSIGNED_BYTE,4};
    if(name=="R8")return {GL_R8,GL_RED,GL_UNSIGNED_BYTE,1};
    if(name=="RG8")return {GL_RG8,GL_RG,GL_UNSIGNED_BYTE,2};
    if(name=="RGB10_A2")return {GL_RGB10_A2,GL_RGBA,GL_UNSIGNED_INT_2_10_10_10_REV,4};
    if(name=="R11F_G11F_B10F")return {GL_R11F_G11F_B10F,GL_RGB,GL_UNSIGNED_INT_10F_11F_11F_REV,4};
    // GL_RGBA16_EXT (EXT_texture_norm16); actual driver allocation and
    // framebuffer validation must succeed, otherwise reject the graph.
    if(name=="RGBA16")return {0x805B,GL_RGBA,GL_UNSIGNED_SHORT,8};
    if(name=="RGB16F")return {GL_RGB16F,GL_RGB,GL_HALF_FLOAT,8};
    if(name=="RGBA16F")return {GL_RGBA16F,GL_RGBA,GL_HALF_FLOAT,8};
    if(name=="R16F")return {GL_R16F,GL_RED,GL_HALF_FLOAT,2};
    if(name=="RG16F")return {GL_RG16F,GL_RG,GL_HALF_FLOAT,4};
    if(name=="RGBA32F")return {GL_RGBA32F,GL_RGBA,GL_FLOAT,16};
    return {0,0,0,0};
}

constexpr std::uint64_t kMemoryLimit=128ull*1024*1024;
constexpr std::uint64_t kPackTextureLimit=16ull*1024*1024;
struct Binding { GLint location; int buffer; int packTexture; unsigned unit; int depth=-1,shadowColor=-1; bool compareDepth=false; };
struct DepthSnapshot {
    GLuint tex=0,fbo=0;
    GLenum format=0;
    int width=0,height=0;
    std::uint64_t bytes=0;
    bool fresh=false;
};
struct SceneColorSnapshot {
    std::uint64_t serial=0;
    GLuint tex=0,fbo=0;
    FormatInfo format{};
    int width=0,height=0;
    std::uint64_t bytes=0;
    bool fresh=false;
};
// Only the game EGL thread owns these snapshots. Context or surface changes
// invalidate them. They are never sampled as color buffers.
struct DepthFrame {
    EGLContext ctx=EGL_NO_CONTEXT;
    EGLSurface surface=EGL_NO_SURFACE;
    std::array<DepthSnapshot,5> images{}; // scene 0..2, shadow 3..4
    std::array<SceneColorSnapshot,kBuffers+2> colors{}; // scene and shadow
    std::vector<WorldUniform> uniforms,derived,previousCamera,customValues,shadowValues;
    custom::Smooth smoothPrevious,smoothNext;
    std::chrono::steady_clock::time_point previousTime{},frameTime{};
};
thread_local DepthFrame depthFrame;
std::uint64_t colorTargetBytes=0;
std::uint64_t depthBytes(){
    std::uint64_t bytes=0;
    for(auto const& d:depthFrame.images)bytes+=d.bytes;
    for(auto const& d:depthFrame.colors)bytes+=d.bytes;
    return bytes;
}
void releaseDepth(DepthSnapshot& d){
    if(d.tex)glDeleteTextures(1,&d.tex);
    if(d.fbo)glDeleteFramebuffers(1,&d.fbo);
    d={};
}
void releaseColor(SceneColorSnapshot& d){
    if(d.tex)glDeleteTextures(1,&d.tex);
    if(d.fbo)glDeleteFramebuffers(1,&d.fbo);
    d={};
}
void syncDepthContext(){
    auto ctx=eglGetCurrentContext();
    auto surface=eglGetCurrentSurface(EGL_DRAW);
    if(depthFrame.ctx==ctx&&depthFrame.surface==surface)return;
    if(ctx!=EGL_NO_CONTEXT&&ctx==depthFrame.ctx){
        for(auto& d:depthFrame.images)releaseDepth(d);
        for(auto& d:depthFrame.colors)releaseColor(d);
    }
    depthFrame={};depthFrame.ctx=ctx;depthFrame.surface=surface;
}
struct CameraPair { char const* current;char const* previous;char const* inverse;unsigned type; };
constexpr CameraPair cameraPairs[]={
    {"gbufferProjection","gbufferPreviousProjection","gbufferProjectionInverse",GL_FLOAT_MAT4},
    {"gbufferModelView","gbufferPreviousModelView","gbufferModelViewInverse",GL_FLOAT_MAT4},
    {"cameraPosition","previousCameraPosition",nullptr,GL_FLOAT_VEC3}
};
WorldUniform const* named(std::vector<WorldUniform> const& values,std::string const& name){
    for(auto const& v:values)if(v.name==name)return &v;return nullptr;
}
void deriveCamera(){
    depthFrame.derived.clear();
    for(auto const& pair:cameraPairs){
        auto current=named(depthFrame.uniforms,pair.current);
        if(!current||current->glType!=pair.type)continue;
        if(pair.inverse&&!named(depthFrame.uniforms,pair.inverse)){
            WorldUniform inverse;inverse.name=pair.inverse;inverse.glType=pair.type;
            if(modmenu::android::shaderbridge::camera::invert(current->floats,inverse.floats))
                depthFrame.derived.push_back(std::move(inverse));
        }
        if(!named(depthFrame.uniforms,pair.previous)){
            // On the first valid frame the current actual value seeds history.
            // Never reuse a camera from an interrupted or failed frame.
            auto previous=named(depthFrame.previousCamera,pair.current);
            WorldUniform value=previous?*previous:*current;
            value.name=pair.previous;depthFrame.derived.push_back(std::move(value));
        }
    }
}
struct ConsumeDepthFrame {
    bool rendered=false;
    ~ConsumeDepthFrame(){
        depthFrame.previousCamera.clear();
        if(rendered)for(auto const& pair:cameraPairs){
            auto v=named(depthFrame.uniforms,pair.current);
            if(v&&v->glType==pair.type)depthFrame.previousCamera.push_back(*v);
        }
        for(auto& d:depthFrame.images)d.fresh=false;
        for(auto& d:depthFrame.colors)d.fresh=false;
        depthFrame.uniforms.clear();depthFrame.derived.clear();depthFrame.customValues.clear();depthFrame.shadowValues.clear();
        if(rendered){depthFrame.smoothPrevious=std::move(depthFrame.smoothNext);depthFrame.previousTime=depthFrame.frameTime;}
        else {depthFrame.smoothPrevious.clear();depthFrame.previousTime={};}
        depthFrame.smoothNext.clear();depthFrame.frameTime={};
    }
};
std::string resourceSignature(){
    // Ignore values and object names: world data changes every frame.
    // Recompile only when the actual set/type/shape of available inputs changes.
    std::string signature;
    auto append=[&](char kind,unsigned index,int width,int height,unsigned format){
        signature+=kind;signature+=std::to_string(index)+":"+
            std::to_string(width)+":"+std::to_string(height)+":"+std::to_string(format)+";";
    };
    for(unsigned i=0;i<depthFrame.images.size();++i){
        auto const& d=depthFrame.images[i];
        if(d.fresh)append('d',i,d.width,d.height,d.format);
    }
    for(unsigned i=0;i<depthFrame.colors.size();++i){
        auto const& d=depthFrame.colors[i];
        if(d.fresh)append('c',i,d.width,d.height,unsigned(d.format.internal));
    }
    std::vector<std::string> uniforms;
    for(auto const& v:depthFrame.uniforms)uniforms.push_back(v.name+":"+std::to_string(v.glType));
    for(auto const& v:depthFrame.derived)uniforms.push_back(v.name+":"+std::to_string(v.glType));
    for(auto const& v:depthFrame.customValues)uniforms.push_back(v.name+":"+std::to_string(v.glType));
    for(auto const& v:depthFrame.shadowValues)uniforms.push_back(v.name+":"+std::to_string(v.glType));
    std::sort(uniforms.begin(),uniforms.end());
    for(auto const& u:uniforms)signature+="u"+u+";";
    return signature;
}

int depthIndex(std::string const& name){
    if(name=="depthtex0")return 0;
    if(name=="depthtex1")return 1;
    if(name=="depthtex2")return 2;
    if(name=="shadowtex0")return 3;
    if(name=="shadowtex1")return 4;
    return -1;
}
int uniformComponents(unsigned type){
    switch(type){
        case GL_FLOAT:case GL_INT:case GL_BOOL:return 1;
        case GL_FLOAT_VEC2:case GL_INT_VEC2:case GL_BOOL_VEC2:return 2;
        case GL_FLOAT_VEC3:case GL_INT_VEC3:case GL_BOOL_VEC3:return 3;
        case GL_FLOAT_VEC4:case GL_INT_VEC4:case GL_BOOL_VEC4:return 4;
        case GL_FLOAT_MAT2:return 4;
        case GL_FLOAT_MAT3:return 9;
        case GL_FLOAT_MAT4:return 16;
        default:return 0;
    }
}
bool integerUniform(unsigned type){
    return type==GL_INT||type==GL_INT_VEC2||type==GL_INT_VEC3||type==GL_INT_VEC4||
        type==GL_BOOL||type==GL_BOOL_VEC2||type==GL_BOOL_VEC3||type==GL_BOOL_VEC4;
}
WorldUniform const* findWorldUniform(std::string const& name){
    if(auto value=named(depthFrame.customValues,name))return value;
    if(auto value=named(depthFrame.uniforms,name))return value;
    if(auto value=named(depthFrame.shadowValues,name))return value;
    return named(depthFrame.derived,name);
}
struct WorldBinding { std::string name; GLenum type; GLint location; };
void uploadWorldUniform(WorldBinding const& b,WorldUniform const& v){
    auto f=v.floats.data();auto i=v.ints.data();
    switch(b.type){
        case GL_FLOAT:glUniform1fv(b.location,1,f);break;
        case GL_FLOAT_VEC2:glUniform2fv(b.location,1,f);break;
        case GL_FLOAT_VEC3:glUniform3fv(b.location,1,f);break;
        case GL_FLOAT_VEC4:glUniform4fv(b.location,1,f);break;
        case GL_INT:case GL_BOOL:glUniform1iv(b.location,1,i);break;
        case GL_INT_VEC2:case GL_BOOL_VEC2:glUniform2iv(b.location,1,i);break;
        case GL_INT_VEC3:case GL_BOOL_VEC3:glUniform3iv(b.location,1,i);break;
        case GL_INT_VEC4:case GL_BOOL_VEC4:glUniform4iv(b.location,1,i);break;
        case GL_FLOAT_MAT2:glUniformMatrix2fv(b.location,1,GL_FALSE,f);break;
        case GL_FLOAT_MAT3:glUniformMatrix3fv(b.location,1,GL_FALSE,f);break;
        case GL_FLOAT_MAT4:glUniformMatrix4fv(b.location,1,GL_FALSE,f);break;
    }
}
struct GpuPackTexture { std::string sampler; GLuint texture=0; };
std::vector<GpuPackTexture> packGpu;
std::uint64_t packGpuBytes=0;
bool packResourcesReady=false;
unsigned frameCounter=0;
struct CompiledPass {
    GLuint program=0;
    std::string name;
    std::vector<unsigned> targets;
    std::vector<Binding> samplers;
    std::vector<WorldBinding> worldUniforms;
    bool finalPass=false;
    bool customVertex=false;
    bool deferred=false;
    std::uint32_t mipmapInputs=0;
};
std::vector<CompiledPass> pipeline;
std::array<std::array<GLuint,2>,kBuffers> textures{};
std::array<unsigned,kBuffers> front{};
std::array<bool,kBuffers> needed{},mipNeeded{},historyNeeded{},sceneNeeded{};
std::array<bool,kBuffers> deferredInputs{},deferredOutputs{};
std::array<std::uint64_t,kBuffers> opaqueSerial{};
std::uint64_t snapshotSerial=0;
bool deferredReady=false,worldSceneRendered=false;
float frameTime=0,frameDelta=0;
bool frameClockReady=false;
int frameViewWidth=0,frameViewHeight=0;
void beginFrameTiming();
std::array<std::array<bool,2>,kBuffers> mipValid{};
GLuint triangleVao=0,passFbo=0,outputTex=0,shadowCompareSampler=0;
int textureWidth=0,textureHeight=0;
std::string activeLabel;
bool renderedFrame=false;
// Render-thread-owned selected source; retain it across temporary absence
// of native world resources, but never render with stale attachments.
std::vector<Pass> selectedSource;
// Built-ins belong to the shader engine, not the native material ABI.
std::optional<WorldUniform> frameBuiltin(std::string const& name){
    WorldUniform u;u.name=name;u.glType=GL_FLOAT;
    if(name=="frameCounter"){u.glType=GL_INT;u.ints[0]=int(frameCounter);}
    else if(name=="framemod8")u.floats[0]=float(frameCounter%8);
    else if(name=="framemod2")u.floats[0]=float(frameCounter%2);
    else if(name=="frameTime"&&frameClockReady)u.floats[0]=frameDelta;
    else if(name=="frameTimeCounter"&&frameClockReady)u.floats[0]=frameTime;
    else if(name=="viewWidth"&&frameViewWidth>0)u.floats[0]=float(frameViewWidth);
    else if(name=="viewHeight"&&frameViewHeight>0)u.floats[0]=float(frameViewHeight);
    else if(name=="aspectRatio"&&frameViewWidth>0&&frameViewHeight>0)
        u.floats[0]=float(frameViewWidth)/float(frameViewHeight);
    else return {};
    return u;
}
bool computeShadowCamera(shaderbridge::camera::ShadowCamera& camera,int& resolution){
    if(selectedSource.empty()||!selectedSource.front().shadowSettings)return false;
    auto sun=named(depthFrame.uniforms,"sunAngle");
    auto position=named(depthFrame.uniforms,"cameraPosition");
    if(!sun||sun->glType!=GL_FLOAT||!position||position->glType!=GL_FLOAT_VEC3)return false;
    auto const& settings=*selectedSource.front().shadowSettings;
    shaderbridge::camera::Vec eye{position->floats[0],position->floats[1],position->floats[2]};
    if(!shaderbridge::camera::shadowCamera(settings,sun->floats[0],eye,camera))return false;
    resolution=settings.resolution;return true;
}
void refreshShadowUniforms(){
    depthFrame.shadowValues.clear();
    // Never mix explicitly supplied matrices and a computed camera.
    for(auto const* name:{"shadowModelView","shadowProjection","shadowModelViewInverse","shadowProjectionInverse"})
        if(named(depthFrame.uniforms,name))return;
    shaderbridge::camera::ShadowCamera camera;int resolution=0;
    if(!computeShadowCamera(camera,resolution))return;
    auto add=[&](char const* name,shaderbridge::camera::Matrix const& m){
        WorldUniform u;u.name=name;u.glType=GL_FLOAT_MAT4;u.floats=m;
        depthFrame.shadowValues.push_back(std::move(u));
    };
    add("shadowModelView",camera.view);add("shadowProjection",camera.projection);
    add("shadowModelViewInverse",camera.viewInverse);add("shadowProjectionInverse",camera.projectionInverse);
}
void refreshCustomUniforms(){
    refreshShadowUniforms();
    depthFrame.customValues.clear();depthFrame.smoothNext.clear();
    if(selectedSource.empty()||selectedSource.front().customUniforms.empty())return;
    if(depthFrame.frameTime==std::chrono::steady_clock::time_point{})
        depthFrame.frameTime=std::chrono::steady_clock::now();
    double ticks=depthFrame.previousTime==std::chrono::steady_clock::time_point{}?0:
        20*std::chrono::duration<double>(depthFrame.frameTime-depthFrame.previousTime).count();
    auto lookup=[](std::string const& key)->custom::Number{
        if(auto builtin=frameBuiltin(key))
            return builtin->glType==GL_INT?double(builtin->ints[0]):double(builtin->floats[0]);
        auto dot=key.find('.');auto name=key.substr(0,dot);
        auto v=named(depthFrame.uniforms,name);if(!v)v=named(depthFrame.derived,name);
        if(!v)return {};
        unsigned i=0;
        if(dot!=std::string::npos){
            if(dot+2!=key.size())return {};
            auto c=key[dot+1];if(c=='x'||c=='r')i=0;else if(c=='y'||c=='g')i=1;
            else if(c=='z'||c=='b')i=2;else if(c=='w'||c=='a')i=3;else return {};
        }
        if(v->glType==GL_FLOAT_MAT2||v->glType==GL_FLOAT_MAT3||v->glType==GL_FLOAT_MAT4)return {};
        int n=uniformComponents(v->glType);
        if(int(i)>=n||(dot==std::string::npos&&n!=1))return {};
        return integerUniform(v->glType)?double(v->ints[i]):double(v->floats[i]);
    };
    custom::Evaluation evaluation{selectedSource.front().customUniforms,lookup,depthFrame.smoothPrevious,ticks};
    for(auto const& rule:selectedSource.front().customUniforms)if(rule.uniform){
        auto n=evaluation.get(rule.name);
        if(n&&std::isfinite(float(*n))){
            WorldUniform v;v.name=rule.name;v.glType=GL_FLOAT;v.floats[0]=float(*n);
            depthFrame.customValues.push_back(std::move(v));
        }
    }
    depthFrame.smoothNext=std::move(evaluation.next);
}
std::vector<GeometryHandle> geometryHandles;
std::vector<std::string> geometryNames;
std::string selectedLabel,lastResourceSignature;
bool waitingForResources=false,attemptedResources=false;
EGLSurface pipelineSurface=EGL_NO_SURFACE;

void statusMessage(bool ok,std::string message) {
    std::lock_guard lock(mu);
    failed=!ok;
    lastStatus=std::move(message);
}

void abandonContext() {
    // Names from the previous EGL context are invalid in this one.
    context=eglGetCurrentContext();
    geometryHandles.clear();geometryNames.clear();
    pipeline.clear();
    packGpu.clear();packGpuBytes=0;packResourcesReady=false;frameCounter=0;
    textures={};front={};needed={};mipNeeded={};historyNeeded={};sceneNeeded={};deferredInputs={};deferredOutputs={};deferredReady=false;mipValid={};colorFormats={};resetBufferScales();triangleVao=passFbo=outputTex=shadowCompareSampler=0;
    textureWidth=textureHeight=0;colorTargetBytes=0;
    renderedFrame=false;
    requestedActive=false;
}

void discardGeometry(){
    for(auto const& g:geometryHandles)if(g.program)glDeleteProgram(g.program);
    geometryHandles.clear();geometryNames.clear();
}
void discardPackResources(){
    for(auto& p:packGpu)if(p.texture)glDeleteTextures(1,&p.texture);
    packGpu.clear();packGpuBytes=0;packResourcesReady=false;
}
void discardGpu() {
    for(auto& p:pipeline) if(p.program)glDeleteProgram(p.program);
    pipeline.clear();
    frameCounter=0;
    for(auto& pair:textures) glDeleteTextures(2,pair.data());
    textures={};front={};needed={};mipNeeded={};historyNeeded={};sceneNeeded={};deferredInputs={};deferredOutputs={};deferredReady=false;mipValid={};colorFormats={};resetBufferScales();
    if(outputTex){glDeleteTextures(1,&outputTex);outputTex=0;}
    if(passFbo){glDeleteFramebuffers(1,&passFbo);passFbo=0;}
    if(triangleVao){glDeleteVertexArrays(1,&triangleVao);triangleVao=0;}
    if(shadowCompareSampler){glDeleteSamplers(1,&shadowCompareSampler);shadowCompareSampler=0;}
    textureWidth=textureHeight=0;colorTargetBytes=0;
    renderedFrame=false;
    requestedActive=false;
}

std::string shaderLog(GLuint shader,bool isProgram=false) {
    GLint size=0;
    if(isProgram)glGetProgramiv(shader,GL_INFO_LOG_LENGTH,&size);
    else glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&size);
    if(size<=1)return {};
    std::string s(static_cast<size_t>(std::min(size,4096)),'\0');
    GLsizei read=0;
    if(isProgram)glGetProgramInfoLog(shader,GLsizei(s.size()),&read,s.data());
    else glGetShaderInfoLog(shader,GLsizei(s.size()),&read,s.data());
    s.resize(size_t(std::max(0,int(read))));
    return s;
}

GLuint compile(GLenum kind,std::string const& source,std::string& error) {
    GLuint obj=glCreateShader(kind);
    if(!obj){error="glCreateShader failed";return 0;}
    char const* p=source.c_str();
    glShaderSource(obj,1,&p,nullptr);
    glCompileShader(obj);
    GLint ok=0;glGetShaderiv(obj,GL_COMPILE_STATUS,&ok);
    if(!ok) {
        error=shaderLog(obj);
        glDeleteShader(obj);
        return 0;
    }
    return obj;
}


bool evaluateEnabled(Pass const& pass,bool& enabled,std::uint32_t& mipmapInputs,std::uint32_t& historyInputs,std::string& error){
    enabled=true;mipmapInputs=0;historyInputs=0;
    if(pass.enabledProbe.empty())return true;
    static char const* vertex=R"(#version 300 es
void main(){gl_Position=vec4(0.0,0.0,0.0,1.0);}
)";
    GLuint vs=compile(GL_VERTEX_SHADER,vertex,error);
    if(!vs)return false;
    GLuint fs=compile(GL_FRAGMENT_SHADER,pass.enabledProbe,error);
    if(!fs){glDeleteShader(vs);return false;}
    GLuint program=glCreateProgram();
    if(!program){glDeleteShader(vs);glDeleteShader(fs);error="Cannot allocate enabled probe";return false;}
    glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){error=shaderLog(program,true);glDeleteProgram(program);return false;}
    bool yes=glGetFragDataLocation(program,"optEnabledOutput")==0;
    bool no=glGetFragDataLocation(program,"optDisabledOutput")==0;
    if(yes)for(unsigned n=0;n<kBuffers;++n){
        auto name="optMipInput"+std::to_string(n);
        if(glGetUniformLocation(program,name.c_str())>=0)mipmapInputs|=std::uint32_t(1)<<n;
        name="optHistory"+std::to_string(n);
        if(glGetUniformLocation(program,name.c_str())>=0)historyInputs|=std::uint32_t(1)<<n;
    }
    glDeleteProgram(program);
    if(yes==no){error="Ambiguous program.enabled probe";return false;}
    enabled=yes;return true;
}

int bufferIndex(std::string const& name) {
    if(name=="gcolor"||name=="texture"||name=="tex")return 0;
    if(name=="gdepth")return 1;
    if(name=="gnormal")return 2;
    if(name=="composite")return 3;
    if(name.size()==5&&name.compare(0,4,"gaux")==0&&name[4]>='1'&&name[4]<='4')
        return name[4]-'1'+4;
    if(name.size()>8&&name.size()<=10&&name.compare(0,8,"colortex")==0){
        unsigned n=0;
        for(size_t i=8;i<name.size();++i){
            if(name[i]<'0'||name[i]>'9')return -1;
            n=n*10+unsigned(name[i]-'0');
        }
        if(n<kBuffers)return int(n);
    }
    return -1;
}
bool compileProgram(Pass const& pass,std::array<bool,kBuffers> const& available,
                    CompiledPass& result,std::string& error,bool stagedGraph) {
    static char const* vertex=R"(#version 300 es
precision highp float;
out vec2 texcoord;
out vec2 texCoord;
out vec2 vTexCoord;
void main() {
    vec2 uv=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    texcoord=uv;
    texCoord=uv;
    vTexCoord=uv;
    gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
}
)";

    GLuint vs=compile(GL_VERTEX_SHADER,pass.vertex.empty()?std::string(vertex):pass.vertex,error);
    if(!vs)return false;
    GLuint fs=compile(GL_FRAGMENT_SHADER,pass.fragment,error);
    if(!fs){glDeleteShader(vs);return false;}
    GLuint linked=glCreateProgram();
    if(!linked){glDeleteShader(vs);glDeleteShader(fs);error="Cannot allocate GL program";return false;}
    glAttachShader(linked,vs);glAttachShader(linked,fs);glLinkProgram(linked);
    glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(linked,GL_LINK_STATUS,&ok);
    if(!ok){error=shaderLog(linked,true);glDeleteProgram(linked);return false;}
    auto fail=[&](std::string message){error=std::move(message);glDeleteProgram(linked);return false;};
    // An original Java vertex stage may need vertex attributes that the
    // fullscreen renderer cannot supply. Never silently bind zero attributes.
    GLint attributes=0;
    glGetProgramiv(linked,GL_ACTIVE_ATTRIBUTES,&attributes);
    for(GLint i=0;i<attributes;++i){
        GLchar name[256]{};
        GLsizei len=0;GLint size=0;GLenum type=0;
        glGetActiveAttrib(linked,GLuint(i),sizeof(name),&len,&size,&type,name);
        if(glGetAttribLocation(linked,name)>=0)
            return fail("Unsupported fullscreen vertex attribute: "+std::string(name));
    }
    std::string missing;
    auto reportMissing=[&](std::string const& name){
        if(missing.size()<3500){if(!missing.empty())missing+=", ";missing+=name;}
    };
    GLint count=0;
    glGetProgramiv(linked,GL_ACTIVE_UNIFORMS,&count);
    for(GLint i=0;i<count;i++) {
        GLchar name[256]{};
        GLsizei len=0;GLint arraySize=0;GLenum type=0;
        glGetActiveUniform(linked,GLuint(i),sizeof(name),&len,&arraySize,&type,name);
        std::string uniform(name,size_t(std::max(0,int(len))));
        int index=bufferIndex(uniform);
        int packIndex=-1;
        for(size_t k=0;k<packGpu.size();++k)
            if(packGpu[k].sampler==uniform){packIndex=int(k);break;}
        int depth=depthIndex(uniform);
        int shadowColor=uniform=="shadowcolor0"?int(kBuffers):uniform=="shadowcolor1"?int(kBuffers+1):-1;
        if(depth>=0&&arraySize==1&&(type==GL_SAMPLER_2D||(type==GL_SAMPLER_2D_SHADOW&&depth>=3))){
            if(!stagedGraph&&!depthFrame.images[unsigned(depth)].fresh){
                reportMissing(uniform+" (depth phase)");continue;
            }
            if(result.samplers.size()>=kBuffers)
                return fail("Too many active texture samplers in "+pass.name);
            result.samplers.push_back({glGetUniformLocation(linked,name),-1,-1,
                                        unsigned(result.samplers.size()),depth,-1,type==GL_SAMPLER_2D_SHADOW});
        }else if(type==GL_SAMPLER_2D&&shadowColor>=0&&arraySize==1){
            if(!stagedGraph&&!depthFrame.colors[unsigned(shadowColor)].fresh){
                reportMissing(uniform+" (shadow color)");continue;
            }
            if(result.samplers.size()>=kBuffers)
                return fail("Too many active texture samplers in "+pass.name);
            result.samplers.push_back({glGetUniformLocation(linked,name),-1,-1,
                                         unsigned(result.samplers.size()),-1,shadowColor});
        }else if(type==GL_SAMPLER_2D&&(index>=0||packIndex>=0)&&arraySize==1){
            if(!stagedGraph&&packIndex<0&&!available[unsigned(index)]){
                reportMissing(uniform+" (geometry buffer)");continue;
            }
            if(result.samplers.size()>=kBuffers)
                return fail("Too many active texture samplers in "+pass.name);
            result.samplers.push_back({glGetUniformLocation(linked,name),index,packIndex,
                                       unsigned(result.samplers.size())});
        }else if(type==GL_INT&&arraySize==1&&uniform=="frameCounter"){
            // Actual rendered frame counter, not an invented world tick.
        }else if(type==GL_FLOAT&&arraySize==1&&
                 (uniform=="viewWidth"||uniform=="viewHeight"||
                  uniform=="aspectRatio"||uniform=="frameTimeCounter"||
                  uniform=="frameTime"||uniform=="framemod8"||uniform=="framemod2")){
            // Actual viewport, timing and pack-declared frame modulo values.
        }else if(arraySize==1&&uniformComponents(type)){
            auto const* value=findWorldUniform(uniform);
            if(!value&&!stagedGraph){reportMissing(uniform+" (world uniform)");continue;}
            if(value&&value->glType!=type){reportMissing(uniform+" (world uniform type mismatch)");continue;}
            GLint location=glGetUniformLocation(linked,name);
            if(location<0){reportMissing(uniform+" (uniform block unsupported)");continue;}
            result.worldUniforms.push_back({uniform,type,location});
        }else{
            reportMissing(uniform+" (unsupported type)");
        }
    }
    if(!missing.empty()){
        // Preserve the old geometry failure wording used by callers/tests.
        return fail("Missing real input "+missing+"; Unsupported world shader uniform or renderer resource");
    }
    result.targets=pass.targets;
    if(pass.routedOutputs){
        // Names encode the chosen colortex. The driver has already evaluated
        // the original preprocessor conditions; no guessed first directive.
        std::array<int,kBuffers> byLocation{};
        byLocation.fill(-1);
        unsigned count=0;
        for(unsigned t=0;t<kBuffers;++t){
            std::string name="optRouteTarget"+std::to_string(t);
            GLint loc=glGetFragDataLocation(linked,name.c_str());
            if(loc<0)continue;
            if(loc>=int(kBuffers)||byLocation[unsigned(loc)]!=-1)
                return fail("Invalid linked conditional output location");
            byLocation[unsigned(loc)]=int(t);++count;
        }
        if(count==0)return fail("No linked conditional render targets");
        result.targets.clear();
        for(unsigned i=0;i<count;++i){
            if(byLocation[i]<0)return fail("Noncontiguous linked render targets");
            result.targets.push_back(unsigned(byLocation[i]));
        }
    }
    result.program=linked;
    result.name=pass.name;
    result.finalPass=pass.finalPass;
    result.customVertex=!pass.vertex.empty();
    result.deferred=pass.deferred;
    return true;
}

bool uploadPackTextures(std::vector<PackTexture> const& inputs,std::string& error){
    if(inputs.size()>8){error="Too many pack textures";return false;}
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    for(auto const& image:inputs){
        if(image.width<=0||image.height<=0||image.width>2048||image.height>2048||
           image.rgba.size()!=std::uint64_t(image.width)*image.height*4||
           packGpuBytes+image.rgba.size()>kPackTextureLimit){
            error="Invalid or oversized pack texture "+image.sampler;return false;
        }
        for(auto const& old:packGpu)if(old.sampler==image.sampler){
            error="Duplicate pack texture "+image.sampler;return false;
        }
        packGpu.push_back({image.sampler,0});
        auto& tex=packGpu.back().texture;
        glGenTextures(1,&tex);
        if(!tex){error="Cannot allocate pack texture";return false;}
        glBindTexture(GL_TEXTURE_2D,tex);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,image.nearest?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,image.nearest?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,image.width,image.height,0,GL_RGBA,
                     GL_UNSIGNED_BYTE,image.rgba.data());
        if(glGetError()!=GL_NO_ERROR){error="Cannot upload pack texture "+image.sampler;return false;}
        packGpuBytes+=image.rgba.size();
    }
    return true;
}

bool compilePipeline(std::vector<Pass> const& passes,std::string& error) {
    if(passes.empty()||passes.size()>kMaxPasses||!passes.back().finalPass){
        error="Invalid composite pipeline: missing final pass or too many passes";return false;
    }
    GLint maxDraw=0,maxColor=0,maxUnits=0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxDraw);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&maxUnits);
    if(maxDraw<1||maxColor<1||maxUnits<int(kBuffers)){
        error="Insufficient GLES3 render targets or fragment texture units";return false;
    }
    if(!packResourcesReady){error="Java pack textures not prepared";return false;}
    for(auto const& entry:passes.front().colorFormats){
        if(entry.first>=kBuffers){error="Invalid colortex format index";return false;}
        colorFormats[entry.first]=resolveColorFormat(entry.second);
    }
    for(auto const& size:passes.front().bufferScales){
        if(size.index>=kBuffers||!std::isfinite(size.width)||!std::isfinite(size.height)||
           size.width<0.125f||size.width>1.0f||size.height<0.125f||size.height>1.0f){
            error="Unsupported Java buffer scaling directive";return false;
        }
        bufferScales[size.index]={size.width,size.height};
    }
    // Collect metadata from all active programs before validating graph
    // dependencies: temporal resources can be read before their first writer.
    struct Metadata { bool enabled=true; std::uint32_t mip=0,history=0; };
    std::vector<Metadata> metadata(passes.size());
    std::array<bool,kBuffers> available{},produced{};
    available[0]=true;needed[0]=true;
    for(unsigned n=0;n<kBuffers;++n)if(depthFrame.colors[n].fresh)available[n]=true;
    sceneNeeded[0]=depthFrame.colors[0].fresh;
    for(size_t i=0;i<passes.size();++i){
        auto& m=metadata[i];
        if(!evaluateEnabled(passes[i],m.enabled,m.mip,m.history,error)){
            error=passes[i].name+" program metadata: "+error;return false;
        }
        if(m.enabled)for(unsigned n=0;n<kBuffers;++n)
            if(m.history&(std::uint32_t(1)<<n)){
                // colortex0 is replaced by a new real scene every frame.
                if(n==0){error="Persistent colortex0 conflicts with captured scene";return false;}
                historyNeeded[n]=true;available[n]=true;needed[n]=true;
            }
    }
    bool stagedGraph=false;
    for(size_t i=0;i<passes.size();++i)stagedGraph|=metadata[i].enabled&&passes[i].deferred;
    bool compositeSeen=false;
    for(size_t i=0;i<passes.size();++i) {
        auto const& pass=passes[i];
        auto const& m=metadata[i];
        std::uint32_t mipmapInputs=m.mip;
        if(!m.enabled){
            if(pass.finalPass){error="Final pass cannot be disabled";return false;}
            continue;
        }
        if(pass.deferred&&(compositeSeen||pass.finalPass)){
            error="Deferred pass outside opaque stage: "+pass.name;return false;
        }
        if(!pass.deferred)compositeSeen=true;
        if(pass.finalPass!=(i+1==passes.size())||pass.targets.empty()||
           pass.targets.size()>size_t(std::min(maxDraw,maxColor))||
           (pass.finalPass&&pass.targets.size()!=1)){
            error="Invalid output targets for "+pass.name;return false;
        }
        std::array<bool,kBuffers> unique{};
        for(auto target:pass.targets){
            if(target>=kBuffers||unique[target]){
                error="Unsupported or repeated colortex target in "+pass.name;return false;
            }
            unique[target]=true;
        }
        CompiledPass p;
        if(!compileProgram(pass,available,p,error,stagedGraph)){
            error=pass.name+": "+error;return false;
        }
        if(p.targets.empty()||p.targets.size()>size_t(std::min(maxDraw,maxColor))||
           (p.finalPass&&p.targets.size()!=1)){
            glDeleteProgram(p.program);
            error="Active output mapping exceeds driver limits in "+pass.name;return false;
        }
        p.mipmapInputs=mipmapInputs;
        for(auto const& binding:p.samplers)if(binding.depth<0&&binding.shadowColor<0&&binding.packTexture<0){
            unsigned n=unsigned(binding.buffer);
            if(mipmapInputs&(std::uint32_t(1)<<n))mipNeeded[n]=true;
            if(!produced[n]&&!historyNeeded[n]&&(stagedGraph||depthFrame.colors[n].fresh)){
                // A stage may declare an external input before its actual
                // geometry/depth producer runs. No texture is considered
                // initialized until the corresponding stage validates it.
                sceneNeeded[n]=true;needed[n]=true;
                if(p.deferred)deferredInputs[n]=true;
            }
            if(stagedGraph&&n==0&&!produced[n]){
                // A final-frame backbuffer is never a staged G-buffer source.
                sceneNeeded[0]=true;
                if(p.deferred)deferredInputs[0]=true;
            }
        }
        if(!p.finalPass)for(auto t:p.targets){available[t]=true;needed[t]=true;produced[t]=true;if(p.deferred)deferredOutputs[t]=true;}
        pipeline.push_back(std::move(p));
    }
    for(unsigned i=0;i<kBuffers;++i)if(sceneNeeded[i]&&
       (bufferScales[i][0]!=1.0f||bufferScales[i][1]!=1.0f)){
        error="Scaled external geometry colortex"+std::to_string(i)+" is not yet supported";return false;
    }
    for(unsigned i=0;i<kBuffers;++i)if(historyNeeded[i]&&depthFrame.colors[i].fresh){
        error="Native scene input and persistent history conflict at colortex"+std::to_string(i);return false;
    }
    for(unsigned i=0;i<kBuffers;++i)if(historyNeeded[i]&&!produced[i]){
        error="Persistent colortex"+std::to_string(i)+" has no active producer";return false;
    }
    for(unsigned i=0;i<kBuffers;++i)if(needed[i]&&!colorFormats[i].bytes){
        error="Unsupported color attachment format for colortex"+std::to_string(i);return false;
    }
    bool compare=false;
    for(auto const& p:pipeline)for(auto const& b:p.samplers)compare|=b.compareDepth;
    if(compare){
        glGenSamplers(1,&shadowCompareSampler);
        if(!shadowCompareSampler){error="Cannot allocate shadow comparison sampler";return false;}
        glSamplerParameteri(shadowCompareSampler,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(shadowCompareSampler,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
        glSamplerParameteri(shadowCompareSampler,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glSamplerParameteri(shadowCompareSampler,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glSamplerParameteri(shadowCompareSampler,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glSamplerParameteri(shadowCompareSampler,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(glGetError()!=GL_NO_ERROR){error="Cannot configure shadow comparison sampler";return false;}
    }
    glGenVertexArrays(1,&triangleVao);
    glGenFramebuffers(1,&passFbo);
    return triangleVao&&passFbo;
}

struct SavedState {
    GLint program=0,vao=0,readFbo=0,drawFbo=0;
    GLint activeTex=GL_TEXTURE0,readBuffer=GL_BACK;
    std::array<GLint,kBuffers> tex{},sampler{};
    GLint viewport[4]{};
    GLint unpackPbo=0,unpackAlign=4,unpackRow=0,unpackSkipRows=0,unpackSkipPixels=0;
    GLboolean blend=false,depth=false,scissor=false,stencil=false,cull=false,discard=false;
    GLboolean depthMask=GL_TRUE,colorMask[4]{};
    SavedState() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);
        glGetIntegerv(GL_READ_BUFFER,&readBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTex);
        for(unsigned i=0;i<kBuffers;++i){
            glActiveTexture(GL_TEXTURE0+i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex[i]);
            glGetIntegerv(GL_SAMPLER_BINDING,&sampler[i]);
        }
        glActiveTexture(GLenum(activeTex));
        glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackPbo);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpackAlign);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpackRow);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&unpackSkipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&unpackSkipPixels);
        blend=glIsEnabled(GL_BLEND);
        depth=glIsEnabled(GL_DEPTH_TEST);
        scissor=glIsEnabled(GL_SCISSOR_TEST);
        stencil=glIsEnabled(GL_STENCIL_TEST);
        cull=glIsEnabled(GL_CULL_FACE);
        discard=glIsEnabled(GL_RASTERIZER_DISCARD);
        glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
    }
    void restore() const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(readFbo));
        glReadBuffer(GLenum(readBuffer));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(drawFbo));
        glUseProgram(GLuint(program));
        glBindVertexArray(GLuint(vao));
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
        if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        if(stencil)glEnable(GL_STENCIL_TEST);else glDisable(GL_STENCIL_TEST);
        if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
        if(discard)glEnable(GL_RASTERIZER_DISCARD);else glDisable(GL_RASTERIZER_DISCARD);
        glDepthMask(depthMask);
        glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,GLuint(unpackPbo));
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpackAlign);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,unpackRow);
        glPixelStorei(GL_UNPACK_SKIP_ROWS,unpackSkipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS,unpackSkipPixels);
        for(unsigned i=0;i<kBuffers;++i){
            glActiveTexture(GL_TEXTURE0+i);
            glBindTexture(GL_TEXTURE_2D,GLuint(tex[i]));
            glBindSampler(i,GLuint(sampler[i]));
        }
        glActiveTexture(GLenum(activeTex));
    }
};

bool allocateTexture(GLuint& texture,int w,int h,FormatInfo const& format) {
    glGenTextures(1,&texture);
    if(!texture)return false;
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,format.internal,w,h,0,format.channels,format.type,nullptr);
    return glGetError()==GL_NO_ERROR;
}

bool preparePackResources(std::string& error){
    if(packResourcesReady)return true;
    if(selectedSource.empty()){error="No selected Java pack";return false;}
    SavedState saved;
    bool ok=uploadPackTextures(selectedSource.front().packTextures,error);
    saved.restore();
    if(!ok){discardPackResources();return false;}
    packResourcesReady=true;
    return true;
}
bool ensureTargets(int width,int height,std::string& error) {
    if(width==textureWidth&&height==textureHeight&&outputTex)return true;
    for(auto& pair:textures){glDeleteTextures(2,pair.data());pair={};}
    if(outputTex){glDeleteTextures(1,&outputTex);outputTex=0;}
    textureWidth=textureHeight=0;colorTargetBytes=0;mipValid={};front={};
    if(width<=0||height<=0||width>4096||height>4096){error="Unsupported viewport size";return false;}
    std::uint64_t basePixels=std::uint64_t(width)*height;
    std::uint64_t bytes=basePixels*4+packGpuBytes;
    for(unsigned i=0;i<kBuffers;++i)if(needed[i]){
        targetWidths[i]=scaledWidth(i,width);targetHeights[i]=scaledHeight(i,height);
        std::uint64_t pixels=std::uint64_t(targetWidths[i])*targetHeights[i];
        std::uint64_t chain=pixels;
        if(mipNeeded[i])for(int w=targetWidths[i],h=targetHeights[i];w>1||h>1;){
            w=std::max(1,w/2);h=std::max(1,h/2);chain+=std::uint64_t(w)*h;
        }
        bytes+=2*(mipNeeded[i]?chain:pixels)*colorFormats[i].bytes;
    }
    if(bytes+depthBytes()>kMemoryLimit){error="Cannot allocate independent GLES3 targets within 128 MiB budget";return false;}
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=0;i<kBuffers;++i)if(needed[i]){
        for(auto& t:textures[i])
            if(!allocateTexture(t,targetWidths[i],targetHeights[i],colorFormats[i])){
            error="Driver cannot allocate declared color format for colortex"+std::to_string(i);return false;
        }
    }
    if(!allocateTexture(outputTex,width,height,FormatInfo{})){error="Cannot allocate final color target";return false;}
    // Defined initial history only; subsequent frames read the actual
    // previous shader output. Do not initialize nonexistent geometry.
    glBindFramebuffer(GL_FRAMEBUFFER,passFbo);
    GLint maxDraw=0,maxColor=0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxDraw);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);
    for(int n=0;n<std::min({maxDraw,maxColor,int(kBuffers)});++n)
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+n,GL_TEXTURE_2D,0,0);
    const GLenum draw=GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1,&draw);glReadBuffer(draw);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const GLfloat zero[4]={0,0,0,0};
    for(unsigned i=0;i<kBuffers;++i)if(historyNeeded[i]){
        for(GLuint t:textures[i]){
            glFramebufferTexture2D(GL_FRAMEBUFFER,draw,GL_TEXTURE_2D,t,0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
                error="Cannot initialize history attachment colortex"+std::to_string(i);return false;
            }
            glClearBufferfv(GL_COLOR,0,zero);
        }
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER,draw,GL_TEXTURE_2D,0,0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glReadBuffer(GL_BACK);
    if(glGetError()!=GL_NO_ERROR){error="Cannot initialize history buffers";return false;}
    textureWidth=width;textureHeight=height;colorTargetBytes=bytes-packGpuBytes;
    return true;
}

void setUniforms(CompiledPass const& pass,int width,int height,float time,float dt) {
    glUseProgram(pass.program);
    for(auto const& b:pass.samplers){
        unsigned unit=b.unit;
        glActiveTexture(GL_TEXTURE0+unit);
        glBindSampler(unit,b.compareDepth?shadowCompareSampler:0);
        GLuint texture=b.depth>=0?depthFrame.images[unsigned(b.depth)].tex:
                      b.shadowColor>=0?depthFrame.colors[unsigned(b.shadowColor)].tex:
                      b.packTexture>=0?packGpu[unsigned(b.packTexture)].texture:
                      textures[unsigned(b.buffer)][front[unsigned(b.buffer)]];
        glBindTexture(GL_TEXTURE_2D,texture);
        if(b.depth<0&&b.shadowColor<0&&b.packTexture<0){
            unsigned index=unsigned(b.buffer),side=front[index];
            bool mip=(pass.mipmapInputs&(std::uint32_t(1)<<index))!=0;
            if(mip&&!mipValid[index][side]){
                glGenerateMipmap(GL_TEXTURE_2D);
                mipValid[index][side]=true;
            }
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,mip?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        }
        if(b.location>=0)glUniform1i(b.location,int(unit));
    }
    auto set=[&](char const* name,float value){
        GLint loc=glGetUniformLocation(pass.program,name);
        if(loc>=0)glUniform1f(loc,value);
    };
    set("viewWidth",float(width));set("viewHeight",float(height));
    set("aspectRatio",float(width)/float(height));set("frameTimeCounter",time);
    set("frameTime",dt);
    set("framemod8",float(frameCounter%8));set("framemod2",float(frameCounter%2));
    GLint frameLoc=glGetUniformLocation(pass.program,"frameCounter");
    if(frameLoc>=0)glUniform1i(frameLoc,int(frameCounter));
    for(auto const& binding:pass.worldUniforms)
        uploadWorldUniform(binding,*findWorldUniform(binding.name));
}

bool hasDeferred(){return std::any_of(pipeline.begin(),pipeline.end(),[](auto const& p){return p.deferred;});}

bool validateStage(bool deferred,int width,int height,std::string& error){
    for(auto const& pass:pipeline)if(pass.deferred==deferred)for(auto const& binding:pass.samplers){
        if(binding.depth>=0){
            auto const& d=depthFrame.images[unsigned(binding.depth)];
            bool scene=binding.depth<3;
            if(!d.fresh||(scene&&(d.width!=width||d.height!=height))){
                auto name=scene?"depthtex"+std::to_string(binding.depth):
                    "shadowtex"+std::to_string(binding.depth-3);
                error="Missing or mismatched current-frame "+name+" in "+pass.name;return false;
            }
        }
        if(binding.shadowColor>=0&&!depthFrame.colors[unsigned(binding.shadowColor)].fresh){
            error="Missing current-frame shadowcolor"+std::to_string(binding.shadowColor-int(kBuffers))+" in "+pass.name;return false;
        }
    }
    for(auto const& pass:pipeline)if(pass.deferred==deferred)for(auto const& binding:pass.worldUniforms){
        auto const* value=findWorldUniform(binding.name);
        if(!value||value->glType!=binding.type){
            error="Missing current-frame world uniform "+binding.name+" in "+pass.name;
            return false;
        }
    }
    return true;
}
bool seedScene(unsigned index,int width,int height,std::string& error){
    auto const& d=depthFrame.colors[index];
    if(!d.fresh||d.width!=width||d.height!=height||d.format.internal!=colorFormats[index].internal){
        error="Missing or mismatched native scene colortex"+std::to_string(index);return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,passFbo);
    GLint maxDraw=0,maxColor=0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxDraw);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);
    for(int n=0;n<std::min({maxDraw,maxColor,int(kBuffers)});++n)
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+n,GL_TEXTURE_2D,0,0);
    const GLenum draw=GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1,&draw);glReadBuffer(draw);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,draw,GL_TEXTURE_2D,textures[index][front[index]],0);
    if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        error="Incomplete native scene target colortex"+std::to_string(index);return false;
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,d.fbo);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if(glGetError()!=GL_NO_ERROR){error="Cannot seed native scene colortex"+std::to_string(index);return false;}
    mipValid[index][front[index]]=false;return true;
}
void beginFrameTiming(){
    if(frameClockReady)return;
    static const auto started=std::chrono::steady_clock::now();
    static auto last=started;
    auto now=std::chrono::steady_clock::now();
    frameTime=std::chrono::duration<float>(now-started).count();
    frameDelta=std::chrono::duration<float>(now-last).count();last=now;
    frameClockReady=true;
    depthFrame.frameTime=now;
}
bool drawStage(bool deferred,int width,int height,std::string& error){
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_RASTERIZER_DISCARD);
    glDepthMask(GL_FALSE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,width,height);glBindVertexArray(triangleVao);
    glBindFramebuffer(GL_FRAMEBUFFER,passFbo);
    for(auto const& pass:pipeline){
        if(pass.deferred!=deferred)continue;
        // Attach write-side texture(s) distinct from every sampled read-side.
        // MRT locations index the directive list, not the colortex number.
        int passWidth=width,passHeight=height;
        if(!pass.finalPass){
            passWidth=targetWidths[pass.targets[0]];
            passHeight=targetHeights[pass.targets[0]];
            for(auto index:pass.targets)if(targetWidths[index]!=passWidth||
                targetHeights[index]!=passHeight){
                error=pass.name+": scaled MRT targets have incompatible sizes";
                return false;
            }
        }
        glViewport(0,0,passWidth,passHeight);
        GLint maxDraw=0,maxColor=0;
        glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxDraw);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);
        unsigned slots=unsigned(std::min({maxDraw,maxColor,int(kBuffers)}));
        for(unsigned i=0;i<slots;++i)
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,0,0);
        std::vector<GLenum> draws;
        for(unsigned i=0;i<pass.targets.size();++i){
            unsigned t=pass.targets[i];
            GLenum attachment=GL_COLOR_ATTACHMENT0+i;
            GLuint write=pass.finalPass?outputTex:textures[t][1-front[t]];
            glFramebufferTexture2D(GL_FRAMEBUFFER,attachment,GL_TEXTURE_2D,write,0);
            draws.push_back(attachment);
        }
        glDrawBuffers(GLsizei(draws.size()),draws.data());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
            error=pass.name+": incomplete render-target framebuffer";return false;
        }
        // Deterministic initialization of newly produced buffers. No synthetic
        // scene depth, shadow or normal textures are ever provided as inputs.
        const GLfloat zero[4]={0,0,0,0};
        for(unsigned i=0;i<draws.size();++i)glClearBufferfv(GL_COLOR,i,zero);
        setUniforms(pass,width,height,frameTime,frameDelta);
        if(pass.customVertex)glDrawArrays(GL_TRIANGLES,0,6);
        else glDrawArrays(GL_TRIANGLES,0,3);
        if(glGetError()!=GL_NO_ERROR){error=pass.name+": GLES3 pass execution failed";return false;}
        if(!pass.finalPass)for(auto t:pass.targets){front[t]=1-front[t];mipValid[t][front[t]]=false;}
    }
    return true;
}
bool renderOpaqueStage(int width,int height,std::string& error){
    if(!hasDeferred()){error="No active deferred graph";return false;}
    if(deferredReady){error="Deferred phase already executed";return false;}
    if(!validateStage(true,width,height,error)||!ensureTargets(width,height,error))return false;
    for(unsigned i=0;i<kBuffers;++i)if(!historyNeeded[i])front[i]=0;
    for(unsigned i=0;i<kBuffers;++i){
        opaqueSerial[i]=depthFrame.colors[i].serial;
        if(deferredInputs[i]&&!seedScene(i,width,height,error))return false;
    }
    beginFrameTiming();
    if(!drawStage(true,width,height,error))return false;
    deferredReady=true;return true;
}
bool renderPipeline(int width,int height,std::string& error){
    bool staged=hasDeferred();
    if(staged&&(!deferredReady||width!=textureWidth||height!=textureHeight)){
        error="Missing current-frame opaque deferred phase";return false;
    }
    if(!validateStage(false,width,height,error))return false;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glReadBuffer(GL_BACK);
    if(!ensureTargets(width,height,error))return false;
    if(!staged){
        for(unsigned i=0;i<kBuffers;++i)if(!historyNeeded[i])front[i]=0;
        beginFrameTiming();
    }
    // In staged mode only a newly captured native attachment may replace
    // an opaque input or a deferred output. This is how the adapter submits
    // the result of subsequent translucent geometry into the shared graph.
    for(unsigned i=0;i<kBuffers;++i){
        auto const& d=depthFrame.colors[i];
        bool updated=staged&&d.fresh&&d.serial>opaqueSerial[i]&&needed[i]&&!historyNeeded[i];
        if(updated|| (sceneNeeded[i]&&(!staged||!deferredInputs[i])&&!deferredOutputs[i])){
            if(!seedScene(i,width,height,error))return false;
        }
    }
    if(!sceneNeeded[0]&&!staged){
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);
        glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
        glBindTexture(GL_TEXTURE_2D,textures[0][0]);
        glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
        mipValid[0][0]=false;
        if(glGetError()!=GL_NO_ERROR){error="Cannot capture actual Bedrock backbuffer";return false;}
    }
    if(staged&&!deferredOutputs[0]&&!deferredInputs[0]&&!depthFrame.colors[0].fresh){
        error="Missing current-frame staged scene color";return false;
    }
    if(!drawStage(false,width,height,error))return false;
    // Only publish the output after the final pass succeeds.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,passFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if(glGetError()!=GL_NO_ERROR){error="Cannot present Java composite output";return false;}
    frameCounter=(frameCounter+1)%720720;
    return true;
}

} // namespace

void requestPipeline(std::vector<Pass> passes,std::string label) {
    size_t total=0,images=0;
    bool badImages=false;
    for(size_t i=0;i<passes.size();++i){
        auto const& p=passes[i];
        total+=p.fragment.size()+p.vertex.size()+p.enabledProbe.size();
        if(i!=0&&(!p.packTextures.empty()||!p.colorFormats.empty()||!p.bufferScales.empty()||
                 !p.geometryPrograms.empty()||!p.blockRules.empty()||!p.customUniforms.empty()))badImages=true;
        if(p.bufferScales.size()>kBuffers)badImages=true;
        std::array<bool,kBuffers> scalesSeen{};
        for(auto const& scale:p.bufferScales){
            if(scale.index>=kBuffers){badImages=true;continue;}
            if(scalesSeen[scale.index]||scale.index==0||
                !std::isfinite(scale.width)||!std::isfinite(scale.height)||
                scale.width<0.125f||scale.width>1.0f||scale.height<0.125f||scale.height>1.0f)badImages=true;
            scalesSeen[scale.index]=true;
        }
        if(p.customUniforms.size()>256)badImages=true;
        if(p.shadowSettings&&(i!=0||!shaderbridge::camera::validShadowSettings(*p.shadowSettings)))badImages=true;
        for(auto const& rule:p.customUniforms)if(rule.nodes.empty()||rule.nodes.size()>512)badImages=true;
        if(p.blockRules.size()>32768)badImages=true;
        if(p.geometryPrograms.size()>24)badImages=true;
        std::vector<std::string> names;
        for(auto const& g:p.geometryPrograms){
            if(g.name.empty()||g.name.size()>64||g.vertex.empty()||g.fragment.empty()||
                std::find(names.begin(),names.end(),g.name)!=names.end())badImages=true;
            names.push_back(g.name);
            total+=g.vertex.size()+g.fragment.size()+g.enabledProbe.size();
        }
        if(p.colorFormats.size()>kBuffers)badImages=true;
        std::array<bool,kBuffers> formatSeen{};
        for(auto const& entry:p.colorFormats){
            if(entry.first>=kBuffers||entry.second.empty()||entry.second.size()>32){badImages=true;continue;}
            if(formatSeen[entry.first])badImages=true;
            formatSeen[entry.first]=true;
        }
        if(p.packTextures.size()>8)badImages=true;
        for(auto const& image:p.packTextures){
            if(image.rgba.size()>kPackTextureLimit-images){badImages=true;break;}
            images+=image.rgba.size();
        }
    }
    std::lock_guard lock(mu);
    if(passes.empty()||passes.size()>kMaxPasses||total>12*1024*1024||badImages){
        pendingAction=2;requestedActive=false;wantsNativeCapture=false;
        pendingPasses.clear();
        failed=true;lastStatus="Java composite graph exceeds pass/source budget";
        return;
    }
    wantsNativeCapture=true;
    pendingPasses=std::move(passes);
    pendingLabel=std::move(label);
    pendingAction=1;lastStatus="Compiling ordered Java composite graph";
    failed=false;
}

void requestEnable(std::string fragment,std::string label) {
    requestPipeline({Pass{"final",std::move(fragment),{0},true}},std::move(label));
}

void requestDisable() {
    wantsNativeCapture=false;
    requestedActive=false;
    std::lock_guard lock(mu);
    pendingAction=2;pendingPasses.clear();
    lastStatus="World postprocess disabled";failed=false;
}

bool captureRequested(){return wantsNativeCapture.load(std::memory_order_relaxed);}

Status status() {
    std::lock_guard lock(mu);
    return {requestedActive.load()&&!failed,failed,lastStatus};
}
RenderMetrics metrics(){
    std::lock_guard lock(mu);
    return renderMetrics;
}

static bool captureColorAttachment(unsigned colortex,unsigned framebuffer,unsigned attachment,
                       std::string const& format,int width,int height,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT){error="No EGL render context";return false;}
    syncDepthContext();
    if(colortex>=depthFrame.colors.size()){error="Invalid color snapshot index";return false;}
    auto& d=depthFrame.colors[colortex];d.fresh=false;
    FormatInfo fmt=resolveColorFormat(format);
    if(!fmt.bytes||width<=0||height<=0||width>4096||height>4096){
        error="Unsupported scene color format or dimensions";return false;
    }
    std::uint64_t bytes=std::uint64_t(width)*height*fmt.bytes;
    if(colorTargetBytes+packGpuBytes+depthBytes()-d.bytes+bytes>kMemoryLimit){
        error="Scene color exceeds GPU memory budget";return false;
    }
    SavedState saved;
    GLint sourceRead=GL_NONE;
    bool sourceBound=false;
    auto finish=[&](bool ok,char const* message){
        if(sourceBound){
            glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(framebuffer));
            glReadBuffer(GLenum(sourceRead));
        }
        saved.restore();
        if(!ok){error=message;d.fresh=false;}
        return ok;
    };
    if(glGetError()!=GL_NO_ERROR)return finish(false,"Pre-existing GLES error at scene color capture");
    GLint maxColor=0;glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);
    if(framebuffer?(attachment>=unsigned(maxColor)):(attachment!=0))
        return finish(false,"Invalid native scene attachment");
    GLenum read=framebuffer?GL_COLOR_ATTACHMENT0+attachment:GL_BACK;
    glBindFramebuffer(GL_FRAMEBUFFER,GLuint(framebuffer));
    GLint samples=0;glGetIntegerv(GL_SAMPLES,&samples);
    if(samples!=0||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        return finish(false,"Scene source must be a complete single-sample framebuffer");
    GLint red=0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,read,
        GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,&red);
    if(red<=0||glGetError()!=GL_NO_ERROR)return finish(false,"Native color attachment not present");
    glGetIntegerv(GL_READ_BUFFER,&sourceRead);
    sourceBound=true;
    if(!d.tex||d.width!=width||d.height!=height||d.format.internal!=fmt.internal){
        releaseColor(d);
        glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        if(!allocateTexture(d.tex,width,height,fmt)){
            releaseColor(d);return finish(false,"Cannot allocate native scene color texture");
        }
        glGenFramebuffers(1,&d.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER,d.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,d.tex,0);
        const GLenum draw=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&draw);glReadBuffer(draw);
        if(!d.fbo||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
           glGetError()!=GL_NO_ERROR){
            releaseColor(d);return finish(false,"Cannot allocate native scene color framebuffer");
        }
        d.width=width;d.height=height;d.format=fmt;d.bytes=bytes;
    }
    glDisable(GL_SCISSOR_TEST);glDisable(GL_RASTERIZER_DISCARD);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(framebuffer));
    glReadBuffer(read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,d.fbo);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if(glGetError()!=GL_NO_ERROR)return finish(false,"Native scene color copy failed");
    d.serial=++snapshotSerial;d.fresh=true;return finish(true,"");
}

bool captureSceneColor(unsigned colortex,unsigned framebuffer,unsigned attachment,
                       std::string const& format,int width,int height,std::string& error){
    if(colortex>=kBuffers){error="Invalid scene color index";return false;}
    return captureColorAttachment(colortex,framebuffer,attachment,format,width,height,error);
}
bool captureShadowColor(unsigned index,unsigned framebuffer,unsigned attachment,
                       std::string const& format,int width,int height,std::string& error){
    if(index>=2){error="Invalid shadow color index";return false;}
    return captureColorAttachment(kBuffers+index,framebuffer,attachment,format,width,height,error);
}

bool publishWorldUniforms(std::vector<WorldUniform> const& values,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT){error="No EGL render context";return false;}
    syncDepthContext();
    if(values.size()>128){error="Too many world uniforms";return false;}
    // Validate the whole batch before committing it to the frame.
    auto next=depthFrame.uniforms;
    for(auto const& v:values){
        int components=uniformComponents(v.glType);
        bool valid=!v.name.empty()&&v.name.size()<128&&components>0;
        for(char c:v.name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                               (c>='0'&&c<='9')||c=='_'))valid=false;
        if(valid&&!integerUniform(v.glType))
            for(int i=0;i<components;++i)if(!std::isfinite(v.floats[i]))valid=false;
        if(!valid){error="Invalid world uniform "+v.name;return false;}
        auto old=std::find_if(next.begin(),next.end(),[&](auto const& x){return x.name==v.name;});
        if(old!=next.end()){
            if(old->glType!=v.glType){error="World uniform type changed: "+v.name;return false;}
            *old=v;
        }else next.push_back(v);
        if(next.size()>128){error="Too many world uniforms in frame";return false;}
    }
    depthFrame.uniforms=std::move(next);deriveCamera();refreshCustomUniforms();
    return true;
}

bool captureProgramUniforms(unsigned program,std::vector<UniformMapping> const& mappings,
                            std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT||!program||!glIsProgram(program)){
        error="No valid native GLES program";return false;
    }
    if(mappings.empty()||mappings.size()>128){error="Invalid uniform mapping count";return false;}
    GLint linked=0,count=0;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){error="Native program not linked";return false;}
    glGetProgramiv(program,GL_ACTIVE_UNIFORMS,&count);
    std::vector<WorldUniform> values;
    for(auto const& mapping:mappings){
        if(!uniformComponents(mapping.glType)){error="Unsupported mapped type";return false;}
        bool matched=false;
        for(GLint n=0;n<count;++n){
            GLchar name[256]{};GLsizei len=0;GLint size=0;GLenum type=0;
            glGetActiveUniform(program,GLuint(n),sizeof(name),&len,&size,&type,name);
            if(mapping.nativeName!=std::string(name,size_t(std::max(0,int(len)))))continue;
            if(size!=1||type!=mapping.glType)break;
            GLint location=glGetUniformLocation(program,name);
            if(location<0)break;
            WorldUniform value;value.name=mapping.javaName;value.glType=type;
            if(integerUniform(type))glGetUniformiv(program,location,value.ints.data());
            else glGetUniformfv(program,location,value.floats.data());
            values.push_back(std::move(value));matched=true;break;
        }
        if(!matched){error="Native uniform missing or wrong type: "+mapping.nativeName;return false;}
    }
    if(glGetError()!=GL_NO_ERROR){error="Native uniform capture failed";return false;}
    return publishWorldUniforms(values,error);
}

static bool captureDepthAttachment(unsigned n,unsigned framebuffer,unsigned internalFormat,
                       int width,int height,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT){error="No EGL render context";return false;}
    syncDepthContext();
    if(n>=depthFrame.images.size()){error="Unknown depth snapshot";return false;}
    auto& d=depthFrame.images[n];d.fresh=false;
    GLenum fmt=GLenum(internalFormat);
    bool stencil=fmt==GL_DEPTH24_STENCIL8||fmt==GL_DEPTH32F_STENCIL8;
    unsigned bpp=fmt==GL_DEPTH_COMPONENT16?2:
        fmt==GL_DEPTH_COMPONENT24||fmt==GL_DEPTH_COMPONENT32F||fmt==GL_DEPTH24_STENCIL8?4:
        fmt==GL_DEPTH32F_STENCIL8?8:0;
    if(!bpp||width<=0||height<=0||width>4096||height>4096){
        error="Unsupported scene depth format or dimensions";return false;
    }
    std::uint64_t bytes=std::uint64_t(width)*height*bpp;
    if(colorTargetBytes+packGpuBytes+depthBytes()-d.bytes+bytes>kMemoryLimit){
        error="Scene depth exceeds GPU memory budget";return false;
    }
    SavedState saved;
    auto finish=[&](bool ok,char const* message){
        saved.restore();
        if(!ok){error=message;d.fresh=false;}
        return ok;
    };
    // The caller identifies the phase and attachment format. GLES validates
    // source/destination compatibility rather than sampling a dummy texture.
    if(glGetError()!=GL_NO_ERROR)return finish(false,"Pre-existing GLES error at depth capture");
    glBindFramebuffer(GL_FRAMEBUFFER,GLuint(framebuffer));
    GLint samples=0;glGetIntegerv(GL_SAMPLES,&samples);
    if(samples!=0||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        return finish(false,"Depth source must be a complete single-sample framebuffer");
    GLint bits=0,stencilBits=0;
    GLenum attachment=framebuffer?GL_DEPTH_ATTACHMENT:GL_DEPTH;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,attachment,
        GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE,&bits);
    if(stencil)glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
        framebuffer?GL_STENCIL_ATTACHMENT:GL_STENCIL,
        GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE,&stencilBits);
    int expected=fmt==GL_DEPTH_COMPONENT16?16:
        fmt==GL_DEPTH_COMPONENT32F||fmt==GL_DEPTH32F_STENCIL8?32:24;
    if(bits!=expected||(stencil&&stencilBits==0)||glGetError()!=GL_NO_ERROR)
        return finish(false,"Depth source attachment format mismatch");
    if(!d.tex||d.width!=width||d.height!=height||d.format!=fmt){
        releaseDepth(d);
        glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);
        glGenTextures(1,&d.tex);glBindTexture(GL_TEXTURE_2D,d.tex);
        glTexStorage2D(GL_TEXTURE_2D,1,fmt,width,height);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glGenFramebuffers(1,&d.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER,d.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,stencil?GL_DEPTH_STENCIL_ATTACHMENT:GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D,d.tex,0);
        const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
        if(!d.tex||!d.fbo||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
           glGetError()!=GL_NO_ERROR){
            releaseDepth(d);return finish(false,"Cannot allocate a real depth snapshot");
        }
        d.width=width;d.height=height;d.format=fmt;d.bytes=bytes;
    }
    glDisable(GL_SCISSOR_TEST);glDisable(GL_RASTERIZER_DISCARD);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(framebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,d.fbo);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    if(glGetError()!=GL_NO_ERROR)return finish(false,"Real scene depth copy failed");
    d.fresh=true;
    return finish(true,"");
}

bool captureSceneDepth(DepthPhase phase,unsigned framebuffer,unsigned internalFormat,
                       int width,int height,std::string& error){
    unsigned n=unsigned(phase);
    if(n>=3){error="Unknown depth phase";return false;}
    return captureDepthAttachment(n,framebuffer,internalFormat,width,height,error);
}
bool getShadowCamera(shaderbridge::camera::ShadowCamera& camera,int& resolution,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT||eglGetCurrentContext()!=context||
       eglGetCurrentSurface(EGL_DRAW)!=pipelineSurface||!computeShadowCamera(camera,resolution)){
        error="Missing real Java sun angle/camera or unsupported shadow settings";return false;
    }
    for(auto const* name:{"shadowModelView","shadowProjection","shadowModelViewInverse","shadowProjectionInverse"})
        if(named(depthFrame.uniforms,name)){error="Explicit shadow matrices already supplied";return false;}
    return true;
}
bool captureShadowDepth(unsigned index,unsigned framebuffer,unsigned internalFormat,
                        int width,int height,std::string& error){
    if(index>=2){error="Invalid shadow depth index";return false;}
    return captureDepthAttachment(3+index,framebuffer,internalFormat,width,height,error);
}

bool acquireGeometryProgram(std::string const& name,GeometryHandle& result,std::string& error){
    result={};
    if(eglGetCurrentContext()==EGL_NO_CONTEXT||eglGetCurrentContext()!=context||
       eglGetCurrentSurface(EGL_DRAW)!=pipelineSurface||!captureRequested()||selectedSource.empty()){
        error="No selected Java geometry graph on this render surface";return false;
    }
    {
        std::lock_guard lock(mu);
        if(pendingAction!=0){error="Java shader selection is changing";return false;}
    }
    for(size_t i=0;i<geometryNames.size();++i)if(geometryNames[i]==name){
        result=geometryHandles[i];return true;
    }
    if(!preparePackResources(error))return false;
    auto const& sources=selectedSource.front().geometryPrograms;
    auto source=std::find_if(sources.begin(),sources.end(),[&](auto const& g){return g.name==name;});
    if(source==sources.end()){error="Java geometry program unavailable: "+name;return false;}
    if(!source->enabledProbe.empty()){
        Pass metadata;metadata.enabledProbe=source->enabledProbe;
        bool enabled=true;std::uint32_t mip=0,history=0;
        if(!evaluateEnabled(metadata,enabled,mip,history,error)){
            error=name+" program.enabled: "+error;return false;
        }
        if(!enabled){error="Java geometry program disabled by shader-pack settings: "+name;return false;}
    }
    GLuint vs=compile(GL_VERTEX_SHADER,source->vertex,error);
    if(!vs){error=name+" vertex: "+error;return false;}
    GLuint fs=compile(GL_FRAGMENT_SHADER,source->fragment,error);
    if(!fs){glDeleteShader(vs);error=name+" fragment: "+error;return false;}
    GLuint program=glCreateProgram();
    if(!program){glDeleteShader(vs);glDeleteShader(fs);error="Cannot create geometry program";return false;}
    glAttachShader(program,vs);glAttachShader(program,fs);
    // Fixed semantic locations for a separately constructed real geometry VAO.
    static char const* attrs[]={"optPosition","optColor","optNormal","optUV0","optUV1",
        "mc_Entity","mc_midTexCoord","at_tangent","at_midBlock"};
    for(unsigned i=0;i<sizeof(attrs)/sizeof(attrs[0]);++i)glBindAttribLocation(program,i,attrs[i]);
    glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    auto fail=[&](std::string message){glDeleteProgram(program);error=name+": "+message;return false;};
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok)return fail(shaderLog(program,true));
    GeometryHandle g;g.program=program;g.shadow=source->shadow;
    GLint count=0;glGetProgramiv(program,GL_ACTIVE_ATTRIBUTES,&count);
    for(GLint i=0;i<count;++i){
        GLchar attr[128]{};GLsizei len=0;GLint size=0;GLenum type=0;
        glGetActiveAttrib(program,GLuint(i),sizeof(attr),&len,&size,&type,attr);
        auto loc=glGetAttribLocation(program,attr);
        if(loc<0)continue;
        bool known=false;
        for(unsigned j=0;j<sizeof(attrs)/sizeof(attrs[0]);++j)
            if(std::string(attr)==attrs[j]&&unsigned(loc)==j&&size==1)known=true;
        if(!known)return fail("Unsupported geometry vertex input "+std::string(attr));
        g.attributes.emplace_back(attr,unsigned(loc));
    }
    std::array<int,16> routes{};routes.fill(-1);
    GLint limit=0;glGetIntegerv(GL_MAX_DRAW_BUFFERS,&limit);
    for(unsigned i=0;i<16;++i){
        auto out="optRouteTarget"+std::to_string(i);
        auto loc=glGetFragDataLocation(program,out.c_str());
        if(loc<0)continue;
        if(loc>=16||loc>=limit||routes[unsigned(loc)]!=-1)return fail("Invalid geometry output route");
        routes[unsigned(loc)]=int(i);
    }
    bool gap=false;
    for(auto i:routes){if(i<0)gap=true;else{if(gap)return fail("Noncontiguous geometry route");g.targets.push_back(unsigned(i));}}
    if(g.targets.empty())return fail("No reflected geometry output");
    geometryNames.push_back(name);geometryHandles.push_back(g);result=std::move(g);return true;
}

bool translateTerrainQuad(std::string const& canonicalName,mesh::State const& states,
    std::array<mesh::NativeVertex,4> const& vertices,mesh::V3 center,
    std::array<mesh::JavaVertex,4>& output,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT||eglGetCurrentContext()!=context||
       eglGetCurrentSurface(EGL_DRAW)!=pipelineSurface||!captureRequested()||selectedSource.empty()){
        error="No selected Java terrain graph";return false;
    }
    {std::lock_guard lock(mu);if(pendingAction!=0){error="Java shader selection changing";return false;}}
    int id=0;
    if(canonicalName.empty()||canonicalName.find(':')==std::string::npos){
        error="Missing canonical Java block name";return false;
    }
    auto match=mesh::resolveBlockId(selectedSource.front().blockRules,canonicalName,states,id);
    if(match==mesh::BlockMatch::Ambiguous){error="Ambiguous Java block state: "+canonicalName;return false;}
    // A verified ordinary block absent from block.properties has ID zero,
    // distinct from an unknown native block identity or conflicting rule.
    if(match==mesh::BlockMatch::Unlisted)id=0;
    return mesh::translateQuad(vertices,center,id,output,error);
}

bool drawGeometry(GeometryDraw const& draw,std::string& error){
    GeometryHandle g;
    if(!acquireGeometryProgram(draw.program,g,error))return false;
    if(!draw.vao||!glIsVertexArray(draw.vao)||!draw.framebuffer||
       !glIsFramebuffer(draw.framebuffer)||draw.width<=0||draw.height<=0||
       draw.width>4096||draw.height>4096||draw.count<=0||draw.first<0||draw.instances<=0||
       (draw.indexType&&draw.indexType!=GL_UNSIGNED_SHORT&&draw.indexType!=GL_UNSIGNED_INT)){
        error="Invalid native geometry draw";return false;
    }
    if(draw.uniforms.size()>128||draw.textures2D.size()>16){
        error="Native geometry binding budget exceeded";return false;
    }
    if(!g.shadow&&!beginJavaFrame(draw.width,draw.height,error))return false;
    beginFrameTiming();
    refreshCustomUniforms();
    SavedState saved;
    std::vector<GLenum> oldDraws;
    bool changed=false;
    auto finish=[&](bool ok,std::string message){
        if(changed){
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw.framebuffer);
            glDrawBuffers(GLsizei(oldDraws.size()),oldDraws.data());
        }
        saved.restore();
        if(!ok)error=std::move(message);
        return ok;
    };
    glBindVertexArray(draw.vao);
    for(auto const& attr:g.attributes){
        GLint enabled=0,buffer=0;
        glGetVertexAttribiv(attr.second,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
        glGetVertexAttribiv(attr.second,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);
        if(!enabled||!buffer)return finish(false,"Missing mapped geometry attribute "+attr.first);
    }
    if(draw.indexType){
        GLint buffer=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&buffer);
        if(!buffer)return finish(false,"Missing geometry index buffer");
        GLint64 bytes=0;glGetBufferParameteri64v(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&bytes);
        unsigned stride=draw.indexType==GL_UNSIGNED_SHORT?2:4;
        if(draw.indexOffset%stride||bytes<0||draw.indexOffset>std::uint64_t(bytes)||
           std::uint64_t(draw.count)*stride>std::uint64_t(bytes)-draw.indexOffset)
            return finish(false,"Geometry index range exceeds buffer");
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw.framebuffer);
    GLint maxDraw=0,maxColor=0;
    glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxDraw);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);
    if(g.targets.size()>size_t(maxDraw)||g.targets.size()>size_t(maxColor))
        return finish(false,"Geometry route exceeds native attachments");
    std::vector<GLenum> routes;
    std::vector<GLuint> attachedTextures;
    auto attachment=[&](GLenum slot,bool required){
        GLint type=0,object=0;
        glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,
            GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,&type);
        if(type==GL_NONE)return !required;
        if(type==GL_TEXTURE){
            glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,slot,
                GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&object);
            attachedTextures.push_back(GLuint(object));
        }
        return true;
    };
    for(int i=0;i<maxColor;++i){
        GLenum slot=GL_COLOR_ATTACHMENT0+i;
        bool required=size_t(i)<g.targets.size();
        if(!attachment(slot,required))return finish(false,"Missing geometry color target");
        if(required)routes.push_back(slot);
    }
    if(!attachment(GL_DEPTH_ATTACHMENT,true))return finish(false,"Missing geometry depth target");
    attachment(GL_STENCIL_ATTACHMENT,false);
    for(int i=0;i<maxDraw;++i){
        GLint value=0;glGetIntegerv(GL_DRAW_BUFFER0+i,&value);
        oldDraws.push_back(GLenum(value));
    }
    glDrawBuffers(GLsizei(routes.size()),routes.data());changed=true;
    if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        return finish(false,"Incomplete geometry framebuffer");
    struct Input { GLint loc; GLuint texture; GLenum type; };
    std::vector<Input> inputs;
    std::vector<std::pair<WorldBinding,WorldUniform>> values;
    GLint count=0;glGetProgramiv(g.program,GL_ACTIVE_UNIFORMS,&count);
    for(GLint i=0;i<count;++i){
        char name[256]{};GLsizei length=0;GLint size=0;GLenum type=0;
        glGetActiveUniform(g.program,GLuint(i),sizeof(name),&length,&size,&type,name);
        GLint loc=glGetUniformLocation(g.program,name);
        if(size!=1||loc<0)return finish(false,"Unsupported geometry array/block "+std::string(name));
        if(type==GL_SAMPLER_2D||type==GL_SAMPLER_2D_SHADOW){
            auto tex=std::find_if(draw.textures2D.begin(),draw.textures2D.end(),[&](auto const& t){return t.first==name;});
            GLuint object=tex==draw.textures2D.end()?0:tex->second;
            if(!object){
                auto pack=std::find_if(packGpu.begin(),packGpu.end(),[&](auto const& t){return t.sampler==name;});
                if(pack!=packGpu.end())object=pack->texture;
            }
            if(!object){
                int depth=depthIndex(name);
                if(depth>=0&&depthFrame.images[unsigned(depth)].fresh)
                    object=depthFrame.images[unsigned(depth)].tex;
                int shadow=std::string(name)=="shadowcolor0"?0:std::string(name)=="shadowcolor1"?1:-1;
                if(shadow>=0&&depthFrame.colors[kBuffers+unsigned(shadow)].fresh)
                    object=depthFrame.colors[kBuffers+unsigned(shadow)].tex;
                int color=bufferIndex(name);
                // "texture", "tex" and the translated atlas are material
                // samplers, not aliases for the world color in geometry.
                if(color>=0&&std::string(name)!="texture"&&std::string(name)!="tex"){
                    unsigned n=unsigned(color);
                    if(deferredReady&&deferredOutputs[n])object=textures[n][front[n]];
                    else if(depthFrame.colors[n].fresh)object=depthFrame.colors[n].tex;
                }
            }
            if(!object||!glIsTexture(object)||inputs.size()>=kBuffers||
               std::find(attachedTextures.begin(),attachedTextures.end(),object)!=attachedTextures.end())
                return finish(false,"Missing or feedback geometry texture "+std::string(name));
            inputs.push_back({loc,object,type});
        }else{
            auto builtin=frameBuiltin(name);
            auto value=builtin?&*builtin:named(draw.uniforms,name);
            if(!value)value=findWorldUniform(name);
            int components=uniformComponents(type);
            if(!value||value->glType!=type||!components)return finish(false,"Missing geometry uniform "+std::string(name));
            if(!integerUniform(type))for(int j=0;j<components;++j)
                if(!std::isfinite(value->floats[j]))return finish(false,"Nonfinite geometry uniform "+std::string(name));
            values.push_back({{name,type,loc},*value});
        }
    }
    GLint vertexUnits=0,fragmentUnits=0,combinedUnits=0;
    glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,&vertexUnits);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS,&fragmentUnits);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,&combinedUnits);
    if(inputs.size()>size_t(combinedUnits)||inputs.size()>size_t(fragmentUnits))
        return finish(false,"Insufficient geometry texture units");
    if(glGetError()!=GL_NO_ERROR)return finish(false,"Native geometry validation generated GL error");
    // The adapter controls depth/blend/culling, and owns the geometry FBO.
    // This call never writes the game's presentation framebuffer.
    glUseProgram(g.program);glViewport(0,0,draw.width,draw.height);
    GLuint compare=0;
    if(std::any_of(inputs.begin(),inputs.end(),[](auto const& t){return t.type==GL_SAMPLER_2D_SHADOW;})){
        glGenSamplers(1,&compare);
        glSamplerParameteri(compare,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(compare,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
        glSamplerParameteri(compare,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glSamplerParameteri(compare,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glSamplerParameteri(compare,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glSamplerParameteri(compare,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    for(unsigned i=0;i<inputs.size();++i){
        auto const& t=inputs[i];
        glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,t.texture);
        glBindSampler(i,t.type==GL_SAMPLER_2D_SHADOW?compare:0);glUniform1i(t.loc,GLint(i));
    }
    for(auto const& v:values)uploadWorldUniform(v.first,v.second);
    bool bound=glGetError()==GL_NO_ERROR;
    if(bound){
        if(draw.indexType)glDrawElementsInstanced(GL_TRIANGLES,draw.count,draw.indexType,
            reinterpret_cast<void const*>(draw.indexOffset),draw.instances);
        else glDrawArraysInstanced(GL_TRIANGLES,draw.first,draw.count,draw.instances);
    }
    bool ok=bound&&glGetError()==GL_NO_ERROR;
    if(compare)glDeleteSamplers(1,&compare);
    return finish(ok,ok?"":"Native Java geometry draw failed");
}

bool renderShadowScene(std::vector<GeometryDraw> const& opaque,
                       std::vector<GeometryDraw> const& translucent,std::string& error){
    if(opaque.size()+translucent.size()==0||opaque.size()+translucent.size()>65536){
        error="No supplied world shadow geometry or excessive batch count";return false;
    }
    shaderbridge::camera::ShadowCamera light;int size=0;
    if(!getShadowCamera(light,size,error)||size>4096)return false;
    syncDepthContext();
    std::array<bool,2> colors{};
    auto inspect=[&](std::vector<GeometryDraw> const& draws){
        for(auto const& draw:draws){
            GeometryHandle g;
            if(!acquireGeometryProgram(draw.program,g,error))return false;
            if(!g.shadow){error="Non-shadow program in light-space pass";return false;}
            for(auto target:g.targets){
                if(target>=2){error="Unsupported shadow color target";return false;}
                colors[target]=true;
            }
        }
        return true;
    };
    // Invalidate old data even if a new scene fails validation.
    auto& all=depthFrame.images[3];auto& solid=depthFrame.images[4];
    all.fresh=solid.fresh=false;
    for(unsigned i=0;i<2;++i)depthFrame.colors[kBuffers+i].fresh=false;
    if(!inspect(opaque)||!inspect(translucent))return false;
    SavedState saved;
    GLint oldDepthFunc=GL_LESS;GLfloat oldRange[2]{};
    glGetIntegerv(GL_DEPTH_FUNC,&oldDepthFunc);glGetFloatv(GL_DEPTH_RANGE,oldRange);
    GLuint work=0;
    auto finish=[&](bool ok){
        if(work)glDeleteFramebuffers(1,&work);
        glDepthFunc(GLenum(oldDepthFunc));glDepthRangef(oldRange[0],oldRange[1]);
        saved.restore();
        if(!ok){
            all.fresh=solid.fresh=false;
            for(unsigned i=0;i<2;++i)depthFrame.colors[kBuffers+i].fresh=false;
        }
        return ok;
    };
    if(glGetError()!=GL_NO_ERROR){error="Pre-existing GLES error at shadow pass";return finish(false);}
    std::uint64_t pixels=std::uint64_t(size)*size;
    std::uint64_t oldBytes=all.bytes+solid.bytes;
    unsigned channels=0;
    for(unsigned i=0;i<2;++i){
        oldBytes+=depthFrame.colors[kBuffers+i].bytes;
        channels+=colors[i]?1:0;
    }
    if(colorTargetBytes+packGpuBytes+depthBytes()-oldBytes+pixels*(8+4*channels)>kMemoryLimit){
        error="Java shadow scene exceeds GPU memory budget";return finish(false);
    }
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    auto depth=[&](DepthSnapshot& d){
        if(d.tex&&d.fbo&&d.width==size&&d.height==size&&d.format==GL_DEPTH_COMPONENT24)return true;
        releaseDepth(d);
        glGenTextures(1,&d.tex);glBindTexture(GL_TEXTURE_2D,d.tex);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH_COMPONENT24,size,size);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glGenFramebuffers(1,&d.fbo);glBindFramebuffer(GL_FRAMEBUFFER,d.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,d.tex,0);
        const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
        if(!d.tex||!d.fbo||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
           glGetError()!=GL_NO_ERROR){releaseDepth(d);return false;}
        d.width=d.height=size;d.format=GL_DEPTH_COMPONENT24;d.bytes=pixels*4;
        return true;
    };
    if(!depth(all)||!depth(solid)){error="Cannot allocate Java shadow depth targets";return finish(false);}
    for(unsigned i=0;i<2;++i){
        auto& d=depthFrame.colors[kBuffers+i];
        if(!colors[i]){releaseColor(d);continue;}
        if(d.tex&&d.width==size&&d.height==size&&d.format.internal==GL_RGBA8)continue;
        releaseColor(d);
        d.format=resolveColorFormat("RGBA8");
        if(!allocateTexture(d.tex,size,size,d.format)){
            releaseColor(d);error="Cannot allocate Java shadow color target";return finish(false);
        }
        d.width=d.height=size;d.bytes=pixels*4;
    }
    glGenFramebuffers(1,&work);glBindFramebuffer(GL_FRAMEBUFFER,work);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,all.tex,0);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_RASTERIZER_DISCARD);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glDepthRangef(0,1);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const GLfloat farDepth=1;
    // Clear each owned color attachment once. Default white is the empty
    // light transmission value, not a fabricated finished shadow map.
    const GLfloat white[4]={1,1,1,1};
    const GLenum first=GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1,&first);glReadBuffer(first);
    for(unsigned i=0;i<2;++i)if(colors[i]){
        glFramebufferTexture2D(GL_FRAMEBUFFER,first,GL_TEXTURE_2D,depthFrame.colors[kBuffers+i].tex,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
            error="Incomplete shadow color framebuffer";return finish(false);
        }
        glClearBufferfv(GL_COLOR,0,white);
    }
    glClearBufferfv(GL_DEPTH,0,&farDepth);
    if(!work||glGetError()!=GL_NO_ERROR){error="Cannot clear Java shadow targets";return finish(false);}
    auto render=[&](std::vector<GeometryDraw> const& draws){
        for(auto draw:draws){
            GeometryHandle g;
            if(!acquireGeometryProgram(draw.program,g,error))return false;
            glBindFramebuffer(GL_FRAMEBUFFER,work);
            for(unsigned slot=0;slot<2;++slot){
                GLuint texture=slot<g.targets.size()?depthFrame.colors[kBuffers+g.targets[slot]].tex:0;
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+slot,GL_TEXTURE_2D,texture,0);
            }
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            draw.framebuffer=work;draw.width=draw.height=size;
            auto matrix=[&](char const* name,shaderbridge::camera::Matrix const& m){
                draw.uniforms.erase(std::remove_if(draw.uniforms.begin(),draw.uniforms.end(),
                    [&](auto const& u){return u.name==name;}),draw.uniforms.end());
                WorldUniform u;u.name=name;u.glType=GL_FLOAT_MAT4;u.floats=m;
                draw.uniforms.push_back(std::move(u));
            };
            // Mesh input coordinates must already be camera-relative; a
            // separate native chunk-local transform must be applied by caller.
            matrix("optModelView",light.view);matrix("optProjection",light.projection);
            matrix("optModelViewProjection",shaderbridge::camera::multiply(light.projection,light.view));
            WorldUniform normal;normal.name="optNormalMatrix";normal.glType=GL_FLOAT_MAT3;
            for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)normal.floats[c*3+r]=light.view[c*4+r];
            draw.uniforms.erase(std::remove_if(draw.uniforms.begin(),draw.uniforms.end(),
                [](auto const& u){return u.name=="optNormalMatrix";}),draw.uniforms.end());
            draw.uniforms.push_back(normal);
            if(!drawGeometry(draw,error))return false;
        }
        return true;
    };
    if(!render(opaque))return finish(false);
    // shadowtex1 excludes translucent casters; shadowtex0 includes them.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,all.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,solid.fbo);
    glBlitFramebuffer(0,0,size,size,0,0,size,size,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    if(glGetError()!=GL_NO_ERROR){error="Cannot preserve opaque shadow depth";return finish(false);}
    if(!render(translucent))return finish(false);
    all.fresh=solid.fresh=true;
    for(unsigned i=0;i<2;++i)if(colors[i])depthFrame.colors[kBuffers+i].fresh=true;
    return finish(true);
}

bool drawTerrainQuads(GeometryDraw const& draw,std::vector<TerrainQuad> const& faces,std::string& error){
    if(faces.empty()||faces.size()>16384||draw.instances!=1){
        error="Unsupported terrain batch size or instancing";return false;
    }
    std::vector<mesh::JavaVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(faces.size()*4);indices.reserve(faces.size()*6);
    for(auto const& face:faces){
        std::array<mesh::JavaVertex,4> mapped{};
        if(!translateTerrainQuad(face.canonicalName,face.states,face.vertices,face.center,mapped,error))return false;
        auto base=std::uint32_t(vertices.size());
        vertices.insert(vertices.end(),mapped.begin(),mapped.end());
        for(auto i:{0u,1u,2u,0u,2u,3u})indices.push_back(base+i);
    }
    GLint oldVao=0,oldArray=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldArray);
    GLuint vao=0,vbo=0,ebo=0;
    glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenBuffers(1,&ebo);
    auto cleanup=[&](){
        glBindVertexArray(GLuint(oldVao));glBindBuffer(GL_ARRAY_BUFFER,GLuint(oldArray));
        if(ebo)glDeleteBuffers(1,&ebo);if(vbo)glDeleteBuffers(1,&vbo);
        if(vao)glDeleteVertexArrays(1,&vao);
    };
    if(!vao||!vbo||!ebo){cleanup();error="Cannot allocate mapped terrain buffers";return false;}
    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(mesh::JavaVertex)),vertices.data(),GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,GLsizeiptr(indices.size()*sizeof(std::uint32_t)),indices.data(),GL_STREAM_DRAW);
    using V=mesh::JavaVertex;
    const std::array<std::pair<GLint,size_t>,9> attrs={{
        {4,offsetof(V,position)},{4,offsetof(V,color)},{3,offsetof(V,normal)},
        {4,offsetof(V,uv0)},{4,offsetof(V,uv1)},{4,offsetof(V,entity)},
        {4,offsetof(V,midUV)},{4,offsetof(V,tangent)},{3,offsetof(V,midBlock)}
    }};
    for(unsigned i=0;i<attrs.size();++i){
        glVertexAttribPointer(i,attrs[i].first,GL_FLOAT,GL_FALSE,sizeof(V),
            reinterpret_cast<void const*>(attrs[i].second));
        glEnableVertexAttribArray(i);
    }
    bool ok=glGetError()==GL_NO_ERROR;
    if(ok){
        auto batch=draw;
        batch.vao=vao;batch.first=0;batch.count=int(indices.size());
        batch.indexType=GL_UNSIGNED_INT;batch.indexOffset=0;
        ok=drawGeometry(batch,error);
    }else error="Mapped terrain buffer upload failed";
    cleanup();
    return ok;
}

bool beginJavaFrame(int width,int height,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT||eglGetCurrentContext()!=context||
       eglGetCurrentSurface(EGL_DRAW)!=pipelineSurface||selectedSource.empty()){
        error="No selected Java frame on this EGL surface";return false;
    }
    if(width<=0||height<=0||width>4096||height>4096){
        error="Invalid Java frame viewport";return false;
    }
    if(frameViewWidth>0&&(width!=frameViewWidth||height!=frameViewHeight)){
        error="Java frame viewport changed during rendering";return false;
    }
    frameViewWidth=width;frameViewHeight=height;
    beginFrameTiming();
    return true;
}
bool renderJavaWorldScene(WorldScene const& scene,std::string& error){
    if(worldSceneRendered){error="Java world scene already executed this frame";return false;}
    if(scene.opaque.empty()||scene.opaque.size()+scene.hand.size()+scene.translucent.size()>65536){
        error="No supplied opaque world geometry or excessive batch count";return false;
    }
    for(float v:scene.fogClear)if(!std::isfinite(v)){error="Invalid world fog clear color";return false;}
    if(!beginJavaFrame(scene.width,scene.height,error))return false;
    syncDepthContext();
    std::array<bool,kBuffers> used{};
    std::array<FormatInfo,kBuffers> formats{};
    for(auto const& entry:selectedSource.front().colorFormats)formats[entry.first]=resolveColorFormat(entry.second);
    auto inspect=[&](std::vector<WorldBatch> const& draws){
        for(auto const& batch:draws){
            GeometryHandle g;
            if(!acquireGeometryProgram(batch.geometry.program,g,error))return false;
            if(g.shadow){error="Shadow program in main geometry stage";return false;}
            for(auto i:g.targets){
                if(i>=kBuffers||!formats[i].bytes||historyNeeded[i]||
                   bufferScales[i][0]!=1.0f||bufferScales[i][1]!=1.0f){
                    error="Unsupported main geometry output or temporal conflict";return false;
                }
                used[i]=true;
            }
        }
        return true;
    };
    // The set is taken from the linked, active geometry programs, not
    // arbitrary requested samplers that would turn into fake buffers.
    if(!inspect(scene.opaque)||!inspect(scene.hand)||!inspect(scene.translucent))return false;
    if(!used[0]){error="No actual main scene color producer";return false;}
    SavedState saved;
    GLint oldDepthFunc=GL_LESS,srcRgb=0,dstRgb=0,srcAlpha=0,dstAlpha=0,eqRgb=0,eqAlpha=0;
    GLfloat oldRange[2]{};
    glGetIntegerv(GL_DEPTH_FUNC,&oldDepthFunc);glGetFloatv(GL_DEPTH_RANGE,oldRange);
    glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRgb);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqAlpha);
    GLuint work=0,read=0;
    auto invalidate=[&](){
        for(unsigned i=0;i<3;++i)depthFrame.images[i].fresh=false;
        for(unsigned i=0;i<kBuffers;++i)depthFrame.colors[i].fresh=false;
        deferredReady=false;
    };
    auto finish=[&](bool ok){
        if(work)glDeleteFramebuffers(1,&work);
        if(read)glDeleteFramebuffers(1,&read);
        glDepthFunc(GLenum(oldDepthFunc));glDepthRangef(oldRange[0],oldRange[1]);
        glBlendFuncSeparate(GLenum(srcRgb),GLenum(dstRgb),GLenum(srcAlpha),GLenum(dstAlpha));
        glBlendEquationSeparate(GLenum(eqRgb),GLenum(eqAlpha));
        saved.restore();
        if(!ok)invalidate();
        worldSceneRendered=ok;return ok;
    };
    invalidate();
    if(glGetError()!=GL_NO_ERROR){error="Pre-existing GLES error at Java world stage";return finish(false);}
    if(!scene.shadowOpaque.empty()||!scene.shadowTranslucent.empty()){
        if(!renderShadowScene(scene.shadowOpaque,scene.shadowTranslucent,error))return finish(false);
    }
    auto& depth=depthFrame.images[0];
    auto pixels=std::uint64_t(scene.width)*scene.height;
    std::uint64_t oldBytes=0,bytes=pixels*12; // complete, pre-hand, pre-translucent
    for(unsigned i=0;i<3;++i)oldBytes+=depthFrame.images[i].bytes;
    for(unsigned i=0;i<kBuffers;++i){
        oldBytes+=depthFrame.colors[i].bytes;
        if(used[i])bytes+=pixels*formats[i].bytes;
    }
    if(colorTargetBytes+packGpuBytes+depthBytes()-oldBytes+bytes>kMemoryLimit){
        error="Java world geometry targets exceed GPU memory budget";return finish(false);
    }
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    if(!depth.tex||!depth.fbo||depth.width!=scene.width||depth.height!=scene.height||
       depth.format!=GL_DEPTH_COMPONENT24){
        releaseDepth(depth);
        glGenTextures(1,&depth.tex);glBindTexture(GL_TEXTURE_2D,depth.tex);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_DEPTH_COMPONENT24,scene.width,scene.height);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
        glGenFramebuffers(1,&depth.fbo);glBindFramebuffer(GL_FRAMEBUFFER,depth.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth.tex,0);
        const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
        if(!depth.tex||!depth.fbo||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
            glGetError()!=GL_NO_ERROR){
            releaseDepth(depth);error="Cannot allocate main geometry depth";return finish(false);
        }
        depth.width=scene.width;depth.height=scene.height;depth.format=GL_DEPTH_COMPONENT24;depth.bytes=pixels*4;
    }
    glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);glDepthRangef(0,1);
    const GLfloat farDepth=1;
    glBindFramebuffer(GL_FRAMEBUFFER,depth.fbo);glClearBufferfv(GL_DEPTH,0,&farDepth);
    const GLenum first=GL_COLOR_ATTACHMENT0;
    for(unsigned i=0;i<kBuffers;++i){
        auto& d=depthFrame.colors[i];
        if(!used[i]){releaseColor(d);continue;}
        if(!d.tex||!d.fbo||d.width!=scene.width||d.height!=scene.height||d.format.internal!=formats[i].internal){
            releaseColor(d);d.format=formats[i];
            if(!allocateTexture(d.tex,scene.width,scene.height,d.format)){
                releaseColor(d);error="Cannot allocate geometry colortex"+std::to_string(i);return finish(false);
            }
            glGenFramebuffers(1,&d.fbo);glBindFramebuffer(GL_FRAMEBUFFER,d.fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER,first,GL_TEXTURE_2D,d.tex,0);
            glDrawBuffers(1,&first);glReadBuffer(first);
            if(!d.fbo||glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||
                glGetError()!=GL_NO_ERROR){
                releaseColor(d);error="Incomplete geometry color target";return finish(false);
            }
            d.width=scene.width;d.height=scene.height;d.bytes=pixels*d.format.bytes;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,d.fbo);
        const GLfloat white[4]={1,1,1,1},black[4]={0,0,0,0};
        glClearBufferfv(GL_COLOR,0,i==0?scene.fogClear.data():i==1?white:black);
    }
    glGenFramebuffers(1,&work);glGenFramebuffers(1,&read);
    if(!work||!read||glGetError()!=GL_NO_ERROR){error="Cannot initialize world geometry FBOs";return finish(false);}
    glBindFramebuffer(GL_FRAMEBUFFER,work);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,depth.tex,0);
    GLint maxColor=0,maxDraw=0;
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&maxColor);glGetIntegerv(GL_MAX_DRAW_BUFFERS,&maxDraw);
    unsigned slots=unsigned(std::min({maxColor,maxDraw,int(kBuffers)}));
    auto drawStage=[&](std::vector<WorldBatch> const& draws,bool updateSerial){
        for(auto const& batch:draws){
            auto draw=batch.geometry;GeometryHandle g;
            if(!acquireGeometryProgram(draw.program,g,error))return false;
            if(g.targets.size()>slots){error="Geometry outputs exceed driver limits";return false;}
            glBindFramebuffer(GL_FRAMEBUFFER,work);
            for(unsigned i=0;i<slots;++i){
                GLuint tex=i<g.targets.size()?depthFrame.colors[g.targets[i]].tex:0;
                glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,tex,0);
            }
            glReadBuffer(first);
            if(batch.blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
            if(batch.depthTest)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            if(batch.cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
            glDepthMask(batch.depthWrite?GL_TRUE:GL_FALSE);
            glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
            glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            draw.framebuffer=work;draw.width=scene.width;draw.height=scene.height;
            if(!drawGeometry(draw,error))return false;
            if(updateSerial)for(auto i:g.targets)depthFrame.colors[i].serial=++snapshotSerial;
        }
        return true;
    };
    if(!drawStage(scene.opaque,false))return finish(false);
    // Iris 1.21: depthtex2 excludes hand AND translucent geometry.
    if(!captureSceneDepth(DepthPhase::BeforeHand,depth.fbo,GL_DEPTH_COMPONENT24,scene.width,scene.height,error))
        return finish(false);
    if(!drawStage(scene.hand,false))return finish(false);
    if(!captureSceneDepth(DepthPhase::BeforeTranslucent,depth.fbo,GL_DEPTH_COMPONENT24,scene.width,scene.height,error))
        return finish(false);
    for(unsigned i=0;i<kBuffers;++i)if(used[i]){
        auto& d=depthFrame.colors[i];d.fresh=true;d.serial=++snapshotSerial;
    }
    // depthtex0 is the currently rendered main depth texture; at the
    // opaque boundary it contains the same geometry as depthtex1.
    depth.fresh=true;
    if(hasDeferred()){
        if(!renderDeferred(scene.width,scene.height,error))return finish(false);
        // Preserve deferred output as the destination for later geometry.
        // The separate deferred texture remains the sampled source.
        for(unsigned i=0;i<kBuffers;++i)if(used[i]&&deferredOutputs[i]){
            GLuint src=deferredTexture(i);
            if(!src){error="Missing deferred output for geometry";return finish(false);}
            glBindFramebuffer(GL_READ_FRAMEBUFFER,read);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER,first,GL_TEXTURE_2D,src,0);glReadBuffer(first);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,depthFrame.colors[i].fbo);
            if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
                error="Incomplete deferred output source";return finish(false);
            }
            glBlitFramebuffer(0,0,scene.width,scene.height,0,0,scene.width,scene.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            if(glGetError()!=GL_NO_ERROR){error="Cannot seed translucent geometry from deferred output";return finish(false);}
            depthFrame.colors[i].serial=++snapshotSerial;
        }
    }
    if(!drawStage(scene.translucent,true))return finish(false);
    // The same owned depth attachment now includes translucent geometry.
    depth.fresh=true;
    return finish(true);
}

bool renderDeferred(int width,int height,std::string& error){
    if(eglGetCurrentContext()==EGL_NO_CONTEXT||eglGetCurrentContext()!=context||
        eglGetCurrentSurface(EGL_DRAW)!=pipelineSurface||!requestedActive.load()){
        error="Deferred graph is not prepared for this EGL surface";return false;
    }
    if(!beginJavaFrame(width,height,error))return false;
    refreshCustomUniforms();
    // Preserve native GL bindings. No deferred result is presented here.
    SavedState saved;
    bool ok=renderOpaqueStage(width,height,error);
    saved.restore();
    return ok;
}
unsigned deferredTexture(unsigned colortex){
    if(colortex>=kBuffers||!deferredReady||!deferredOutputs[colortex]||
       eglGetCurrentContext()!=context||eglGetCurrentSurface(EGL_DRAW)!=pipelineSurface)return 0;
    return textures[colortex][front[colortex]];
}
void beforeSwap(int width,int height) {
    EGLContext now=eglGetCurrentContext();
    if(now==EGL_NO_CONTEXT)return;
    auto surface=eglGetCurrentSurface(EGL_DRAW);
    syncDepthContext();
    ConsumeDepthFrame consumeDepth;
    struct EndDeferredFrame {
        ~EndDeferredFrame(){deferredReady=false;worldSceneRendered=false;frameClockReady=false;frameViewWidth=frameViewHeight=0;}
    } endDeferredFrame;
    if(context!=now||pipelineSurface!=surface){
        if(context!=now)abandonContext();else{discardGeometry();discardGpu();}
        pipelineSurface=surface;
        if(!selectedSource.empty()){
            waitingForResources=true;attemptedResources=false;
            statusMessage(false,"Graphics surface changed; waiting for current-frame resources");
        }
    }
    int action=0;
    std::vector<Pass> passes;
    std::string label;
    {
        std::lock_guard lock(mu);
        action=std::exchange(pendingAction,0);
        if(action==1){passes=std::move(pendingPasses);label=std::move(pendingLabel);}
    }
    if(action==2){
        {std::lock_guard lock(mu);renderMetrics={};}
        discardGeometry();discardGpu();discardPackResources();
        for(auto& d:depthFrame.images)releaseDepth(d);
        for(auto& d:depthFrame.colors)releaseColor(d);
        selectedSource.clear();selectedLabel.clear();lastResourceSignature.clear();
        waitingForResources=false;attemptedResources=false;
        return;
    }
    if(action==1){
        {std::lock_guard lock(mu);renderMetrics={};}
        discardGeometry();discardGpu();discardPackResources();
        depthFrame.previousCamera.clear();deriveCamera();
        depthFrame.smoothPrevious.clear();depthFrame.previousTime={};depthFrame.frameTime={};
        selectedSource=std::move(passes);selectedLabel=std::move(label);
        waitingForResources=true;attemptedResources=false;
    }
    if(!selectedSource.empty()){
        std::string error;
        if(!beginJavaFrame(width,height,error)){
            statusMessage(false,error+"; vanilla preserved");return;
        }
    }
    refreshCustomUniforms();
    if(!selectedSource.empty()&&!packResourcesReady){
        std::string error;
        if(!preparePackResources(error)){
            selectedSource.clear();waitingForResources=false;wantsNativeCapture=false;
            statusMessage(false,"Java pack resource initialization failed: "+error);
            return;
        }
    }
    if(waitingForResources&&!selectedSource.empty()){
        auto signature=resourceSignature();
        if(!attemptedResources||signature!=lastResourceSignature){
            attemptedResources=true;lastResourceSignature=std::move(signature);
            SavedState saved;
            discardGpu();
            std::string error;
            bool ok=compilePipeline(selectedSource,error);
            saved.restore();
            if(!ok){
                discardGpu();
                bool transient=error.find("Missing real input ")!=std::string::npos&&
                    error.find("(unsupported type)")==std::string::npos&&
                    error.find("(uniform block unsupported)")==std::string::npos&&
                    error.find("(world uniform type mismatch)")==std::string::npos;
                if(transient){
                    statusMessage(false,"Waiting for verified world resources: "+error);
                }else{
                    discardGeometry();discardPackResources();
                    selectedSource.clear();waitingForResources=false;wantsNativeCapture=false;
                    statusMessage(false,"Java pipeline rejected: "+error);
                }
                return;
            }
            waitingForResources=false;
            activeLabel=selectedLabel;
            requestedActive=true;
            renderedFrame=false;
            statusMessage(true,"Compiled Java composite graph; awaiting rendered frame");
        }
    }
    if(requestedActive.load()&&!pipeline.empty()){
        if(hasDeferred()&&!deferredReady){
            {std::lock_guard lock(mu);++renderMetrics.waitingFrames;}
            renderedFrame=false;
            statusMessage(false,"Waiting for verified opaque deferred phase; vanilla preserved");
            return;
        }
        SavedState saved;
        std::string error;
        auto started=std::chrono::steady_clock::now();
        bool ok=renderPipeline(width,height,error);
        auto elapsed=std::chrono::duration<float,std::milli>(
            std::chrono::steady_clock::now()-started).count();
        {
            std::lock_guard lock(mu);
            renderMetrics.width=width;renderMetrics.height=height;
            if(ok){
                ++renderMetrics.presentedFrames;
                renderMetrics.lastCpuMs=elapsed;
                if(renderMetrics.presentedFrames==1)renderMetrics.averageCpuMs=elapsed;
                else renderMetrics.averageCpuMs=
                    renderMetrics.averageCpuMs*0.9f+elapsed*0.1f;
            }else ++renderMetrics.failedFrames;
        }
        consumeDepth.rendered=ok;
        saved.restore();
        if(!ok){
            bool staged=hasDeferred();
            bool transient=error.rfind("Missing current-frame ",0)==0||
                error.rfind("Missing or mismatched current-frame ",0)==0||
                error.rfind("Missing or mismatched native scene ",0)==0;
            if(transient&&staged&&!selectedSource.empty()){
                // Keep linked programs ready for the next opaque phase.
                // Discard potentially partially written textures/history.
                for(auto& pair:textures){glDeleteTextures(2,pair.data());pair={};}
                if(outputTex){glDeleteTextures(1,&outputTex);outputTex=0;}
                textureWidth=textureHeight=0;colorTargetBytes=0;
                front={};mipValid={};frameCounter=0;renderedFrame=false;
                statusMessage(false,"Waiting for verified world resources: "+error+"; vanilla restored");
            }else if(transient&&!selectedSource.empty()){
                discardGpu();
                waitingForResources=true;attemptedResources=true;
                lastResourceSignature=resourceSignature();
                // A staged graph compiles before its live input set is complete.
                // Rebuild it on the next swap even when that set is unchanged,
                // so the next opaque callback can run instead of deadlocking.
                if(std::any_of(selectedSource.begin(),selectedSource.end(),[](auto const& p){return p.deferred;}))
                    attemptedResources=false;
                statusMessage(false,"Waiting for verified world resources: "+error+"; vanilla restored");
            }else{
                discardGpu();discardGeometry();discardPackResources();
                selectedSource.clear();waitingForResources=false;wantsNativeCapture=false;
                statusMessage(false,error+"; vanilla restored");
            }
        }else if(!renderedFrame){
            renderedFrame=true;
            statusMessage(true,"Java composite world graph rendered: "+activeLabel+
                         " ("+std::to_string(pipeline.size())+" passes)");
        }
    }
}
void stop(){requestDisable();}
} // namespace modmenu::android::shaderworld
