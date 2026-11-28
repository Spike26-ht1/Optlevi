// End-to-end test of the actual original shader ZIPs, not copied GLSL.
using namespace modmenu::android::shaderworld;
static void check(bool ok,std::string const& message){
    if(!ok){std::cerr<<message<<" | "<<status().message<<"\n";std::exit(1);}
}
static void clearFrame(){
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,32,32);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0.2f,0.4f,0.6f,1);glClear(GL_COLOR_BUFFER_BIT);
}
static std::array<unsigned char,4> readPixel(){
    std::array<unsigned char,4> p{};glBindFramebuffer(GL_FRAMEBUFFER,0);
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());
    check(glGetError()==GL_NO_ERROR,"Readback failed");return p;
}
static WorldUniform matrix(char const* name,unsigned type){
    WorldUniform m;m.name=name;m.glType=type;
    if(type==GL_FLOAT_MAT3)m.floats={1,0,0,0,1,0,0,0,1};
    else m.floats={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    return m;
}
int main(int argc,char** argv){
    check(argc==2,"Missing shader ZIP folder");
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    check(eglInitialize(display,nullptr,nullptr)&&eglBindAPI(EGL_OPENGL_ES_API),"EGL initialization");
    const EGLint configAttrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config{};EGLint n=0;
    check(eglChooseConfig(display,configAttrs,&config,1,&n)&&n==1,"GLES configuration");
    const EGLint surfaceAttrs[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    const EGLint contextAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
    EGLContext ctx=eglCreateContext(display,config,EGL_NO_CONTEXT,contextAttrs);
    check(eglMakeCurrent(display,surface,surface,ctx),"GLES context");
    std::filesystem::path folder=argv[1];
    std::string error;
    auto load=[&](char const* file,char const* profile){
        OptPackInfo pack;pack.path=folder/file;
        std::vector<Pass> passes;error.clear();
        bool ok=optBuildJavaPipeline(pack,passes,error,profile);
        check(ok,std::string(file)+" "+profile+": "+error);
        requestPipeline(std::move(passes),file);
        clearFrame();beforeSwap(32,32);
    };
    load("OptLevi-Lab-Lite-0.1.zip","NEUTRAL");
    auto p=readPixel();
    check(status().active&&std::abs(int(p[0])-51)<=2&&std::abs(int(p[1])-102)<=2&&
          std::abs(int(p[2])-153)<=2,"Lite neutral failed");
    load("OptLevi-Lab-Lite-0.1.zip","VIVID");
    p=readPixel();
    check(status().active&&p[2]>153&&p[0]<51,"Lite profile did not change actual frame");
    // Load the actual embedded 3-pass pack through the full ZIP parser,
    // compile and draw its shader programs against real GLES backbuffer data.
    load("OptLevi-Lab-WorldFX-0.2.zip","PERFORMANCE");
    auto lowFx=readPixel();
    check(status().active&&lowFx[2]>100&&lowFx[0]>0,
          "WorldFX single-pass fallback did not execute");
    load("OptLevi-Lab-WorldFX-0.2.zip","BALANCED");
    auto balanced=readPixel();
    check(status().active&&balanced[2]>lowFx[2]&&balanced[1]>=lowFx[1],
          "WorldFX multi-pass bloom pipeline not applied");
    load("OptLevi-Lab-WorldFX-0.2.zip","QUALITY");
    auto quality=readPixel();
    check(status().active&&quality[2]>=balanced[2],
          "WorldFX quality profile did not execute");
        // Actual pack geometry consumes a real atlas texture and mapped VAO.
    // Native terrain mode must write back to the real source framebuffer,
    // not paint over an unrelated swap image. A controlled host depth
    // attachment has a depth step at x=16, from real GPU draw calls.
    load("OptLevi-Lab-Terrain-0.1.zip","");
    check(!status().active,"Native terrain effect ran without terrain");
    GLuint nativeFbo=0,nativeColor=0,nativeDepth=0,nativeVao=0;
    glGenFramebuffers(1,&nativeFbo);glBindFramebuffer(GL_FRAMEBUFFER,nativeFbo);
    glGenTextures(1,&nativeColor);glBindTexture(GL_TEXTURE_2D,nativeColor);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,32,32);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,nativeColor,0);
    glGenRenderbuffers(1,&nativeDepth);glBindRenderbuffer(GL_RENDERBUFFER,nativeDepth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,32,32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,nativeDepth);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Native FBO incomplete");
    // Standard right-handed perspective: camera faces -Z; nearer
    // geometry has a larger view-space Z. Do not use an identity inverse
    // for positive clip depths: that reverses the shader's occlusion test.
    const char* nativeVs="#version 300 es\nuniform float z;uniform mat4 projection;void main(){vec2 uv=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=projection*vec4((uv*2.0-1.0)*(-z),z,1);}";
    const char* nativeFs="#version 300 es\nprecision highp float;out vec4 color;void main(){color=vec4(0.8,0.8,0.8,1);}";
    auto stage=[&](GLenum type,char const* source){
        GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
        GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);check(ok,"Native fixture shader compilation");return shader;
    };
    GLuint vs=stage(GL_VERTEX_SHADER,nativeVs),fs=stage(GL_FRAGMENT_SHADER,nativeFs);
    GLuint nativeProgram=glCreateProgram();glAttachShader(nativeProgram,vs);glAttachShader(nativeProgram,fs);
    glLinkProgram(nativeProgram);glDeleteShader(vs);glDeleteShader(fs);glUseProgram(nativeProgram);
    GLint linked=0;glGetProgramiv(nativeProgram,GL_LINK_STATUS,&linked);check(linked,"Native fixture link");
    glGenVertexArrays(1,&nativeVao);glBindVertexArray(nativeVao);
    glViewport(0,0,32,32);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glClearDepthf(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    using modmenu::android::shaderbridge::camera::Matrix;
    Matrix nativeProjection{1,0,0,0, 0,1,0,0, 0,0,-1.5f,-1, 0,0,-2.5f,0};
    auto projection=matrix("gbufferProjectionInverse",GL_FLOAT_MAT4);
    check(modmenu::android::shaderbridge::camera::invert(nativeProjection,projection.floats),
          "Native perspective inverse failed");
    glUniformMatrix4fv(glGetUniformLocation(nativeProgram,"projection"),1,GL_FALSE,nativeProjection.data());
    glUniform1f(glGetUniformLocation(nativeProgram,"z"),-2.5f);glDrawArrays(GL_TRIANGLES,0,3);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,16,32);
    glUniform1f(glGetUniformLocation(nativeProgram,"z"),-2.0f);glDrawArrays(GL_TRIANGLES,0,3);
    glDisable(GL_SCISSOR_TEST);
    check(glGetError()==GL_NO_ERROR,"Native fixture depth draws failed");
    // Separate read/draw targets and a non-default active texture are
    // deliberately kept across the effect. Verify restored bindings.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glActiveTexture(GL_TEXTURE3);
    bool nativeOk=publishWorldUniforms({projection},error);
    check(nativeOk,"Native projection publish: "+error);
    nativeOk=renderNativeTerrainPhase(nativeFbo,32,32,error);
    check(nativeOk,"Native terrain phase: "+error);
    GLint restoredRead=0,restoredDraw=0,restoredProgram=0,restoredVao=0,restoredActive=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&restoredRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&restoredDraw);
    glGetIntegerv(GL_CURRENT_PROGRAM,&restoredProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&restoredVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&restoredActive);
    check(restoredRead==0&&GLuint(restoredDraw)==nativeFbo&&GLuint(restoredProgram)==nativeProgram&&
          GLuint(restoredVao)==nativeVao&&restoredActive==GL_TEXTURE3&&glIsEnabled(GL_DEPTH_TEST),
          "Native terrain effect leaked GLES bindings/state");
    glBindFramebuffer(GL_READ_FRAMEBUFFER,nativeFbo);
    glActiveTexture(GL_TEXTURE0);
    std::array<unsigned char,4> edge{},farPixel{};
    glReadPixels(17,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,edge.data());
    glReadPixels(30,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,farPixel.data());
    std::cout<<"Native terrain GPU pixels: edge="<<int(edge[0])<<" far="<<int(farPixel[0])<<"\n";
    check(edge[0]+10<farPixel[0]&&std::abs(int(farPixel[0])-204)<=3,
          "Native depth contact shading did not darken geometry edge");
    clearFrame();beforeSwap(32,32);p=readPixel();
    check(status().active&&metrics().presentedFrames==1&&std::abs(int(p[0])-51)<=2,
          "Native terrain phase incorrectly repainted swap framebuffer");
    clearFrame();beforeSwap(32,32);
    check(metrics().waitingFrames>0,"Missing native terrain phase was not reported");
    glUseProgram(0);glBindVertexArray(0);
    glDeleteProgram(nativeProgram);glDeleteVertexArrays(1,&nativeVao);
    glDeleteRenderbuffers(1,&nativeDepth);glDeleteTextures(1,&nativeColor);glDeleteFramebuffers(1,&nativeFbo);
    glDisable(GL_DEPTH_TEST);
    // The mesh is deliberately controlled; no claim of Bedrock gameplay.
    GLuint vao=0,vbo=0,atlas=0;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    std::vector<float> vertices;
    for(auto xy:std::array<std::array<float,2>,3>{{{-1,-1},{3,-1},{-1,3}}}){
        for(float v:std::array<float,19>{xy[0],xy[1],0,1, 0.4f,0.4f,0.4f,1,
                0,1,0, 0.5f,0.5f,0,1, 1,1,0,1})vertices.push_back(v);
    }
    glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(float)),vertices.data(),GL_STATIC_DRAW);
    const int counts[]={4,4,3,4,4},offsets[]={0,4,8,11,15};
    for(unsigned i=0;i<5;++i){
        glVertexAttribPointer(i,counts[i],GL_FLOAT,GL_FALSE,19*sizeof(float),
            reinterpret_cast<void*>(std::uintptr_t(offsets[i]*sizeof(float))));
        glEnableVertexAttribArray(i);
    }
    glGenTextures(1,&atlas);glBindTexture(GL_TEXTURE_2D,atlas);
    const unsigned char white[]={255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    WorldBatch terrain;terrain.geometry.program="gbuffers_terrain";
    terrain.geometry.vao=vao;terrain.geometry.count=3;
    terrain.geometry.uniforms={matrix("optModelView",GL_FLOAT_MAT4),matrix("optProjection",GL_FLOAT_MAT4),
        matrix("optNormalMatrix",GL_FLOAT_MAT3),matrix("optTextureMatrix0",GL_FLOAT_MAT4),
        matrix("optTextureMatrix1",GL_FLOAT_MAT4)};
    terrain.geometry.textures2D={{"optGeometryAtlas",atlas}};
    WorldScene world;world.width=world.height=32;world.fogClear={0.1f,0.2f,0.3f,1};world.opaque={terrain};
    load("OptLevi-Lab-World-0.1.zip","MINIMUM");
    check(!status().active,"World pack must wait for actual geometry");
    bool ok=renderJavaWorldScene(world,error);check(ok,"Lab world minimum: "+error);
    clearFrame();beforeSwap(32,32);p=readPixel();
    check(status().active&&std::abs(int(p[0])-110)<=3,"World terrain/deferred/final output incorrect");
    load("OptLevi-Lab-World-0.1.zip","LOW");
    WorldUniform sun;sun.name="sunAngle";sun.glType=GL_FLOAT;sun.floats[0]=0.25;
    WorldUniform eye;eye.name="cameraPosition";eye.glType=GL_FLOAT_VEC3;
    ok=publishWorldUniforms({sun,eye},error);check(ok,"World inputs: "+error);
    GeometryDraw shadow=terrain.geometry;shadow.program="shadow";
    world.shadowOpaque={shadow};
    // Use the original water program on a second real draw, preserving depth.
    auto water=terrain;water.geometry.program="gbuffers_water";water.blend=true;
    water.depthWrite=false;world.translucent={water};
    ok=renderJavaWorldScene(world,error);check(ok,"Lab world low: "+error);
    clearFrame();beforeSwap(32,32);p=readPixel();
    check(status().active&&p[2]>p[0],"World shadow/water/final path failed");
    requestDisable();beforeSwap(32,32);
    glDeleteTextures(1,&atlas);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,ctx);eglDestroySurface(display,surface);eglTerminate(display);
    std::cout<<"Original Lab ZIPs: neutral/vivid, terrain/deferred, shadow/water execution OK\n";
}
