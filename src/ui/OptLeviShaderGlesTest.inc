// OptLevi Java shader GPU test bench, GLES3 (Android).
//
// This deliberately tests *one* Iris/OptiFine final or composite fragment
// pass in an offscreen 320x180 preview. It DOES NOT hook RenderDragon's
// scene, replace material.bin, or execute a full Java framebuffer pipeline.
// GLSL compile is only triggered by an explicit user interaction.
#include <zlib.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

static EGLContext gOptPreviewContext=EGL_NO_CONTEXT;
static GLuint gOptPreviewTexture=0;
static std::string gOptPreviewReport;
static std::string gOptPreviewFile;
static bool gOptPreviewSuccess=false;

static constexpr int kPreviewW=320;
static constexpr int kPreviewH=180;
static constexpr size_t kMaxShaderBytes=256*1024;
static constexpr size_t kMaxZipBytes=64*1024*1024;

static void optPreviewCheckContext() {
    EGLContext ctx=eglGetCurrentContext();
    if(ctx!=gOptPreviewContext) {
        // Old texture belongs to old context: don't delete its name on a
        // newly created context (could belong to the game).
        gOptPreviewContext=ctx;
        gOptPreviewTexture=0;
        gOptPreviewSuccess=false;
        gOptPreviewReport.clear();
    }
}

static bool optReadBounded(std::filesystem::path const& path,
                           std::string& out,size_t limit) {
    std::error_code ec;
    auto size=std::filesystem::file_size(path,ec);
    if(ec||size==0||size>limit)return false;
    std::ifstream in(path,std::ios::binary);
    if(!in)return false;
    out.resize(static_cast<size_t>(size));
    in.read(out.data(),static_cast<std::streamsize>(out.size()));
    return static_cast<size_t>(in.gcount())==out.size();
}

// Read only a single .fsh entry by ZIP central-directory lookup. Nothing is
// extracted to filesystem, and output is bounded (zip-bomb protection).
static bool optReadZipShader(std::filesystem::path const& file,
                            std::string& source,std::string& origin) {
    std::string zip;
    if(!optReadBounded(file,zip,kMaxZipBytes)||zip.size()<22)return false;
    auto* p=reinterpret_cast<unsigned char const*>(zip.data());
    size_t size=zip.size(),eocd=size;
    for(size_t i=size-22;;--i) {
        if(optLE32(p+i)==0x06054b50){eocd=i;break;}
        if(i==0||size-i>65557)break;
    }
    if(eocd==size)return false;
    size_t cd=optLE32(p+eocd+16);
    size_t count=optLE16(p+eocd+10);
    if(cd>size||count>4096)return false;
    size_t best=0,bestPos=0;
    std::string bestName;
    for(size_t n=0;n<count;n++) {
        if(cd+46>size||optLE32(p+cd)!=0x02014b50)return false;
        size_t len=optLE16(p+cd+28),extra=optLE16(p+cd+30),
               comment=optLE16(p+cd+32);
        if(len>512||cd+46+len+extra+comment>size)return false;
        std::string name(zip.data()+cd+46,len);
        std::string lower=optLower(name);
        for(char& c:lower)if(c=='\\')c='/';
        bool shaderDir=lower.rfind("shaders/",0)==0 ||
                         lower.find("/shaders/")!=std::string::npos;
        int rank=0;
        if(shaderDir && optEnds(lower,"/final.fsh"))rank=4;
        else if(shaderDir && optEnds(lower,"/composite.fsh"))rank=3;
        else if(shaderDir && optEnds(lower,"/composite1.fsh"))rank=2;
        else if(shaderDir && optEnds(lower,".fsh"))rank=1;
        if(rank>int(best) && optLE32(p+cd+24)>0 &&
           optLE32(p+cd+24)<=kMaxShaderBytes &&
           (optLE16(p+cd+10)==0||optLE16(p+cd+10)==8) &&
           (optLE16(p+cd+8)&1)==0){
            best=size_t(rank);bestPos=cd;bestName=name;
        }
        cd+=46+len+extra+comment;
    }
    if(best==0)return false;
    size_t unpacked=optLE32(p+bestPos+24);
    size_t packed=optLE32(p+bestPos+20);
    size_t local=optLE32(p+bestPos+42);
    auto method=optLE16(p+bestPos+10);
    if(local>size||local+30>size||optLE32(p+local)!=0x04034b50)return false;
    size_t data=local+30+optLE16(p+local+26)+optLE16(p+local+28);
    if(data>size||packed>size-data||packed>kMaxShaderBytes)return false;
    if(method==0) {
        if(packed!=unpacked)return false;
        source.assign(zip.data()+data,packed);
    }else{
        source.resize(unpacked);
        z_stream zs{};
        if(inflateInit2(&zs,-MAX_WBITS)!=Z_OK)return false;
        zs.next_in=(Bytef*)(p+data);
        zs.avail_in=static_cast<uInt>(packed);
        zs.next_out=(Bytef*)source.data();
        zs.avail_out=static_cast<uInt>(unpacked);
        int status=inflate(&zs,Z_FINISH);
        bool ok=status==Z_STREAM_END&&zs.total_out==unpacked;
        inflateEnd(&zs);
        if(!ok){source.clear();return false;}
    }
    origin=bestName;
    return true;
}

