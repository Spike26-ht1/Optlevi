#include "ShaderWorldRuntime.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <atomic>
#include <chrono>
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
bool failed=false;

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
struct Binding { GLint location; int buffer; int packTexture; unsigned unit; };
struct GpuPackTexture { std::string sampler; GLuint texture=0; };
std::vector<GpuPackTexture> packGpu;
std::uint64_t packGpuBytes=0;
unsigned frameCounter=0;
struct CompiledPass {
    GLuint program=0;
    std::string name;
    std::vector<unsigned> targets;
    std::vector<Binding> samplers;
    bool finalPass=false;
    bool customVertex=false;
    std::uint32_t mipmapInputs=0;
};
std::vector<CompiledPass> pipeline;
std::array<std::array<GLuint,2>,kBuffers> textures{};
std::array<unsigned,kBuffers> front{};
std::array<bool,kBuffers> needed{},mipNeeded{},historyNeeded{};
std::array<std::array<bool,2>,kBuffers> mipValid{};
GLuint triangleVao=0,passFbo=0,outputTex=0;
int textureWidth=0,textureHeight=0;
std::string activeLabel;
bool renderedFrame=false;

void statusMessage(bool ok,std::string message) {
    std::lock_guard lock(mu);
    failed=!ok;
    lastStatus=std::move(message);
}

void abandonContext() {
    // Names from the previous EGL context are invalid in this one.
    context=eglGetCurrentContext();
    pipeline.clear();
    packGpu.clear();packGpuBytes=0;frameCounter=0;
    textures={};front={};needed={};mipNeeded={};historyNeeded={};mipValid={};colorFormats={};triangleVao=passFbo=outputTex=0;
    textureWidth=textureHeight=0;
    renderedFrame=false;
    requestedActive=false;
}

