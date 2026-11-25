// Original synthetic fixture: exercise actual GLES3 rendering and readback,
// not just source-string checks. This does not run Minecraft or BSL.
#include "ShaderWorldRuntime.h"
#include "ShaderCameraMath.h"
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
    // Centered perspective with a rotated and translated rigid view.
    using modmenu::android::shaderbridge::camera::Projection;
    using modmenu::android::shaderbridge::camera::recoverProjection;
    const float a=-101.0f/99.0f,b=-200.0f/99.0f;
    const std::array<float,16> p={2,0,0,0, 0,3,0,0, 0,0,a,-1, 0,0,b,0};
    // Eye (3,7,-11), right=(0,0,-1), up=(0,1,0), back=(1,0,0).
    const std::array<float,16> v={0,0,1,0, 0,1,0,0, -1,0,0,0, -11,-7,-3,1};
    std::array<float,16> vp{};
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)
        vp[c*4+r]+=p[k*4+r]*v[c*4+k];
    Projection recovered;
    require(recoverProjection(vp,{3,7,-11,0},recovered),"Native perspective recovery failed");
    require(std::abs(recovered.nearClip-1)<0.001f&&std::abs(recovered.farClip-100)<0.02f,
            "Recovered clip distances incorrect");
    for(int i=0;i<16;++i)require(std::abs(recovered.matrix[i]-p[i])<0.001f,"Recovered P differs");
    for(int c=0;c<4;++c)for(int r=0;r<4;++r){
        float x=0;for(int k=0;k<4;++k)x+=recovered.matrix[k*4+r]*recovered.inverse[c*4+k];
        require(std::abs(x-(c==r?1.0f:0.0f))<0.001f,"Recovered inverse incorrect");
    }
    require(!recoverProjection(vp,{0,0,0,0},recovered),"Unrelated eye accepted");
    auto skew=vp;skew[0]+=0.1f;
    require(!recoverProjection(skew,{3,7,-11,0},recovered),"Off-axis projection accepted");
    std::array<float,16> ortho={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    require(!recoverProjection(ortho,{0,0,0,0},recovered),"Orthographic projection accepted");
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
    // Separate light-space depth snapshots and hardware comparison.
    GLuint shadowFbo=0,shadowDepth=0,shadowColor=0;
    glGenFramebuffers(1,&shadowFbo);glBindFramebuffer(GL_FRAMEBUFFER,shadowFbo);
    glGenRenderbuffers(1,&shadowDepth);glBindRenderbuffer(GL_RENDERBUFFER,shadowDepth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,16,16);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,shadowDepth);
    glGenTextures(1,&shadowColor);glBindTexture(GL_TEXTURE_2D,shadowColor);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,16,16);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,shadowColor,0);
    const GLenum shadowDraw=GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1,&shadowDraw);glReadBuffer(shadowDraw);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Shadow fixture incomplete");
    glClearDepthf(0.25f);glClear(GL_DEPTH_BUFFER_BIT);
    require(captureShadowDepth(1,shadowFbo,GL_DEPTH_COMPONENT24,16,16,captureError),"Opaque shadow capture failed");
    glClearDepthf(0.75f);glClear(GL_DEPTH_BUFFER_BIT);
    require(captureShadowDepth(0,shadowFbo,GL_DEPTH_COMPONENT24,16,16,captureError),"Complete shadow capture failed");
    const GLfloat shadowTint[4]={0.2f,0.6f,0.4f,1.0f};
    glClearBufferfv(GL_COLOR,0,shadowTint);
    require(captureShadowColor(0,shadowFbo,0,"RGBA8",16,16,captureError),"Shadow color capture failed");
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    Pass shadowEnd{"final",fragment(R"(
precision highp sampler2DShadow;
uniform sampler2DShadow shadowtex0;
uniform sampler2DShadow shadowtex1;
uniform sampler2D shadowcolor0;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(shadowtex0,vec3(texcoord,0.5)),
                      texture(shadowtex1,vec3(texcoord,0.5)),
                      texture(shadowcolor0,texcoord).g,1.0);}
)"),{0},true};
    requestPipeline({shadowEnd},"Real light-space depth fixture");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Shadow comparison graph rejected");
    actual=pixel();
    require(actual[0]>=253&&actual[1]<=2&&std::abs(int(actual[2])-153)<=2,"Shadow comparison/color result incorrect");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Stale shadow resources reused");
    glDeleteFramebuffers(1,&shadowFbo);glDeleteRenderbuffers(1,&shadowDepth);glDeleteTextures(1,&shadowColor);
    // Typed actual world uniforms: source values are reflected from a linked
    // native fixture program, not synthesized by the Java shader runtime.
    auto buildShader=[](GLenum type,char const* code){
        GLuint sh=glCreateShader(type);glShaderSource(sh,1,&code,nullptr);glCompileShader(sh);
        GLint ok=0;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);require(ok,"Native uniform fixture compile");return sh;
    };
    GLuint nativeVs=buildShader(GL_VERTEX_SHADER,R"(#version 300 es
uniform mat4 nativeMatrix;
void main(){gl_Position=nativeMatrix*vec4(0.0,0.0,0.0,1.0);}
)");
    GLuint nativeFs=buildShader(GL_FRAGMENT_SHADER,R"(#version 300 es
precision highp float;
uniform vec3 nativeCamera;
uniform ivec2 nativeLight;
out vec4 color;
void main(){color=vec4(nativeCamera+vec3(nativeLight,0),1.0);}
)");
    GLuint nativeProgram=glCreateProgram();glAttachShader(nativeProgram,nativeVs);
    glAttachShader(nativeProgram,nativeFs);glLinkProgram(nativeProgram);
    glDeleteShader(nativeVs);glDeleteShader(nativeFs);
    GLint nativeOk=0;glGetProgramiv(nativeProgram,GL_LINK_STATUS,&nativeOk);require(nativeOk,"Native program link");
    glUseProgram(nativeProgram);
    const float matrix[16]={0.25f,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(nativeProgram,"nativeMatrix"),1,GL_FALSE,matrix);
    glUniform3f(glGetUniformLocation(nativeProgram,"nativeCamera"),0.5f,0.0f,0.0f);
    glUniform2i(glGetUniformLocation(nativeProgram,"nativeLight"),0,3);
    require(captureProgramUniforms(nativeProgram,{{"nativeMatrix","gbufferProjection",GL_FLOAT_MAT4},
        {"nativeCamera","cameraPosition",GL_FLOAT_VEC3},{"nativeLight","eyeBrightnessSmooth",GL_INT_VEC2}},captureError),
        "Native uniform capture failed");
    Pass worldEnd{"final",fragment(R"(
uniform mat4 gbufferProjection;
uniform vec3 cameraPosition;
uniform ivec2 eyeBrightnessSmooth;
layout(location=0) out vec4 color;
void main(){color=vec4(gbufferProjection[0][0],cameraPosition.x,float(eyeBrightnessSmooth.y)/4.0,1.0);}
)"),{0},true};
    requestPipeline({worldEnd},"Typed world uniform fixture");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Typed world uniform graph failed");
    actual=pixel();
    require(std::abs(int(actual[0])-64)<=2&&std::abs(int(actual[1])-128)<=2&&
        std::abs(int(actual[2])-191)<=2,"World uniform values not transferred");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Stale world uniforms were reused");
    // A selected Java pack survives a menu/world transition without Apply
    // being pressed again; the missing-resource signature is not retried
    // every frame, and the returning world supplies fresh actual values.
    auto waitingMessage=status().message;
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().message==waitingMessage,"Waiting graph changed without new resources");
    require(captureProgramUniforms(nativeProgram,{{"nativeMatrix","gbufferProjection",GL_FLOAT_MAT4},
        {"nativeCamera","cameraPosition",GL_FLOAT_VEC3},{"nativeLight","eyeBrightnessSmooth",GL_INT_VEC2}},captureError),
        "Returning world uniform capture failed");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Selected graph did not resume when world returned");
    actual=pixel();
    require(std::abs(int(actual[0])-64)<=2&&std::abs(int(actual[1])-128)<=2&&
        std::abs(int(actual[2])-191)<=2,"Resumed graph did not bind fresh data");
    requestPipeline({worldEnd},"Apply from main menu fixture");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().message.find("Waiting for verified world resources")!=std::string::npos,
        "Main menu apply should wait");
    require(captureProgramUniforms(nativeProgram,{{"nativeMatrix","gbufferProjection",GL_FLOAT_MAT4},
        {"nativeCamera","cameraPosition",GL_FLOAT_VEC3},{"nativeLight","eyeBrightnessSmooth",GL_INT_VEC2}},captureError),
        "Entering world uniform capture failed");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Main menu selection did not activate in world");
    glUseProgram(0);glDeleteProgram(nativeProgram);
    // Native G-buffer snapshots must contain the actual selected MRT
    // attachment, not the final color, and must expire after the frame.
    GLuint geometryFbo=0,geometryTex[2]{};
    glGenFramebuffers(1,&geometryFbo);glBindFramebuffer(GL_FRAMEBUFFER,geometryFbo);
    glGenTextures(2,geometryTex);
    const GLenum attachments[2]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
    for(int n=0;n<2;++n){
        glBindTexture(GL_TEXTURE_2D,geometryTex[n]);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,32,32);
        glFramebufferTexture2D(GL_FRAMEBUFFER,attachments[n],GL_TEXTURE_2D,geometryTex[n],0);
    }
    glDrawBuffers(2,attachments);glReadBuffer(GL_COLOR_ATTACHMENT0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Native MRT fixture incomplete");
    const GLfloat sceneColor[4]={0.1f,0.2f,0.3f,1.0f};
    const GLfloat geometryColor[4]={0.7f,0.4f,0.6f,1.0f};
    glClearBufferfv(GL_COLOR,0,sceneColor);glClearBufferfv(GL_COLOR,1,geometryColor);
    require(captureSceneColor(0,geometryFbo,0,"RGBA8",32,32,captureError),"Native color capture failed");
    require(captureSceneColor(1,geometryFbo,1,"RGBA8",32,32,captureError),"Native MRT capture failed");
    GLint nativeRead=0;glGetIntegerv(GL_READ_BUFFER,&nativeRead);
    require(nativeRead==GL_COLOR_ATTACHMENT0,"Native source read attachment changed");
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    Pass geometryEnd{"final",fragment(R"(
uniform sampler2D colortex0;
uniform sampler2D colortex1;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(colortex0,texcoord).r,texture(colortex1,texcoord).g,
texture(colortex1,texcoord).b,1.0);}
)"),{0},true};
    requestPipeline({geometryEnd},"Native MRT input fixture");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Native MRT graph rejected");
    actual=pixel();
    require(std::abs(int(actual[0])-26)<=2&&std::abs(int(actual[1])-102)<=2&&
        std::abs(int(actual[2])-153)<=2,"Native MRT input did not match attachments");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Stale native MRT data reused");
    glDeleteFramebuffers(1,&geometryFbo);glDeleteTextures(2,geometryTex);
    // Deferred output is generated only at the opaque boundary and can
    // be consumed by the native translucent stage before composite.
    GLuint opaqueFbo=0,opaqueTex=0;
    glGenFramebuffers(1,&opaqueFbo);glBindFramebuffer(GL_FRAMEBUFFER,opaqueFbo);
    glGenTextures(1,&opaqueTex);glBindTexture(GL_TEXTURE_2D,opaqueTex);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,32,32);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,opaqueTex,0);
    const GLenum opaqueDraw=GL_COLOR_ATTACHMENT0;glDrawBuffers(1,&opaqueDraw);glReadBuffer(opaqueDraw);
    const GLfloat opaqueColor[4]={0.4f,0.2f,0.6f,1.0f};
    glClearBufferfv(GL_COLOR,0,opaqueColor);
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Opaque input capture failed");
    Pass deferred{"deferred",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=texture(colortex0,texcoord)*0.5;}
)"),{0},false};
    deferred.deferred=true;
    Pass stagedEnd{"final",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(colortex0,texcoord).rgb,1.0);}
)"),{0},true};
    requestPipeline({deferred,stagedEnd},"Opaque phase fixture");
    clearScene();beforeSwap(32,32);
    actual=pixel();require(std::abs(int(actual[0])-51)<=2,"Deferred ran at presentation");
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Next opaque capture failed");
    require(renderDeferred(32,32,captureError),"Deferred phase execution failed");
    require(deferredTexture(0)!=0,"Deferred output not exposed to translucent stage");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Composite after deferred failed");
    actual=pixel();require(std::abs(int(actual[0])-51)<=2&&std::abs(int(actual[1])-26)<=2&&
        std::abs(int(actual[2])-77)<=2,"Deferred output not preserved");
    require(deferredTexture(0)==0,"Stale deferred output exposed after swap");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Missing opaque phase reported active");
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Returning opaque capture failed");
    require(renderDeferred(32,32,captureError),"Returning deferred phase failed");
    require(!renderDeferred(32,32,captureError),"Repeated opaque phase accepted");
    // Simulate the native material's updated G-buffer after translucency.
    glBindFramebuffer(GL_FRAMEBUFFER,opaqueFbo);
    const GLfloat translucentColor[4]={0.8f,0.4f,0.2f,1.0f};
    glClearBufferfv(GL_COLOR,0,translucentColor);
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Late geometry capture failed");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Updated geometry not accepted");
    actual=pixel();require(std::abs(int(actual[0])-204)<=2&&std::abs(int(actual[1])-102)<=2&&
        std::abs(int(actual[2])-51)<=2,"Late geometry did not replace deferred buffer");
    glDeleteFramebuffers(1,&opaqueFbo);glDeleteTextures(1,&opaqueTex);
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