static bool optReadJavaShader(OptPackInfo const& pack,
                              std::string& source,std::string& origin) {
    if(pack.folder) {
        std::error_code ec;
        for(char const* name:{"shaders/final.fsh","shaders/composite.fsh",
                              "shaders/composite1.fsh"}) {
            auto p=pack.path/name;
            if(std::filesystem::is_regular_file(p,ec)&&
               optReadBounded(p,source,kMaxShaderBytes)){
                origin=name;
                return true;
            }
        }
        return false;
    }
    if(optEnds(optLower(pack.path.filename().string()),".zip")||
       optEnds(optLower(pack.path.filename().string()),".mcpack"))
        return optReadZipShader(pack.path,source,origin);
    return false;
}

// Simple compatibility translation only; do NOT claim to support complete
// Iris properties, shadow buffers, gbuffers, includes or multiple passes.
static void optReplaceAll(std::string& s,std::string const& a,
                          std::string const& b) {
    size_t at=0;
    while((at=s.find(a,at))!=std::string::npos) {
        s.replace(at,a.size(),b);at+=b.size();
    }
}
static std::string optTranslateFinal(std::string source) {
    // Keep line numbers useful by removing just the first #version line.
    auto version=source.find("#version");
    if(version!=std::string::npos && (version==0||
       source[version-1]=='\n')) {
        auto end=source.find('\n',version);
        source.erase(version,end==std::string::npos?
                     std::string::npos:end-version+1);
    }
    optReplaceAll(source,"texture2D(","texture(");
    optReplaceAll(source,"textureCube(","texture(");
    optReplaceAll(source,"texture2DLod(","textureLod(");
    optReplaceAll(source,"gl_FragColor","optFragColor");
    optReplaceAll(source,"gl_FragData[0]","optFragColor");
    optReplaceAll(source,"gl_TexCoord[0]","vec4(vTexCoord,0.0,1.0)");
    optReplaceAll(source,"varying ","in ");
    return std::string("#version 300 es\n"
        "precision highp float;\nprecision highp int;\n"
        "layout(location=0) out vec4 optFragColor;\n") + source;
}

static GLuint optCompile(GLenum kind,std::string const& code,std::string& report) {
    GLuint shader=glCreateShader(kind);
    if(!shader){report+="glCreateShader failed\n";return 0;}
    const char* p=code.c_str();
    glShaderSource(shader,1,&p,nullptr);
    glCompileShader(shader);
    GLint ok=GL_FALSE;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    GLint n=0;
    glGetShaderiv(shader,GL_INFO_LOG_LENGTH,&n);
    if(n>1) {
        std::string log(static_cast<size_t>(std::min(n,8192)),'\0');
        GLsizei got=0;
        glGetShaderInfoLog(shader,GLsizei(log.size()),&got,log.data());
        log.resize(std::max(0,int(got)));
        report+=log+"\n";
    }
    if(ok!=GL_TRUE){glDeleteShader(shader);return 0;}
    return shader;
}

