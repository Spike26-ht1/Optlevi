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
constexpr unsigned kBuffers=8;
constexpr unsigned kMaxPasses=24;
constexpr std::uint64_t kMemoryLimit=128ull*1024*1024;
struct Binding { GLint location; int buffer; };
struct CompiledPass {
    GLuint program=0;
    std::string name;
    std::vector<unsigned> targets;
    std::vector<Binding> samplers;
    bool finalPass=false;
};
std::vector<CompiledPass> pipeline;
std::array<std::array<GLuint,2>,kBuffers> textures{};
std::array<unsigned,kBuffers> front{};
std::array<bool,kBuffers> needed{};
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
    textures={};front={};needed={};triangleVao=passFbo=outputTex=0;
    textureWidth=textureHeight=0;
    renderedFrame=false;
    requestedActive=false;
}

void discardGpu() {
    for(auto& p:pipeline) if(p.program)glDeleteProgram(p.program);
    pipeline.clear();
    for(auto& pair:textures) glDeleteTextures(2,pair.data());
    textures={};front={};needed={};
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

int bufferIndex(std::string const& name) {
    if(name=="gcolor"||name=="texture"||name=="tex")return 0;
    if(name.size()==9&&name.compare(0,8,"colortex")==0&&
       name[8]>='0'&&name[8]<'0'+int(kBuffers))return name[8]-'0';
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

    GLuint vs=compile(GL_VERTEX_SHADER,vertex,error);
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
    GLint count=0;
    glGetProgramiv(linked,GL_ACTIVE_UNIFORMS,&count);
    for(GLint i=0;i<count;i++) {
        GLchar name[256]{};
        GLsizei len=0;GLint arraySize=0;GLenum type=0;
        glGetActiveUniform(linked,GLuint(i),sizeof(name),&len,&arraySize,&type,name);
        std::string uniform(name,size_t(std::max(0,int(len))));
        int index=bufferIndex(uniform);
        if(type==GL_SAMPLER_2D&&index>=0&&arraySize==1){
            if(!available[unsigned(index)])
                return fail("Missing real input "+uniform+" in "+pass.name+
                  " (requires a RenderDragon scene/geometry pass)");
            result.samplers.push_back({glGetUniformLocation(linked,name),index});
        }else if(type==GL_FLOAT&&arraySize==1&&
                 (uniform=="viewWidth"||uniform=="viewHeight"||
                  uniform=="aspectRatio"||uniform=="frameTimeCounter"||
                  uniform=="frameTime")){
            // Actual viewport values and monotonic frame timing are provided.
        }else{
            return fail("Unsupported world shader uniform: "+uniform+" in "+pass.name);
        }
    }
    result.program=linked;
    result.name=pass.name;
    result.targets=pass.targets;
    result.finalPass=pass.finalPass;
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
    std::array<bool,kBuffers> available{};
    available[0]=true;needed[0]=true;
    for(size_t i=0;i<passes.size();++i) {
        auto const& pass=passes[i];
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
        pipeline.push_back(std::move(p));
        if(!pass.finalPass)for(auto t:pass.targets){available[t]=true;needed[t]=true;}
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
    GLint unpackPbo=0,unpackAlign=4;
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
        for(unsigned i=0;i<kBuffers;++i){
            glActiveTexture(GL_TEXTURE0+i);
            glBindTexture(GL_TEXTURE_2D,GLuint(tex[i]));
            glBindSampler(i,GLuint(sampler[i]));
        }
        glActiveTexture(GLenum(activeTex));
    }
};

bool allocateTexture(GLuint& texture,int w,int h) {
    glGenTextures(1,&texture);
    if(!texture)return false;
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    return glGetError()==GL_NO_ERROR;
}

bool ensureTargets(int width,int height) {
    if(width==textureWidth&&height==textureHeight&&outputTex)return true;
    for(auto& pair:textures){glDeleteTextures(2,pair.data());pair={};}
    if(outputTex){glDeleteTextures(1,&outputTex);outputTex=0;}
    textureWidth=textureHeight=0;
    if(width<=0||height<=0||width>4096||height>4096)return false;
    unsigned n=1+2*unsigned(std::count(needed.begin(),needed.end(),true));
    if(std::uint64_t(width)*height*4*n>kMemoryLimit)return false;
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=0;i<kBuffers;++i)if(needed[i]){
        for(auto& t:textures[i])if(!allocateTexture(t,width,height))return false;
    }
    if(!allocateTexture(outputTex,width,height))return false;
    textureWidth=width;textureHeight=height;
    return true;
}

void setUniforms(CompiledPass const& pass,int width,int height,float time,float dt) {
    glUseProgram(pass.program);
    for(auto const& b:pass.samplers){
        unsigned unit=unsigned(b.buffer);
        glActiveTexture(GL_TEXTURE0+unit);
        glBindSampler(unit,0);
        glBindTexture(GL_TEXTURE_2D,textures[unit][front[unit]]);
        if(b.location>=0)glUniform1i(b.location,int(unit));
    }
    auto set=[&](char const* name,float value){
        GLint loc=glGetUniformLocation(pass.program,name);
        if(loc>=0)glUniform1f(loc,value);
    };
    set("viewWidth",float(width));set("viewHeight",float(height));
    set("aspectRatio",float(width)/float(height));set("frameTimeCounter",time);
    set("frameTime",dt);
}

bool renderPipeline(int width,int height,std::string& error) {
    // Preserve the entire frame until EVERY pass has rendered successfully.
    // Never render a rejected/partially compiled shader directly to the game.
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glReadBuffer(GL_BACK);
    if(!ensureTargets(width,height)){
        error="Cannot allocate independent GLES3 targets within 128 MiB budget";return false;
    }
    front.fill(0);
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
        glDrawArrays(GL_TRIANGLES,0,3);
        if(glGetError()!=GL_NO_ERROR){error=pass.name+": GLES3 pass execution failed";return false;}
        if(!pass.finalPass)for(auto t:pass.targets)front[t]=1-front[t];
    }
    // Only publish the output after the final pass succeeds.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,passFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    if(glGetError()!=GL_NO_ERROR){error="Cannot present Java composite output";return false;}
    return true;
}

} // namespace

void requestPipeline(std::vector<Pass> passes,std::string label) {
    size_t total=0;
    for(auto const& p:passes)total+=p.fragment.size();
    std::lock_guard lock(mu);
    if(passes.empty()||passes.size()>kMaxPasses||total>4*1024*1024){
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
