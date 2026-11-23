#include "ShaderWorldRuntime.h"
#include "core/Log.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace modmenu::android::shaderworld {
namespace {
std::mutex mu;
std::string pendingCode;
std::string pendingLabel;
std::string lastStatus="No world shader active";
int pendingAction=0; // 0 none, 1 enable, 2 disable
std::atomic<bool> requestedActive{false};
bool failed=false;

// GPU state is touched exclusively from the selected game's EGL swap thread.
EGLContext context=EGL_NO_CONTEXT;
GLuint program=0,sourceTex=0,triangleVao=0;
int textureWidth=0,textureHeight=0;

void statusMessage(bool ok,std::string message) {
    std::lock_guard lock(mu);
    failed=!ok;
    lastStatus=std::move(message);
}

void abandonContext() {
    // GL names from a destroyed EGL context cannot safely be deleted here.
    context=eglGetCurrentContext();
    program=sourceTex=triangleVao=0;
    textureWidth=textureHeight=0;
    requestedActive=false;
}

void discardGpu() {
    if(program){glDeleteProgram(program);program=0;}
    if(sourceTex){glDeleteTextures(1,&sourceTex);sourceTex=0;}
    if(triangleVao){glDeleteVertexArrays(1,&triangleVao);triangleVao=0;}
    textureWidth=textureHeight=0;
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

bool compileProgram(std::string const& fragment,std::string& error) {
    static char const* vertex=R"(#version 300 es
precision highp float;
out vec2 texcoord;
out vec2 vTexCoord;
void main() {
    vec2 uv=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    texcoord=uv;
    vTexCoord=uv;
    gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
}
)";
    GLuint vs=compile(GL_VERTEX_SHADER,vertex,error);
    if(!vs)return false;
    GLuint fs=compile(GL_FRAGMENT_SHADER,fragment,error);
    if(!fs){glDeleteShader(vs);return false;}
    GLuint linked=glCreateProgram();
    glAttachShader(linked,vs);
    glAttachShader(linked,fs);
    glLinkProgram(linked);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok=0;glGetProgramiv(linked,GL_LINK_STATUS,&ok);
    if(!ok) {
        error=shaderLog(linked,true);
        glDeleteProgram(linked);
        return false;
    }

    // Reject complex Iris programs requiring extra samplers, shadow maps
    // or scene uniforms not provided by this single-pass adapter.
    static char const* allowed[]={
        "colortex0","gcolor","texture","tex",
        "viewWidth","viewHeight","frameTimeCounter"
    };
    GLint count=0;
    glGetProgramiv(linked,GL_ACTIVE_UNIFORMS,&count);
    for(GLint i=0;i<count;i++) {
        GLchar name[256]{};
        GLsizei len=0;GLint arraySize=0;GLenum type=0;
        glGetActiveUniform(linked,GLuint(i),sizeof(name),&len,&arraySize,&type,name);
        std::string uniform(name,size_t(std::max(0,int(len))));
        bool known=false;
        for(char const* test:allowed)if(uniform==test){known=true;break;}
        if(!known){
            error="Unsupported world shader uniform: "+uniform+
                  ". Use the offscreen test for complex Iris packs.";
            glDeleteProgram(linked);
            return false;
        }
    }
    program=linked;
    glGenVertexArrays(1,&triangleVao);
    return true;
}

struct SavedState {
    GLint program=0,vao=0,readFbo=0,drawFbo=0;
    GLint activeTex=GL_TEXTURE0,tex0=0,readBuffer=GL_BACK;
    GLint viewport[4]{};
    GLint unpackPbo=0,unpackAlign=4;
    GLboolean blend=false,depth=false,scissor=false,stencil=false,cull=false;
    GLboolean depthMask=GL_TRUE,colorMask[4]{};
    SavedState() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&drawFbo);
        glGetIntegerv(GL_READ_BUFFER,&readBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex0);
        glGetIntegerv(GL_VIEWPORT,viewport);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpackPbo);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpackAlign);
        blend=glIsEnabled(GL_BLEND);
        depth=glIsEnabled(GL_DEPTH_TEST);
        scissor=glIsEnabled(GL_SCISSOR_TEST);
        stencil=glIsEnabled(GL_STENCIL_TEST);
        cull=glIsEnabled(GL_CULL_FACE);
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
        glDepthMask(depthMask);
        glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,GLuint(unpackPbo));
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpackAlign);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,GLuint(tex0));
        glActiveTexture(GLenum(activeTex));
    }
};