static bool optRenderFragment(std::string const& source,
                              std::string& diagnostics) {
    if(eglGetCurrentContext()==EGL_NO_CONTEXT) {
        diagnostics="No OpenGL ES context is available";
        return false;
    }
    optPreviewCheckContext();
    static const char* vertex=R"(#version 300 es
precision highp float;
out vec2 texcoord;
out vec2 vTexCoord;
void main(){
    vec2 pos=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    texcoord=pos;
    vTexCoord=pos;
    gl_Position=vec4(pos*2.0-1.0,0.0,1.0);
}
)";
    GLuint vs=optCompile(GL_VERTEX_SHADER,vertex,diagnostics);
    if(!vs)return false;
    GLuint fs=optCompile(GL_FRAGMENT_SHADER,optTranslateFinal(source),diagnostics);
    if(!fs){glDeleteShader(vs);return false;}
    GLuint program=glCreateProgram();
    glAttachShader(program,vs);
    glAttachShader(program,fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked=GL_FALSE;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(linked!=GL_TRUE){
        diagnostics+="GLSL link error (unsupported Iris pass interface)\n";
        GLint n=0;glGetProgramiv(program,GL_INFO_LOG_LENGTH,&n);
        if(n>1){
            std::string log(static_cast<size_t>(std::min(n,8192)),'\0');
            GLsizei got=0;glGetProgramInfoLog(program,GLsizei(log.size()),&got,log.data());
            log.resize(std::max(0,int(got)));diagnostics+=log;
        }
        glDeleteProgram(program);
        return false;
    }

    GLint savedFbo=0,savedProgram=0,savedVao=0,savedActive=0,
          savedTexture=0,savedViewport[4]{},savedPack=4;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&savedFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM,&savedProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&savedVao);
    glGetIntegerv(GL_VIEWPORT,savedViewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&savedActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&savedTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&savedPack);
    GLboolean blend=glIsEnabled(GL_BLEND),depth=glIsEnabled(GL_DEPTH_TEST),
              scissor=glIsEnabled(GL_SCISSOR_TEST);

    GLuint scene=0,fbo=0,vao=0,result=0;
    std::vector<unsigned char> gradient(kPreviewW*kPreviewH*4);
    for(int y=0;y<kPreviewH;y++)for(int x=0;x<kPreviewW;x++) {
        size_t at=4*size_t(y*kPreviewW+x);
        gradient[at+0]=static_cast<unsigned char>(x*255/(kPreviewW-1));
        gradient[at+1]=static_cast<unsigned char>(y*255/(kPreviewH-1));
        gradient[at+2]=150;gradient[at+3]=255;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glGenTextures(1,&scene);
    glBindTexture(GL_TEXTURE_2D,scene);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,kPreviewW,kPreviewH,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,gradient.data());
    glGenTextures(1,&result);
    glBindTexture(GL_TEXTURE_2D,result);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,kPreviewW,kPreviewH,0,GL_RGBA,
                 GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D,result,0);
    bool ok=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    if(ok) {
        glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);
        glViewport(0,0,kPreviewW,kPreviewH);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,scene);
        for(char const* name:{"colortex0","gcolor","texture","tex","colortex1"}) {
            GLint loc=glGetUniformLocation(program,name);
            if(loc>=0)glUniform1i(loc,0);
        }
        for(char const* name:{"viewWidth"}) {
            GLint loc=glGetUniformLocation(program,name);
            if(loc>=0)glUniform1f(loc,float(kPreviewW));
        }
        GLint height=glGetUniformLocation(program,"viewHeight");
        if(height>=0)glUniform1f(height,float(kPreviewH));
        glGenVertexArrays(1,&vao);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES,0,3);
        ok=glGetError()==GL_NO_ERROR;
        if(!ok)diagnostics+="GLES preview draw failed\n";
    }else diagnostics+="GLES offscreen framebuffer incomplete\n";
    glBindFramebuffer(GL_FRAMEBUFFER,GLuint(savedFbo));
    glUseProgram(GLuint(savedProgram));
    glBindVertexArray(GLuint(savedVao));
    glViewport(savedViewport[0],savedViewport[1],savedViewport[2],savedViewport[3]);
    if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
    if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D,GLuint(savedTexture));
    glActiveTexture(GLenum(savedActive));
    glPixelStorei(GL_UNPACK_ALIGNMENT,savedPack);
    if(vao)glDeleteVertexArrays(1,&vao);
    if(fbo)glDeleteFramebuffers(1,&fbo);
    if(scene)glDeleteTextures(1,&scene);
    glDeleteProgram(program);
    if(ok){
        if(gOptPreviewTexture)glDeleteTextures(1,&gOptPreviewTexture);
        gOptPreviewTexture=result;
        diagnostics+="GLES3 compiled and drew 1 final/composite pass to an offscreen texture.";
        return true;
    }
    if(result)glDeleteTextures(1,&result);
    return false;
}

static void optTestSelectedShader(OptPackInfo const* pack) {
    std::string src,origin;
    if(!pack) {
        origin="Built-in GLES3 diagnostic shader";
        src=R"(#version 300 es
precision highp float;
in vec2 texcoord;
uniform sampler2D colortex0;
out vec4 unusedOutput;
void main(){unusedOutput=vec4(1.0)-texture(colortex0,texcoord);unusedOutput.a=1.0;}
)";
        // A test shader must use optFragColor for the real transform pipeline.
        optReplaceAll(src,"out vec4 unusedOutput;","");
        optReplaceAll(src,"unusedOutput","optFragColor");
    }else if(!optReadJavaShader(*pack,src,origin)) {
        gOptPreviewSuccess=false;
        gOptPreviewReport="No directly testable final.fsh or composite.fsh found. "
            "Complex Iris packs need multi-pass RenderDragon integration.";
        gOptPreviewFile.clear();
        return;
    }
    gOptPreviewFile=origin;
    gOptPreviewReport.clear();
    gOptPreviewSuccess=optRenderFragment(src,gOptPreviewReport);
}