void discardGpu() {
    for(auto& p:pipeline) if(p.program)glDeleteProgram(p.program);
    pipeline.clear();
    for(auto& p:packGpu)if(p.texture)glDeleteTextures(1,&p.texture);
    packGpu.clear();packGpuBytes=0;frameCounter=0;
    for(auto& pair:textures) glDeleteTextures(2,pair.data());
    textures={};front={};needed={};mipNeeded={};historyNeeded={};mipValid={};colorFormats={};
    if(outputTex){glDeleteTextures(1,&outputTex);outputTex=0;}
    if(passFbo){glDeleteFramebuffers(1,&passFbo);passFbo=0;}
    if(triangleVao){glDeleteVertexArrays(1,&triangleVao);triangleVao=0;}
    textureWidth=textureHeight=0;
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
                    CompiledPass& result,std::string& error) {
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
        if(type==GL_SAMPLER_2D&&(index>=0||packIndex>=0)&&arraySize==1){
            if(packIndex<0&&!available[unsigned(index)])
                return fail("Missing real input "+uniform+" in "+pass.name+
                  " (requires a RenderDragon scene/geometry pass)");
            if(result.samplers.size()>=kBuffers)
                return fail("Too many active texture samplers in "+pass.name);
            result.samplers.push_back({glGetUniformLocation(linked,name),index,packIndex,
                                       unsigned(result.samplers.size())});
        }else if(type==GL_INT&&arraySize==1&&uniform=="frameCounter"){
            // Actual rendered frame counter, not an invented world tick.
        }else if(type==GL_FLOAT&&arraySize==1&&
                 (uniform=="viewWidth"||uniform=="viewHeight"||
                  uniform=="aspectRatio"||uniform=="frameTimeCounter"||
                  uniform=="frameTime")){
            // Actual viewport values and monotonic frame timing are provided.
        }else{
            return fail("Unsupported world shader uniform: "+uniform+" in "+pass.name);
        }
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
    if(!uploadPackTextures(passes.front().packTextures,error))return false;
    for(auto const& entry:passes.front().colorFormats){
        if(entry.first>=kBuffers){error="Invalid colortex format index";return false;}
        colorFormats[entry.first]=resolveColorFormat(entry.second);
    }
    // Collect metadata from all active programs before validating graph
    // dependencies: temporal resources can be read before their first writer.
    struct Metadata { bool enabled=true; std::uint32_t mip=0,history=0; };
    std::vector<Metadata> metadata(passes.size());
    std::array<bool,kBuffers> available{},produced{};
    available[0]=true;needed[0]=true;
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
    for(size_t i=0;i<passes.size();++i) {
        auto const& pass=passes[i];
        auto const& m=metadata[i];
        std::uint32_t mipmapInputs=m.mip;
        if(!m.enabled){
            if(pass.finalPass){error="Final pass cannot be disabled";return false;}
            continue;
        }
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
        if(!compileProgram(pass,available,p,error)){
            error=pass.name+": "+error;return false;
        }
        if(p.targets.empty()||p.targets.size()>size_t(std::min(maxDraw,maxColor))||
           (p.finalPass&&p.targets.size()!=1)){
            glDeleteProgram(p.program);
            error="Active output mapping exceeds driver limits in "+pass.name;return false;
        }
        p.mipmapInputs=mipmapInputs;
        for(auto const& binding:p.samplers)
            if(binding.packTexture<0&&(mipmapInputs&(std::uint32_t(1)<<binding.buffer)))
                mipNeeded[unsigned(binding.buffer)]=true;
        if(!p.finalPass)for(auto t:p.targets){available[t]=true;needed[t]=true;produced[t]=true;}
        pipeline.push_back(std::move(p));
    }
    for(unsigned i=0;i<kBuffers;++i)if(historyNeeded[i]&&!produced[i]){
        error="Persistent colortex"+std::to_string(i)+" has no active producer";return false;
    }
    for(unsigned i=0;i<kBuffers;++i)if(needed[i]&&!colorFormats[i].bytes){
        error="Unsupported color attachment format for colortex"+std::to_string(i);return false;
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

bool ensureTargets(int width,int height,std::string& error) {
    if(width==textureWidth&&height==textureHeight&&outputTex)return true;
    for(auto& pair:textures){glDeleteTextures(2,pair.data());pair={};}
    if(outputTex){glDeleteTextures(1,&outputTex);outputTex=0;}
    textureWidth=textureHeight=0;mipValid={};front={};frameCounter=0;
    if(width<=0||height<=0||width>4096||height>4096){error="Unsupported viewport size";return false;}
    std::uint64_t basePixels=std::uint64_t(width)*height,chainPixels=basePixels;
    for(int w=width,h=height;w>1||h>1;){
        w=std::max(1,w/2);h=std::max(1,h/2);
        chainPixels+=std::uint64_t(w)*h;
    }
    std::uint64_t bytes=basePixels*4+packGpuBytes;
    for(unsigned i=0;i<kBuffers;++i)if(needed[i])
        bytes+=2*(mipNeeded[i]?chainPixels:basePixels)*colorFormats[i].bytes;
    if(bytes>kMemoryLimit){error="Cannot allocate independent GLES3 targets within 128 MiB budget";return false;}
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=0;i<kBuffers;++i)if(needed[i]){
        for(auto& t:textures[i])if(!allocateTexture(t,width,height,colorFormats[i])){
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
    textureWidth=width;textureHeight=height;
    return true;
}

void setUniforms(CompiledPass const& pass,int width,int height,float time,float dt) {
    glUseProgram(pass.program);
    for(auto const& b:pass.samplers){
        unsigned unit=b.unit;
        glActiveTexture(GL_TEXTURE0+unit);
        glBindSampler(unit,0);
        GLuint texture=b.packTexture>=0?packGpu[unsigned(b.packTexture)].texture:
                      textures[unsigned(b.buffer)][front[unsigned(b.buffer)]];
        glBindTexture(GL_TEXTURE_2D,texture);
        if(b.packTexture<0){
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
    GLint frameLoc=glGetUniformLocation(pass.program,"frameCounter");
    if(frameLoc>=0)glUniform1i(frameLoc,int(frameCounter));
}

bool renderPipeline(int width,int height,std::string& error) {
    // Preserve the entire frame until EVERY pass has rendered successfully.
    // Never render a rejected/partially compiled shader directly to the game.
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glReadBuffer(GL_BACK);
    if(!ensureTargets(width,height,error))return false;
    for(unsigned i=0;i<kBuffers;++i)if(!historyNeeded[i])front[i]=0;
    mipValid[0][0]=false;
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0,0);
    glBindTexture(GL_TEXTURE_2D,textures[0][0]);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
    if(glGetError()!=GL_NO_ERROR){error="Cannot capture actual Bedrock backbuffer";return false;}
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glDisable(GL_RASTERIZER_DISCARD);
    glDepthMask(GL_FALSE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,width,height);glBindVertexArray(triangleVao);
    glBindFramebuffer(GL_FRAMEBUFFER,passFbo);
    static const auto started=std::chrono::steady_clock::now();
    static auto last=started;
    auto now=std::chrono::steady_clock::now();
    float time=std::chrono::duration<float>(now-started).count();
    float dt=std::chrono::duration<float>(now-last).count();last=now;
    for(auto const& pass:pipeline){
        // Attach write-side texture(s) distinct from every sampled read-side.
        // MRT locations index the directive list, not the colortex number.
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
        setUniforms(pass,width,height,time,dt);
        if(pass.customVertex)glDrawArrays(GL_TRIANGLES,0,6);
        else glDrawArrays(GL_TRIANGLES,0,3);
        if(glGetError()!=GL_NO_ERROR){error=pass.name+": GLES3 pass execution failed";return false;}
        if(!pass.finalPass)for(auto t:pass.targets){front[t]=1-front[t];mipValid[t][front[t]]=false;}
    }
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
        if(i!=0&&(!p.packTextures.empty()||!p.colorFormats.empty()))badImages=true;
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
    if(passes.empty()||passes.size()>kMaxPasses||total>4*1024*1024||badImages){
        pendingAction=2;requestedActive=false;
        pendingPasses.clear();
        failed=true;lastStatus="Java composite graph exceeds pass/source budget";
        return;
    }
    pendingPasses=std::move(passes);
    pendingLabel=std::move(label);
    pendingAction=1;lastStatus="Compiling ordered Java composite graph";
    failed=false;
}

void requestEnable(std::string fragment,std::string label) {
    requestPipeline({Pass{"final",std::move(fragment),{0},true}},std::move(label));
}

void requestDisable() {
    requestedActive=false;
    std::lock_guard lock(mu);
    pendingAction=2;pendingPasses.clear();
    lastStatus="World postprocess disabled";failed=false;
}

Status status() {
    std::lock_guard lock(mu);
    return {requestedActive.load(),failed,lastStatus};
}

void beforeSwap(int width,int height) {
    EGLContext now=eglGetCurrentContext();
    if(now==EGL_NO_CONTEXT)return;
    if(context!=now) {
        abandonContext();
        statusMessage(false,"Graphics context changed. Apply shader again.");
    }
    int action=0;
    std::vector<Pass> passes;
    std::string label;
    {
        std::lock_guard lock(mu);
        action=std::exchange(pendingAction,0);
        if(action==1){passes=std::move(pendingPasses);label=std::move(pendingLabel);}
    }
    if(action==2){discardGpu();return;}
    if(action==1){
        SavedState saved;
        discardGpu();
        std::string error;
        bool ok=compilePipeline(passes,error);
        saved.restore();
        if(!ok){
            discardGpu();statusMessage(false,"Java pipeline rejected: "+error);return;
        }
        activeLabel=std::move(label);
        requestedActive=true;
        renderedFrame=false;
        statusMessage(true,"Compiled Java composite graph; awaiting rendered frame");
    }
    if(requestedActive.load()&&!pipeline.empty()){
        SavedState saved;
        std::string error;
        bool ok=renderPipeline(width,height,error);
        saved.restore();
        if(!ok){discardGpu();statusMessage(false,error+"; vanilla restored");}
        else if(!renderedFrame){
            renderedFrame=true;
            statusMessage(true,"Java composite world graph rendered: "+activeLabel+
                         " ("+std::to_string(pipeline.size())+" passes)");
        }
    }
}
void stop(){requestDisable();}
} // namespace modmenu::android::shaderworld
