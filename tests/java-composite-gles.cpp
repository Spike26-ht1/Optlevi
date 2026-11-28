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
    // Inverse/previous values derive from actual Java-convention current
    // matrices. History advances only after successful presentation.
    WorldUniform pose;pose.name="gbufferModelView";pose.glType=GL_FLOAT_MAT4;
    pose.floats={2,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    WorldUniform position;position.name="cameraPosition";position.glType=GL_FLOAT_VEC3;
    position.floats[0]=0.25f;
    Pass cameraHistory{"final",fragment(R"(
uniform mat4 gbufferModelViewInverse;
uniform mat4 gbufferPreviousModelView;
uniform vec3 previousCameraPosition;
layout(location=0) out vec4 color;
void main(){color=vec4(gbufferModelViewInverse[0][0],
                       gbufferPreviousModelView[0][0]*0.25,
                       previousCameraPosition.x,1.0);}
)"),{0},true};
    require(publishWorldUniforms({pose,position},captureError),"Camera publish failed");
    requestPipeline({cameraHistory},"Camera history fixture");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(status().active&&std::abs(int(actual[0])-128)<=2&&
        std::abs(int(actual[1])-128)<=2&&std::abs(int(actual[2])-64)<=2,"First camera history invalid");
    pose.floats[0]=4;position.floats[0]=0.75f;
    require(publishWorldUniforms({pose,position},captureError),"Next camera publish failed");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(status().active&&std::abs(int(actual[0])-64)<=2&&
        std::abs(int(actual[1])-128)<=2&&std::abs(int(actual[2])-64)<=2,"Previous camera was not preserved");
    clearScene();beforeSwap(32,32);
    require(!status().active,"Missing current camera accepted");
    require(publishWorldUniforms({pose,position},captureError),"Reentry camera publish failed");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(status().active&&actual[1]>=253&&std::abs(int(actual[2])-191)<=2,"Camera history not reset after gap");
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
    // The final stage needs a uniform that is not available until after
    // the opaque callback. The linked graph must be ready at that boundary.
    stagedEnd.fragment=fragment(R"(
uniform sampler2D colortex0;
uniform float lateSceneValue;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(colortex0,texcoord).rgb*lateSceneValue,1.0);}
)");
    WorldUniform late;late.name="lateSceneValue";late.glType=GL_FLOAT;late.floats[0]=1;
    requestPipeline({deferred,stagedEnd},"Opaque phase fixture");
    clearScene();beforeSwap(32,32);
    actual=pixel();require(std::abs(int(actual[0])-51)<=2,"Deferred ran at presentation");
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Next opaque capture failed");
    require(renderDeferred(32,32,captureError),"Deferred phase execution failed");
    require(deferredTexture(0)!=0,"Deferred output not exposed to translucent stage");
    require(publishWorldUniforms({late},captureError),"Late composite uniform publish failed");
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
    require(publishWorldUniforms({late},captureError),"Returning late uniform publish failed");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Updated geometry not accepted");
    actual=pixel();require(std::abs(int(actual[0])-204)<=2&&std::abs(int(actual[1])-102)<=2&&
        std::abs(int(actual[2])-51)<=2,"Late geometry did not replace deferred buffer");
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Failure-frame opaque capture failed");
    require(renderDeferred(32,32,captureError),"Failure-frame deferred phase failed");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().failed,"Missing late composite input accepted");
    require(captureSceneColor(0,opaqueFbo,0,"RGBA8",32,32,captureError),"Recovery opaque capture failed");
    require(renderDeferred(32,32,captureError),"Graph lost readiness after missing late input");
    require(publishWorldUniforms({late},captureError),"Recovery late uniform failed");
    clearScene();beforeSwap(32,32);
    require(status().active&&!status().failed,"Staged graph did not recover");
    glDeleteFramebuffers(1,&opaqueFbo);glDeleteTextures(1,&opaqueTex);
    // Version-conditional pack material IDs and packed RenderChunk faces.
    std::vector<mesh::BlockRule> rules;
    require(mesh::parseBlockRules("#if MC_VERSION >= 11300\nblock.20000= \\\n minecraft:water minecraft:flowing_water\nblock.10500=minecraft:oak_leaves\nblock.15014=minecraft:campfire:lit=true\n#elif MC_VERSION >= 10800\nblock.15000=minecraft:water\n#else\nblock.10000=minecraft:water\n#endif\n",rules,captureError),"Block rules parse failed");
    int block=0;
    require(mesh::blockId(rules,"minecraft:water",{},block)&&block==20000,"Wrong water ID");
    require(mesh::blockId(rules,"minecraft:oak_leaves",{},block)&&block==10500,"Wrong leaf ID");
    require(mesh::blockId(rules,"minecraft:campfire",{{"lit","true"}},block)&&block==15014,"Wrong lit block ID");
    require(!mesh::blockId(rules,"minecraft:campfire",{{"lit","false"}},block),"Wrong state accepted");
    std::array<mesh::NativeVertex,4> face{};
    face[0].position={0,0,0};face[1].position={1,0,0};face[2].position={1,1,0};face[3].position={0,1,0};
    face[0].packedUV={0.1f,0.1f};face[1].packedUV={0.2f,0.1f};
    face[2].packedUV={0.2f,0.2f};face[3].packedUV={0.1f,0.2f};
    for(auto& vertex:face){vertex.color={1,1,1,1};vertex.packedLight=0;}
    std::array<mesh::JavaVertex,4> mapped{};
    require(mesh::translateQuad(face,{0.5f,0.5f,0.5f},20000,mapped,captureError),"Packed terrain face conversion failed");
    require(mapped[0].normal[2]>0.999f&&mapped[0].tangent[0]>0.999f&&mapped[0].entity[0]==20000&&
        mapped[0].midBlock[0]==32&&std::abs(mapped[0].midUV[0]-0.3f)<0.001f,"Translated terrain attributes wrong");
    face[2].position[2]=0.5f;
    require(!mesh::translateQuad(face,{0.5f,0.5f,0.5f},20000,mapped,captureError),"Nonplanar terrain face accepted");
    // A mapped indexed mesh runs a selected geometry vertex+fragment
    // pair into real MRT/depth attachments, without writing the backbuffer.
    Pass meshPack=end;
    meshPack.geometryPrograms.push_back({"gbuffers_terrain",R"(#version 300 es
in vec4 optPosition;
in vec4 optColor;
uniform mat4 optProjection;
out vec4 tint;
uniform vec3 optTerrainOffset;
void main(){gl_Position=optProjection*vec4(optPosition.xyz+optTerrainOffset,optPosition.w);tint=optColor;}
)",R"(#version 300 es
precision highp float;
in vec4 tint;
layout(location=0) out vec4 optRouteTarget0;
void main(){optRouteTarget0=tint;}
)",false});
    // Geometry has to be able to sample pack-owned textures BEFORE the
    // composite graph has any of its world resources. Otherwise there is
    // a cyclic startup dependency.
    meshPack.fragment=fragment(R"(
uniform sampler2D colortex3;
layout(location=0) out vec4 color;
void main(){color=texture(colortex3,texcoord);}
)");
    meshPack.packTextures.push_back({"noisetex",1,1,true,{255,255,255,255}});
    auto& terrain=meshPack.geometryPrograms.front().fragment;
    auto at=terrain.find("in vec4 tint;");
    require(at!=std::string::npos,"Geometry fixture source missing");
    terrain.insert(at,"uniform sampler2D noisetex;\nuniform float viewWidth;\nuniform float viewHeight;\nuniform int frameCounter;\n");
    at=terrain.find("optRouteTarget0=tint;");
    require(at!=std::string::npos,"Geometry fixture output missing");
    terrain.replace(at,std::string("optRouteTarget0=tint;").size(),
        "optRouteTarget0=tint*texture(noisetex,vec2(0.5))*vec4(viewWidth/32.0,viewHeight/32.0,frameCounter==0?1.0:0.0,1.0);");
    // Disabled shadow source must never be compiled, even if invalid GLSL.
    GeometryProgram disabledShadow{"shadow","invalid vertex","invalid fragment",true};
    disabledShadow.enabledProbe=fragment(R"(
layout(location=0) out vec4 optDisabledOutput;
void main(){optDisabledOutput=vec4(1.0);}
)");
    meshPack.geometryPrograms.push_back(std::move(disabledShadow));
    requestPipeline({meshPack},"Mapped geometry fixture");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().message.find("Missing real input colortex3")!=std::string::npos,
        "Expected composite to wait for geometry");
    GeometryHandle disabledGeometry;
    require(!acquireGeometryProgram("shadow",disabledGeometry,captureError)&&
        captureError.find("disabled by shader-pack settings")!=std::string::npos,
        "Disabled geometry shader was compiled");
    GLuint meshFbo=0,meshColor=0,meshDepth=0,meshVao=0,meshVbo=0,meshEbo=0;
    glGenFramebuffers(1,&meshFbo);glBindFramebuffer(GL_FRAMEBUFFER,meshFbo);
    glGenTextures(1,&meshColor);glBindTexture(GL_TEXTURE_2D,meshColor);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,32,32);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,meshColor,0);
    glGenRenderbuffers(1,&meshDepth);glBindRenderbuffer(GL_RENDERBUFFER,meshDepth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,32,32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,meshDepth);
    glDrawBuffers(1,&opaqueDraw);glReadBuffer(GL_COLOR_ATTACHMENT0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Mesh FBO incomplete");
    glClearColor(0,0,0,1);glClearDepthf(1);glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);
    glGenVertexArrays(1,&meshVao);glBindVertexArray(meshVao);
    glGenBuffers(1,&meshVbo);glBindBuffer(GL_ARRAY_BUFFER,meshVbo);
    const float verts[]={-1,-1,0,1, 0.25f,0.5f,0.75f,1,
                         3,-1,0,1, 0.25f,0.5f,0.75f,1,
                        -1, 3,0,1, 0.25f,0.5f,0.75f,1};
    glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(4*sizeof(float)));
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glGenBuffers(1,&meshEbo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,meshEbo);
    const unsigned short indices[]={0,1,2};glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    WorldUniform transform;transform.name="optProjection";transform.glType=GL_FLOAT_MAT4;
    transform.floats={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    GeometryDraw mesh;mesh.program="gbuffers_terrain";mesh.vao=meshVao;mesh.framebuffer=meshFbo;
    mesh.width=mesh.height=32;mesh.count=3;mesh.indexType=GL_UNSIGNED_SHORT;
    mesh.uniforms={transform};
    require(drawGeometry(mesh,captureError),"Mapped geometry draw failed");
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    require(std::abs(int(actual[0])-64)<=2&&std::abs(int(actual[1])-128)<=2&&
        std::abs(int(actual[2])-191)<=2,"Geometry output wrong");
    mesh.uniforms.clear();
    require(!drawGeometry(mesh,captureError),"Missing draw transform accepted");
    // Use the translated quad batch path, not a hand-built Java VAO.
    TerrainQuad quad;quad.canonicalName="minecraft:stone";quad.center={0,0,0};
    quad.vertices[0].position={-1,-1,0};quad.vertices[1].position={1,-1,0};
    quad.vertices[2].position={1,1,0};quad.vertices[3].position={-1,1,0};
    quad.vertices[0].packedUV={0.1f,0.1f};quad.vertices[1].packedUV={0.2f,0.1f};
    quad.vertices[2].packedUV={0.2f,0.2f};quad.vertices[3].packedUV={0.1f,0.2f};
    for(auto& vertex:quad.vertices){vertex.color={0.6f,0.2f,0.4f,1};vertex.packedLight=0;}
    mesh.uniforms={transform};glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    std::vector<mesh::NativeVertex> nativeVertices(quad.vertices.begin(),quad.vertices.end());
    TerrainFaceMetadata meta{quad.canonicalName,quad.states,quad.center};
    std::vector<TerrainQuad> assembled;
    require(assembleIndexedTerrainQuads(nativeVertices,{0,1,2,0,2,3},0,{meta},assembled,captureError),
        "Indexed native quad assembly failed");
    require(assembled.size()==1&&assembled[0].vertices[3].position==quad.vertices[3].position,
        "Native quad order wrong");
    require(assembleIndexedTerrainQuads(nativeVertices,{2,3,4,2,4,5},-2,{meta},assembled,captureError),
        "Signed base vertex assembly failed");
    require(!assembleIndexedTerrainQuads(nativeVertices,{0,1,2,0,3,2},0,{meta},assembled,captureError),
        "Reversed second triangle accepted");
    require(!assembleIndexedTerrainQuads(nativeVertices,{0,1,2,0,2,8},0,{meta},assembled,captureError),
        "Out-of-range index accepted");
    require(!assembleIndexedTerrainQuads(nativeVertices,{0,1,2,0,2,3},0,{},assembled,captureError),
        "Missing block metadata accepted");
    require(assembled.size()==1,"Failed assembly modified existing batch");
    require(assembleIndexedTerrainQuads(nativeVertices,{0,1,3,1,2,3},0,{meta},assembled,captureError),
        "Alternate native diagonal rejected");
    require(assembled[0].vertices[0].position==quad.vertices[3].position&&
        assembled[0].vertices[2].position==quad.vertices[1].position,"Original diagonal not preserved");
    // Real GLES buffer fixture: packed native layout, nonzero EBO offset,
    // signed base vertex, separate verified light component and coordinate mapping.
    GLuint packedVao=0,packedVbo=0,packedEbo=0;
    glGenVertexArrays(1,&packedVao);glBindVertexArray(packedVao);
    glGenBuffers(1,&packedVbo);glBindBuffer(GL_ARRAY_BUFFER,packedVbo);
    struct Packed {std::uint16_t pos[4];std::uint8_t color[4];std::uint16_t uv[2],light[2];};
    const Packed packed[]={
        {{0xbc00,0xbc00,0,0x3c00},{153,51,102,255},{6554,6554},{123,0}},
        {{0x3c00,0xbc00,0,0x3c00},{153,51,102,255},{13107,6554},{123,0}},
        {{0x3c00,0x3c00,0,0x3c00},{153,51,102,255},{13107,13107},{123,0}},
        {{0xbc00,0x3c00,0,0x3c00},{153,51,102,255},{6554,13107},{123,0}}
    };
    glBufferData(GL_ARRAY_BUFFER,sizeof(packed),packed,GL_STATIC_DRAW);
    glVertexAttribPointer(0,4,GL_HALF_FLOAT,GL_FALSE,sizeof(Packed),reinterpret_cast<void*>(offsetof(Packed,pos)));
    glVertexAttribPointer(1,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Packed),reinterpret_cast<void*>(offsetof(Packed,color)));
    glVertexAttribPointer(2,2,GL_UNSIGNED_SHORT,GL_TRUE,sizeof(Packed),reinterpret_cast<void*>(offsetof(Packed,uv)));
    glVertexAttribPointer(3,2,GL_UNSIGNED_SHORT,GL_TRUE,sizeof(Packed),reinterpret_cast<void*>(offsetof(Packed,light)));
    for(unsigned i=0;i<4;++i)glEnableVertexAttribArray(i);
    glGenBuffers(1,&packedEbo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,packedEbo);
    const std::uint16_t packedIndices[]={999,2,3,4,2,4,5};
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(packedIndices),packedIndices,GL_STATIC_DRAW);
    glBindBuffer(GL_COPY_READ_BUFFER,meshVbo);glBindBuffer(GL_COPY_WRITE_BUFFER,meshEbo);
    PackedTerrainSource nativeSource;
    nativeSource.position=0;nativeSource.color=1;nativeSource.uv=2;nativeSource.light=3;
    nativeSource.lightComponent=1;nativeSource.indexType=GL_UNSIGNED_SHORT;
    nativeSource.indexOffset=2;nativeSource.baseVertex=-2;nativeSource.scale={1,1,1};
    require(capturePackedTerrainQuads(nativeSource,{meta},assembled,captureError),"Packed native GLES capture failed");
    require(assembled[0].vertices[0].position==quad.vertices[0].position&&
        assembled[0].vertices[2].packedLight==0,"Packed native values wrong");
    GLint copyRead=0,copyWrite=0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING,&copyRead);glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING,&copyWrite);
    require(GLuint(copyRead)==meshVbo&&GLuint(copyWrite)==meshEbo,"Packed capture leaked copy bindings");
    nativeSource.indexOffset=4;
    require(!capturePackedTerrainQuads(nativeSource,{meta},assembled,captureError),"Truncated EBO accepted");
    require(assembled.size()==1,"Failed native capture modified previous result");
    glBindBuffer(GL_COPY_READ_BUFFER,0);glBindBuffer(GL_COPY_WRITE_BUFFER,0);
    glBindVertexArray(meshVao);glBindBuffer(GL_ARRAY_BUFFER,meshVbo);
    glDeleteVertexArrays(1,&packedVao);glDeleteBuffers(1,&packedVbo);glDeleteBuffers(1,&packedEbo);
    require(drawTerrainQuads(mesh,assembled,captureError),"Mapped terrain batch draw failed");
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    require(std::abs(int(actual[0])-153)<=2&&std::abs(int(actual[1])-51)<=2&&
        std::abs(int(actual[2])-102)<=2,"Mapped terrain batch output wrong");
    // Persistent GPU mesh cache: hit without CPU input, replacement and
    // stale-token rejection. The same draw ABI is used by world and shadow.
    std::uint64_t token=0,hit=0,newToken=0;
    require(prepareTerrainMesh(17,1,assembled,token,captureError)&&token,
        "Terrain cache upload failed");
    require(prepareTerrainMesh(17,1,{},hit,captureError)&&hit==token,
        "Terrain cache hit required CPU geometry");
    auto cached=mesh;cached.vao=0;cached.terrainMesh=token;
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    require(drawGeometry(cached,captureError),"Cached terrain draw failed");
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    require(std::abs(int(actual[0])-153)<=2&&std::abs(int(actual[1])-51)<=2&&
        std::abs(int(actual[2])-102)<=2,"Cached terrain output wrong");
    // Move the existing GPU mesh offscreen, then reset the offset on the
    // same token. No CPU face rebuild and no stale per-draw translation.
    cached.terrainOffset={4,0,0};
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    require(drawGeometry(cached,captureError),"Offset cached terrain draw failed");
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    require(actual[0]<=2&&actual[1]<=2&&actual[2]<=2,"Cached mesh offset not applied");
    cached.terrainOffset={0,0,0};
    require(drawGeometry(cached,captureError),"Reset cached terrain offset failed");
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
    require(std::abs(int(actual[0])-153)<=2,"Cached mesh offset leaked between draws");
    require(!prepareTerrainMesh(17,2,{},newToken,captureError),"Missing revised mesh accepted");
    require(!drawGeometry(cached,captureError),"Stale cached mesh accepted");
    require(prepareTerrainMesh(17,2,assembled,newToken,captureError)&&newToken!=token,
        "Terrain cache revision upload failed");
    cached.terrainMesh=newToken;
    require(drawGeometry(cached,captureError),"Revised mesh draw failed");
    require(releaseTerrainMesh(newToken)&&!releaseTerrainMesh(newToken),
        "Terrain cache release failed");
    require(!drawGeometry(cached,captureError),"Released mesh accepted");
    GLint restoredVao=0,restoredArray=0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&restoredVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&restoredArray);
    require(GLuint(restoredVao)==meshVao&&GLuint(restoredArray)==meshVbo,"Terrain batch leaked GL bindings");
    glDisable(GL_DEPTH_TEST);glBindFramebuffer(GL_FRAMEBUFFER,0);glBindVertexArray(0);
    glDeleteBuffers(1,&meshVbo);glDeleteBuffers(1,&meshEbo);glDeleteVertexArrays(1,&meshVao);
    glDeleteRenderbuffers(1,&meshDepth);glDeleteTextures(1,&meshColor);glDeleteFramebuffers(1,&meshFbo);
    // Calculated Java uniforms are resolved from actual inputs, not native
    // material-specific values. Missing dependencies stay unavailable.
    Pass calculated{"final",fragment(R"(
uniform float timeBrightness;
uniform float blindFactor;
uniform float isCold;
layout(location=0) out vec4 color;
void main(){color=vec4(timeBrightness,blindFactor,isCold,1.0);}
)"),{0},true};
    require(custom::parse("variable.float.phase=frac(sunAngle - 0.05)\n"
        "uniform.float.timeBrightness=max(sin(phase * 6.28318530718),0.0)\n"
        "uniform.float.blindFactor=clamp(blindness * 2.0 - 1.0,0.0,1.0)\n"
        "#if MC_VERSION >= 11800\n"
        "uniform.float.isCold=if(in(biome,BIOME_GROVE),1,0) * \\\n"
        " if(cameraPosition.y >= 113,1,0)\n#else\nuniform.float.isCold=0\n#endif\n",
        calculated.customUniforms,captureError),"Java uniform declarations failed");
    WorldUniform sun;sun.name="sunAngle";sun.glType=GL_FLOAT;sun.floats[0]=0.30f;
    WorldUniform blind;blind.name="blindness";blind.glType=GL_FLOAT;blind.floats[0]=0.75f;
    WorldUniform biome;biome.name="biome";biome.glType=GL_INT;biome.ints[0]=7;
    WorldUniform grove=biome;grove.name="BIOME_GROVE";
    WorldUniform altitude;altitude.name="cameraPosition";altitude.glType=GL_FLOAT_VEC3;altitude.floats[1]=120;
    require(publishWorldUniforms({sun,blind,biome,grove,altitude},captureError),"Calculated uniform input failed");
    requestPipeline({calculated},"Custom Java uniform fixture");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(status().active&&actual[0]>=253&&std::abs(int(actual[1])-128)<=2&&actual[2]>=253,
        "Calculated uniform pixel incorrect");
    clearScene();beforeSwap(32,32);require(!status().active,"Stale calculated values accepted");
    std::vector<custom::Rule> smoothRules;
    require(custom::parse("uniform.float.value=smooth(7,target,10,10)\n",smoothRules,captureError),"Smooth expression parse failed");
    custom::Smooth previous{{7,0}};
    custom::Evaluation eval{smoothRules,[](std::string const& name)->custom::Number{
        return name=="target"?custom::Number{1}:custom::Number{};
    },previous,10};
    auto smooth=eval.get("value");
    require(smooth&&std::abs(*smooth-0.5)<1e-6,"Smooth half-life incorrect");
    require(eval.get("value")==smooth,"Repeated smooth evaluation changed result");
    std::vector<custom::Rule> cyclic;
    require(custom::parse("variable.float.a=b\nuniform.float.b=a\n",cyclic,captureError),"Cycle fixture parse failed");
    custom::Evaluation cycle{cyclic,[](std::string const&)->custom::Number{return {};},previous,0};
    require(!cycle.get("b"),"Cyclic custom uniform accepted");
    // Shader-pack quality profiles inherit settings, then override only
    // recognized declarations with valid option values.
    profiles::Definitions profileDefs;profiles::Settings profileSettings;
    require(profiles::read("profile.MINIMUM=!AO !SHADOW shadowMapResolution=512\n"
        "profile.LOW=profile.MINIMUM SHADOW shadowMapResolution=1024\n",
        profileDefs,captureError),"Profile parse failed");
    require(profiles::resolve(profileDefs,"LOW",profileSettings,captureError),"Profile inheritance failed");
    require(profileSettings["AO"]=="false"&&profileSettings["SHADOW"]=="true"&&
        profileSettings["shadowMapResolution"]=="1024","Profile overrides incorrect");
    std::string optionSource="#define AO\n// #define SHADOW\nconst int shadowMapResolution = 2048; //[512 1024 2048]\n";
    std::set<std::string> appliedOptions;
    require(profiles::apply(optionSource,profileSettings,appliedOptions,captureError),"Profile rewrite failed");
    require(optionSource.find("// #define AO")!=std::string::npos&&
        optionSource.find("\n#define SHADOW")!=std::string::npos&&
        optionSource.find("= 1024;")!=std::string::npos&&appliedOptions.size()==3,"Profile declarations incorrect");
    profileSettings["shadowMapResolution"]="999";
    require(!profiles::apply(optionSource,profileSettings,appliedOptions,captureError),"Invalid profile value accepted");
    profileDefs["MINIMUM"]={"profile.LOW"};
    require(!profiles::resolve(profileDefs,"LOW",profileSettings,captureError),"Profile cycle accepted");
    // A real published sun angle and camera pose drive the Java shadow
    // projection. The actual shadow depth still has to come from geometry.
    using modmenu::android::shaderbridge::camera::ShadowSettings;
    using modmenu::android::shaderbridge::camera::ShadowCamera;
    Pass lightCamera{"final",fragment(R"(
uniform mat4 shadowProjection;
uniform mat4 shadowModelView;
uniform mat4 shadowModelViewInverse;
layout(location=0) out vec4 color;
void main(){color=vec4(shadowProjection[0][0]*32.0,
(shadowModelView*shadowModelViewInverse)[1][1],0.25,1.0);}
)"),{0},true};
    ShadowSettings settings;settings.distance=64;settings.rotation=-40;settings.resolution=512;
    lightCamera.shadowSettings=settings;
    requestPipeline({lightCamera},"Java shadow camera fixture");
    clearScene();beforeSwap(32,32);
    require(!status().active,"Shadow camera without world inputs accepted");
    WorldUniform realSun;realSun.name="sunAngle";realSun.glType=GL_FLOAT;realSun.floats[0]=0.25f;
    WorldUniform realEye;realEye.name="cameraPosition";realEye.glType=GL_FLOAT_VEC3;
    realEye.floats[0]=2;realEye.floats[1]=65;realEye.floats[2]=-7;
    require(publishWorldUniforms({realSun,realEye},captureError),"Shadow camera source publish failed");
    ShadowCamera light;int mapSize=0;
    require(getShadowCamera(light,mapSize,captureError)&&mapSize==512&&
        std::abs(light.projection[0]-1.0f/64)<1e-6,"Shadow camera query wrong");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(status().active&&std::abs(int(actual[0])-128)<=2&&actual[1]>=253&&
        std::abs(int(actual[2])-64)<=2,"Java shadow camera uniform output wrong");
    clearScene();beforeSwap(32,32);require(!status().active,"Stale shadow camera accepted");
    // Render real mapped triangles into the engine-owned shadow targets.
    // The test uses a synthetic mesh, but depth is written by the GPU and
    // the final program samples BOTH opaque-only and complete shadow maps.
    Pass shadowScene{"final",fragment(R"(
precision highp sampler2DShadow;
uniform sampler2DShadow shadowtex0;
uniform sampler2DShadow shadowtex1;
uniform sampler2D shadowcolor0;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(shadowtex0,vec3(0.5,0.5,0.5)),
texture(shadowtex1,vec3(0.5,0.5,0.5)),texture(shadowcolor0,vec2(0.5)).g,1.0);}
)"),{0},true};
    settings.resolution=32;settings.distance=1;settings.nearPlane=-1;settings.farPlane=1;
    settings.rotation=0;settings.interval=0;
    shadowScene.shadowSettings=settings;
    shadowScene.geometryPrograms.push_back({"shadow",R"(#version 300 es
in vec4 optPosition;
in vec4 optColor;
uniform mat4 optModelViewProjection;
out vec4 tint;
void main(){gl_Position=optModelViewProjection*optPosition;tint=optColor;}
)",R"(#version 300 es
precision highp float;
in vec4 tint;
layout(location=0) out vec4 optRouteTarget0;
void main(){optRouteTarget0=tint;}
)",true});
    requestPipeline({shadowScene},"Engine owned shadow scene fixture");
    clearScene();beforeSwap(32,32);
    require(!status().active,"Shadow graph accepted without generated maps");
    require(publishWorldUniforms({realSun,realEye},captureError),"Shadow scene inputs failed");
    require(getShadowCamera(light,mapSize,captureError),"Shadow scene camera failed");
    GLuint shadowMeshVao=0,shadowMeshVbo=0;
    glGenVertexArrays(1,&shadowMeshVao);glBindVertexArray(shadowMeshVao);
    glGenBuffers(1,&shadowMeshVbo);glBindBuffer(GL_ARRAY_BUFFER,shadowMeshVbo);
    // Invert the actual camera transform to construct known light-space
    // triangle positions in the camera-relative world coordinate frame.
    std::vector<float> shadowVerts;
    auto inverse=modmenu::android::shaderbridge::camera::multiply(light.viewInverse,light.projectionInverse);
    for(int layer=0;layer<2;++layer){
        float z=layer==0?0.5f:-0.5f;
        for(auto xy:std::array<std::array<float,2>,3>{{{-1,-1},{3,-1},{-1,3}}}){
            std::array<float,4> clip{xy[0],xy[1],z,1};
            for(int row=0;row<4;++row){
                float v=0;for(int col=0;col<4;++col)v+=inverse[col*4+row]*clip[col];
                shadowVerts.push_back(v);
            }
            for(float v:std::array<float,4>{1,layer==0?0.2f:0.6f,1,1})shadowVerts.push_back(v);
        }
    }
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(shadowVerts.size()*sizeof(float)),shadowVerts.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(4*sizeof(float)));glEnableVertexAttribArray(1);
    GeometryDraw caster;caster.program="shadow";caster.vao=shadowMeshVao;caster.count=3;
    auto transparentCaster=caster;transparentCaster.first=3;
    require(renderShadowScene({caster},{transparentCaster},captureError),"Engine shadow pass failed");
    clearScene();beforeSwap(32,32);actual=pixel();
    require(status().active&&actual[0]<=2&&actual[1]>=253&&
        std::abs(int(actual[2])-153)<=2,"Generated opaque/translucent shadow textures wrong");
    clearScene();beforeSwap(32,32);require(!status().active,"Generated shadow textures reused without world draw");
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    glDeleteVertexArrays(1,&shadowMeshVao);glDeleteBuffers(1,&shadowMeshVbo);
    // Engine-owned world coordinator: opaque, hand, deferred, translucent
    // and final execute in order using real rendered MRT/depth attachments.
    Pass sceneDeferred{"deferred",fragment(R"(
uniform sampler2D colortex0;
layout(location=0) out vec4 color;
void main(){color=texture(colortex0,texcoord)*0.5;}
)"),{0},false};sceneDeferred.deferred=true;
    Pass sceneFinal{"final",fragment(R"(
uniform sampler2D colortex0;
uniform sampler2D depthtex0;
uniform sampler2D depthtex1;
uniform sampler2D depthtex2;
layout(location=0) out vec4 color;
void main(){color=vec4(texture(colortex0,texcoord).r,
texture(depthtex0,texcoord).r,texture(depthtex1,texcoord).r,texture(depthtex2,texcoord).r);}
)"),{0},true};
    GeometryProgram sceneTerrain{"gbuffers_terrain",R"(#version 300 es
in vec4 optPosition;in vec4 optColor;out vec4 tint;
void main(){gl_Position=optPosition;tint=optColor;}
)",R"(#version 300 es
precision highp float;in vec4 tint;
layout(location=0) out vec4 optRouteTarget0;
void main(){optRouteTarget0=tint;}
)",false};
    sceneDeferred.geometryPrograms.push_back(sceneTerrain);
    requestPipeline({sceneDeferred,sceneFinal},"Complete world phase coordinator fixture");
    clearScene();beforeSwap(32,32);
    GLuint sceneVao=0,sceneVbo=0;
    glGenVertexArrays(1,&sceneVao);glBindVertexArray(sceneVao);
    glGenBuffers(1,&sceneVbo);glBindBuffer(GL_ARRAY_BUFFER,sceneVbo);
    std::vector<float> sceneVerts;
    for(float z:{0.5f,0.0f,-0.5f})for(auto xy:std::array<std::array<float,2>,3>{{{-1,-1},{3,-1},{-1,3}}}){
        for(float v:std::array<float,8>{xy[0],xy[1],z,1,0.8f,0.2f,0.4f,1})sceneVerts.push_back(v);
    }
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(sceneVerts.size()*sizeof(float)),sceneVerts.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,8*sizeof(float),nullptr);glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,8*sizeof(float),reinterpret_cast<void*>(4*sizeof(float)));glEnableVertexAttribArray(1);
    WorldScene worldScene;worldScene.width=worldScene.height=32;worldScene.fogClear={0.2f,0.3f,0.4f,1};
    WorldBatch worldOpaque;worldOpaque.geometry.program="gbuffers_terrain";worldOpaque.geometry.vao=sceneVao;
    worldOpaque.geometry.count=3;
    auto worldHand=worldOpaque;worldHand.geometry.first=3;
    auto worldTranslucent=worldOpaque;worldTranslucent.geometry.first=6;
    worldScene.opaque={worldOpaque};worldScene.hand={worldHand};worldScene.translucent={worldTranslucent};
    require(renderJavaWorldScene(worldScene,captureError),"World phase coordinator failed");
    require(!renderJavaWorldScene(worldScene,captureError),"Duplicate world scene accepted");
    require(!ownedGpuWork(),"GPU scope leaked after geometry");
    {OwnedGpuScope outer;require(ownedGpuWork(),"GPU scope missing");
     {OwnedGpuScope inner;require(ownedGpuWork(),"Nested GPU scope missing");}
     require(ownedGpuWork(),"Nested GPU scope ended outer scope");}
    require(!ownedGpuWork(),"GPU scope leaked after nesting");
    require(presentJavaWorldScene(0,32,32,captureError),"Pre-HUD mapped world presentation failed");
    actual=pixel();
    require(status().active&&std::abs(int(actual[0])-204)<=2&&std::abs(int(actual[1])-64)<=2&&
        std::abs(int(actual[2])-128)<=2&&std::abs(int(actual[3])-191)<=2,"World geometry phase order incorrect");
    require(!presentJavaWorldScene(0,32,32,captureError),"Duplicate pre-HUD presentation accepted");
    // Simulate the native HUD drawn after world composition. Swap must not
    // replace it with the already-presented Java scene.
    clearScene();beforeSwap(32,32);actual=pixel();
    require(std::abs(int(actual[0])-51)<=2&&std::abs(int(actual[1])-102)<=2&&
        std::abs(int(actual[2])-153)<=2,"Swap overwrote native HUD after pre-HUD presentation");
    clearScene();beforeSwap(32,32);require(!status().active,"Stale world frame reused");
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    glDeleteVertexArrays(1,&sceneVao);glDeleteBuffers(1,&sceneVbo);
    // A geometry-bearing pack without deferred passes must never turn
    // the composited backbuffer into its main Java colortex0.
    Pass noDeferred=end;
    noDeferred.geometryPrograms={sceneTerrain};
    requestPipeline({noDeferred},"Geometry pack without deferred fixture");
    clearScene();beforeSwap(32,32);
    require(!status().active&&status().message.find("native scene colortex0")!=std::string::npos,
            "Geometry pack fell back to the backbuffer");
    actual=pixel();
    require(std::abs(int(actual[0])-51)<=2&&std::abs(int(actual[1])-102)<=2&&
            std::abs(int(actual[2])-153)<=2,"Unprovided geometry pack changed vanilla scene");
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