bool ensureInputTexture(int width,int height) {
    if(width==textureWidth&&height==textureHeight&&sourceTex)
        return true;
    if(sourceTex){glDeleteTextures(1,&sourceTex);sourceTex=0;}
    if(width<=0||height<=0||width>4096||height>4096)return false;
    glGenTextures(1,&sourceTex);
    if(!sourceTex)return false;
    glBindTexture(GL_TEXTURE_2D,sourceTex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,
                 GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    textureWidth=width; textureHeight=height;
    return glGetError()==GL_NO_ERROR;
}

void applyOneFrame(int width,int height) {
    SavedState saved;
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glReadBuffer(GL_BACK);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if(!ensureInputTexture(width,height)){
        saved.restore();
        discardGpu();
        statusMessage(false,"World shader cannot allocate the game frame texture");
        return;
    }

    // Capture the already-rendered game backbuffer, then run the Java-style
    // final/composite fragment over it. This is NOT Iris deferred rendering.
    glBindTexture(GL_TEXTURE_2D,sourceTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
    GLenum copyError=glGetError();
    if(copyError!=GL_NO_ERROR){
        saved.restore();
        discardGpu();
        statusMessage(false,"Cannot capture RenderDragon frame with OpenGL ES");
        return;
    }

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,width,height);
    glUseProgram(program);
    glBindVertexArray(triangleVao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,sourceTex);
    for(char const* name:{"colortex0","gcolor","texture","tex"}) {
        GLint loc=glGetUniformLocation(program,name);
        if(loc>=0)glUniform1i(loc,0);
    }
    GLint ww=glGetUniformLocation(program,"viewWidth");
    GLint hh=glGetUniformLocation(program,"viewHeight");
    if(ww>=0)glUniform1f(ww,float(width));
    if(hh>=0)glUniform1f(hh,float(height));
    glDrawArrays(GL_TRIANGLES,0,3);
    GLenum renderError=glGetError();
    saved.restore();
    if(renderError!=GL_NO_ERROR){
        discardGpu();
        statusMessage(false,"GLES3 world postprocessing failed; disabled");
    }
}

} // namespace

void requestEnable(std::string fragment,std::string label) {
    if(fragment.size()>256*1024)return;
    std::lock_guard lock(mu);
    pendingCode=std::move(fragment);
    pendingLabel=std::move(label);
    pendingAction=1;
    lastStatus="Compiling Java final/composite shader for world postprocess";
    failed=false;
}

void requestDisable() {
    requestedActive=false; // no shader on next frame, even before GL cleanup
    std::lock_guard lock(mu);
    pendingAction=2;
    pendingCode.clear();
    lastStatus="World postprocess disabled";
    failed=false;
}

Status status() {
    std::lock_guard lock(mu);
    return {requestedActive.load(),failed,lastStatus};
}

void beforeSwap(int width,int height) {
    if(eglGetCurrentContext()==EGL_NO_CONTEXT)return;
    if(context!=eglGetCurrentContext()) {
        abandonContext();
        statusMessage(false,"Graphics context changed. Apply shader again.");
    }
    int action=0;
    std::string source,label;
    {
        std::lock_guard lock(mu);
        action=std::exchange(pendingAction,0);
        if(action==1) {
            source=std::move(pendingCode);
            label=std::move(pendingLabel);
        }
    }
    if(action==2) {
        discardGpu();
        return;
    }
    if(action==1) {
        SavedState saved;
        discardGpu();
        std::string error;
        bool ok=compileProgram(source,error);
        saved.restore();
        if(ok) {
            requestedActive=true;
            statusMessage(true,"Experimental GLES3 world pass active: "+label);
        } else {
            discardGpu();
            statusMessage(false,"Java world pass rejected: "+error);
        }
    }
    if(requestedActive.load()&&program)
        applyOneFrame(width,height);
}

void stop(){requestDisable();}
} // namespace modmenu::android::shaderworld
