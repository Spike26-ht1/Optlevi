// Original synthetic fixture: exercise actual GLES3 rendering and readback,
// not just source-string checks. This does not run Minecraft or BSL.
#include "ShaderWorldRuntime.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <string>

using namespace modmenu::android::shaderworld;
static void require(bool ok,char const* message){
    if(!ok){std::cerr<<message<<" | runtime: "<<status().message<<"\n";std::exit(1);}
}
static std::string fragment(std::string body){
    return "#version 300 es\nprecision highp float;\nin vec2 texcoord;\n"+body;
}
static std::array<unsigned char,4> pixel(){
    std::array<unsigned char,4> rgba{};
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    require(glGetError()==GL_NO_ERROR,"Readback failed");
    return rgba;
}
static void clearScene(){
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,32,32);
    glClearColor(0.2f,0.4f,0.6f,1.0f);glClear(GL_COLOR_BUFFER_BIT);
}
int main(){
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major=0,minor=0;
    require(d!=EGL_NO_DISPLAY&&eglInitialize(d,&major,&minor),"EGL initialize");
    require(eglBindAPI(EGL_OPENGL_ES_API),"EGL ES API");
    const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config{};EGLint found=0;
    require(eglChooseConfig(d,attrs,&config,1,&found)&&found==1,"Choose GLES3 config");
    const EGLint surfAttrs[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(d,config,surfAttrs);
    const EGLint ctxAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext context=eglCreateContext(d,config,EGL_NO_CONTEXT,ctxAttrs);
    require(surface!=EGL_NO_SURFACE&&context!=EGL_NO_CONTEXT&&
       eglMakeCurrent(d,surface,surface,context),"Make GLES3 context current");
    std::cout<<"GLES renderer: "<<glGetString(GL_RENDERER)<<"\n";
    clearScene();
    // First composite writes separate colortex0 and colortex1.
    // Second composite samples both and rewrites colortex0 (ping-pong).
    // Final reads that output. Expected RGB=(0.8,0.4,0.6).
    Pass p0{"composite",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 a;
layout(location=1) out vec4 b;
void main(){
  vec4 c=texture(colortex0,texcoord);
  a=vec4(c.r,c.g,0.0,1.0);
  b=vec4(1.0-c.r,0.0,c.b,1.0);
}
)"),{0,1},false};
    Pass p1{"composite1",fragment(R"(
uniform sampler2D colortex0;
uniform sampler2D colortex1;
layout(location=0) out vec4 color;
void main(){vec4 a=texture(colortex0,texcoord);vec4 b=texture(colortex1,texcoord);
color=vec4(b.r,a.g,b.b,1.0);}
)"),{0},false};
    Pass end{"final",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=texture(colortex0,texcoord);}
)"),{0},true};
    requestPipeline({p0,p1,end},"Original two-target fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"Expected real rendered graph");
    auto actual=pixel();
    require(std::abs(int(actual[0])-204)<=2&&std::abs(int(actual[1])-102)<=2&&
            std::abs(int(actual[2])-153)<=2,"Unexpected multi-pass pixels");
    std::cout<<"Actual composite result: "<<int(actual[0])<<","<<int(actual[1])
             <<","<<int(actual[2])<<"\n";
    // Original vertex stage computes a nonstandard varying. The fragment
    // can only link and produce the expected output when that stage executes.
    clearScene();
    Pass custom{"final",R"(#version 300 es
precision highp float;
in vec2 customUV;
layout(location=0) out vec4 color;
uniform sampler2D colortex0;
void main(){color=vec4(customUV.x,texture(colortex0,customUV).g,0.25,1.0);}
)",{0},true,R"(#version 300 es
precision highp float;
out vec2 customUV;
void main(){
    const vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),
        vec2(0,0),vec2(1,1),vec2(0,1));
    vec2 p=corners[gl_VertexID];
    customUV=vec2(0.75,p.y);
    gl_Position=vec4(p*2.0-1.0,0.0,1.0);
}
)"};
    requestPipeline({custom},"Original custom vertex fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"Custom vertex stage failed");
    actual=pixel();
    require(std::abs(int(actual[0])-191)<=2&&std::abs(int(actual[1])-102)<=2&&
            std::abs(int(actual[2])-64)<=2,"Original vertex varying was not executed");
    // An active route can write a higher-index buffer. Subsequent passes
    // must validate and sample the actual driver-selected output, not route 0.
    clearScene();
    Pass routed{"composite",fragment(R"(
#define USE_ALT
#ifdef USE_ALT
layout(location=0) out vec4 optRouteTarget9;
void main(){optRouteTarget9=vec4(0.5,0.25,0.75,1.0);}
#else
layout(location=0) out vec4 optRouteTarget0;
void main(){optRouteTarget0=vec4(1.0);}
#endif
)"),{0},false,"",true};
    Pass routedEnd{"final",fragment(R"(
uniform sampler2D colortex9;
layout(location=0) out vec4 color;
void main(){color=texture(colortex9,texcoord);}
)"),{0},true};
    requestPipeline({routed,routedEnd},"Conditional route fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"Conditional target was not resolved");
    actual=pixel();
    require(std::abs(int(actual[0])-128)<=2&&std::abs(int(actual[1])-64)<=2&&
            std::abs(int(actual[2])-191)<=2,"Conditional target data incorrect");
    // Pack-owned noise image has a real GPU upload and its own sampler
    // unit. It must not consume or impersonate a scene color buffer.
    clearScene();
    Pass noise{"final",fragment(R"(
uniform sampler2D noisetex;
uniform int frameCounter;
layout(location=0) out vec4 color;
void main(){color=texture(noisetex,texcoord);color.a=float(frameCounter==0);}
)"),{0},true};
    noise.packTextures.push_back({"noisetex",1,1,true,{17,99,201,255}});
    requestPipeline({noise},"Pack PNG texture fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"Pack texture was not bound");
    actual=pixel();
    require(std::abs(int(actual[0])-17)<=2&&std::abs(int(actual[1])-99)<=2&&
            std::abs(int(actual[2])-201)<=2&&actual[3]==255,"Pack texture data incorrect");
    // Disabled option passes must not compile broken GLSL or create inputs.
    clearScene();
    Pass disabled{"composite", "intentionally invalid GLSL",{3},false};
    disabled.enabledProbe=fragment(R"(
layout(location=0) out vec4 optDisabledOutput;
void main(){optDisabledOutput=vec4(0.0);}
)");
    requestPipeline({disabled,end},"Disabled option fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"Disabled pass should be skipped");
    actual=pixel();
    require(std::abs(int(actual[0])-51)<=2,"Disabled pass modified scene");
    disabled.enabledProbe=fragment(R"(
layout(location=0) out vec4 optEnabledOutput;
void main(){optEnabledOutput=vec4(1.0);}
)");
    requestPipeline({disabled,end},"Enabled option fixture");
    beforeSwap(32,32);
    require(!status().active&&status().failed,"Enabled broken pass should reject graph");
    // Pass-local mipmaps must reflect metadata, generate from the real
    // ping-pong output, and be refreshed on a subsequent frame.
    clearScene();
    Pass mipSource{"composite",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=texture(colortex0,texcoord);}
)"),{0},false};
    Pass mipEnd{"final",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=textureLod(colortex0,vec2(0.5),5.0);}
)"),{0},true};
    mipEnd.enabledProbe=fragment(R"(
uniform float optMipInput0;
layout(location=0) out vec4 optEnabledOutput;
void main(){optEnabledOutput=vec4(optMipInput0);}
)");
    requestPipeline({mipSource,mipEnd},"Real mipmap fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"Mipmapped pass failed");
    actual=pixel();
    require(std::abs(int(actual[0])-51)<=2&&std::abs(int(actual[2])-153)<=2,"Mip chain did not contain scene data");
    glClearColor(0.8f,0.1f,0.2f,1.0f);glClear(GL_COLOR_BUFFER_BIT);
    beforeSwap(32,32);
    actual=pixel();
    require(std::abs(int(actual[0])-204)<=2&&std::abs(int(actual[2])-51)<=2,"Mip chain remained stale after new frame");
    // BSL's HDR scene attachment must retain values above 1.0 until
    // the final tone map. RGBA8 would clamp those values and fail readback.
    clearScene();
    Pass hdr{"composite",fragment(R"(
layout(location=0) out vec4 color;
void main(){color=vec4(4.0,2.0,1.0,1.0);}
)"),{0},false};
    hdr.colorFormats.push_back({0,"R11F_G11F_B10F"});
    Pass hdrEnd{"final",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(colortex0,texcoord).rgb*0.25,1.0);}
)"),{0},true};
    requestPipeline({hdr,hdrEnd},"HDR attachment fixture");
    beforeSwap(32,32);
    require(status().active&&!status().failed,"HDR attachment failed");
    actual=pixel();
    require(std::abs(int(actual[0])-255)<=2&&std::abs(int(actual[1])-128)<=2&&
        std::abs(int(actual[2])-64)<=2,"Intermediate HDR data was clipped");
    // Clear=false must keep the previous GPU output even when the writer
    // occurs after the first read, and reset on pipeline reapplication.
    clearScene();
    Pass history{"composite",fragment(R"(
uniform sampler2D colortex2;
layout(location=0) out vec4 color;
void main(){color=texture(colortex2,texcoord)+vec4(0.25,0.0,0.0,0.0);}
)"),{2},false};
    history.enabledProbe=fragment(R"(
uniform float optHistory2;
layout(location=0) out vec4 optEnabledOutput;
void main(){optEnabledOutput=vec4(optHistory2);}
)");
    Pass historyEnd{"final",fragment(R"(
uniform sampler2D colortex2;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(colortex2,texcoord).rgb,1.0);}
)"),{0},true};
    requestPipeline({history,historyEnd},"Persistent history fixture");
    for(int n=1;n<=3;++n){
        clearScene();beforeSwap(32,32);
        require(status().active&&!status().failed,"Persistent history graph failed");
        actual=pixel();
        require(std::abs(int(actual[0])-int(n*63.75f))<=2,"History did not survive a frame");
    }
    requestPipeline({history,historyEnd},"Reset history fixture");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(std::abs(int(actual[0])-64)<=2,"History not reset on reapply");
    requestPipeline({historyEnd},"Missing history writer fixture");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Missing history input was accepted");
    // Independent depth snapshots with genuine framebuffer depth storage.
    // No phase guesses and no CPU-generated replacement depth texture.
    clearScene();
    GLuint source=0,depth=0;
    glGenFramebuffers(1,&source);glBindFramebuffer(GL_FRAMEBUFFER,source);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,32,32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Depth fixture incomplete");
    std::string captureError;
    glDepthMask(GL_TRUE);glClearDepthf(0.25f);glClear(GL_DEPTH_BUFFER_BIT);
    require(captureSceneDepth(DepthPhase::BeforeHand,source,GL_DEPTH_COMPONENT24,32,32,captureError),"Before-hand capture failed");
    glClearDepthf(0.5f);glClear(GL_DEPTH_BUFFER_BIT);
    require(captureSceneDepth(DepthPhase::BeforeTranslucent,source,GL_DEPTH_COMPONENT24,32,32,captureError),"Opaque capture failed");
    glClearDepthf(0.75f);glClear(GL_DEPTH_BUFFER_BIT);
    require(captureSceneDepth(DepthPhase::Complete,source,GL_DEPTH_COMPONENT24,32,32,captureError),"Complete capture failed");
    GLint savedFbo=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&savedFbo);
    require(GLuint(savedFbo)==source,"Depth capture did not restore framebuffer");
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    Pass depthEnd{"final",fragment(R"(
uniform sampler2D depthtex0;
uniform sampler2D depthtex1;
uniform sampler2D depthtex2;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(depthtex0,texcoord).r,
                      texture(depthtex1,texcoord).r,
                      texture(depthtex2,texcoord).r,1.0);}
)"),{0},true};
    requestPipeline({depthEnd},"Real depth phase fixture");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Real depth graph rejected");
    actual=pixel();
    require(std::abs(int(actual[0])-191)<=2&&std::abs(int(actual[1])-128)<=2&&
        std::abs(int(actual[2])-64)<=2,"Depth phases did not match source data");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Stale depth was reused");
    actual=pixel();require(std::abs(int(actual[0])-51)<=2,"Stale depth modified vanilla scene");
    glDeleteFramebuffers(1,&source);glDeleteRenderbuffers(1,&depth);
    // Missing geometry input must not turn into a fake color sampler.
    clearScene();
    Pass unavailable{"final",fragment(R"(
uniform sampler2D colortex3;
layout(location=0) out vec4 color;
void main(){color=texture(colortex3,texcoord);}
)"),{0},true};
    requestPipeline({unavailable},"Missing input fixture");
    beforeSwap(32,32);
    require(!status().active&&status().failed&&
            status().message.find("Missing real input colortex3")!=std::string::npos,
            "Missing scene input should reject pipeline");
    actual=pixel();
    require(std::abs(int(actual[0])-51)<=2&&std::abs(int(actual[1])-102)<=2&&
            std::abs(int(actual[2])-153)<=2,"Rejected graph changed scene");
    requestDisable();beforeSwap(32,32);
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(d,context);eglDestroySurface(d,surface);eglTerminate(d);
    std::cout<<"Real GLES3 draw/readback and missing-input rejection: OK\n";
}
