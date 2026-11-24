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

#include "OptLeviJavaPack.inc"

static bool optReadJavaShader(OptPackInfo const& pack,
                              std::string& source,std::string& origin) {
    return optReadJavaFinalPass(pack,source,origin);
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
static void optReplaceIdentifier(std::string& source,std::string const& from,
                                 std::string const& to);

// An explicit Java 1.21.0 compatibility profile, not the Bedrock version
// number and not a claim that Iris's native resource providers exist.
static std::string optGlesCompatibilityHeader(){
    return R"GLSL(#version 300 es
#ifdef GL_EXT_shader_implicit_conversions
#extension GL_EXT_shader_implicit_conversions : enable
#endif
precision highp float;
precision highp int;
precision highp sampler2DShadow;
#ifndef MC_VERSION
#define MC_VERSION 12100
#endif
#define optTexture2DLod(s,u,l) textureLod(s,u,float(l))
#define optShadow2D(s,c) vec4(vec3(texture(s,c)),1.0)
)GLSL";
}

// Desktop GLSL 120 allows global variables initialized from uniforms and
// varyings. ES 300 requires constant global initializers. Preserve the
// declaration scope and preprocessor conditions, but execute simple
// non-const scalar/vector/matrix initializers in source order before main.
// Complex declarations are deliberately left to the compiler.
static std::string optTranslateGlobalInitializers(std::string const& source){
    std::vector<std::string> lines,masked;
    std::istringstream input(source);
    std::string line;
    bool comment=false;
    while(std::getline(input,line)){
        lines.push_back(line);
        std::string mask=line;
        for(size_t i=0;i<mask.size();++i){
            if(comment){
                if(i+1<mask.size()&&line[i]=='*'&&line[i+1]=='/'){
                    mask[i]=mask[i+1]=' ';++i;comment=false;
                }else mask[i]=' ';
            }else if(i+1<mask.size()&&line[i]=='/'&&line[i+1]=='*'){
                mask[i]=mask[i+1]=' ';++i;comment=true;
            }else if(i+1<mask.size()&&line[i]=='/'&&line[i+1]=='/'){
                mask.replace(i,mask.size()-i,mask.size()-i,' ');break;
            }
        }
        masked.push_back(std::move(mask));
    }
    auto isAlpha=[](char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_';};
    auto ident=[&](char c){return isAlpha(c)||(c>='0'&&c<='9');};
    auto skip=[](std::string const& s,size_t& p){
        while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\r'))++p;
    };
    auto word=[&](std::string const& s,size_t& p){
        skip(s,p);size_t a=p;
        if(p<s.size()&&isAlpha(s[p])){
            ++p;while(p<s.size()&&ident(s[p]))++p;
        }
        return s.substr(a,p-a);
    };
    static const std::set<std::string> types{
        "float","int","uint","bool","vec2","vec3","vec4",
        "ivec2","ivec3","ivec4","uvec2","uvec3","uvec4",
        "bvec2","bvec3","bvec4","mat2","mat3","mat4",
        "mat2x2","mat2x3","mat2x4","mat3x2","mat3x3","mat3x4",
        "mat4x2","mat4x3","mat4x4"};
    std::string body,calls;
    unsigned count=0;
    int depth=0;
    std::vector<int> branchDepth;
    bool continuation=false;
    for(size_t i=0;i<lines.size();++i){
        auto const& mask=masked[i];
        size_t at=0;skip(mask,at);
        if(continuation||(at<mask.size()&&mask[at]=='#')){
            if(!continuation){
                ++at;auto directive=word(mask,at);
                if(directive=="if"||directive=="ifdef"||directive=="ifndef")
                    branchDepth.push_back(depth);
                else if((directive=="else"||directive=="elif")&&!branchDepth.empty())
                    depth=branchDepth.back();
                else if(directive=="endif"&&!branchDepth.empty())branchDepth.pop_back();
            }
            auto end=mask.find_last_not_of(" \t\r");
            continuation=end!=std::string::npos&&mask[end]=='\\';
            body+=lines[i]+"\n";continue;
        }
        if(depth==0&&count<512){
            at=0;auto type=word(mask,at);
            auto name=word(mask,at);skip(mask,at);
            if(types.count(type)&&!name.empty()&&at<mask.size()&&mask[at]=='='&&
               (at+1==mask.size()||mask[at+1]!='=')){
                size_t start=at+1,j=i,end=std::string::npos;
                int parentheses=0;
                bool bad=false;
                // The original shader contains multi-line expressions. Only
                // consume a single complete declaration, never a later one.
                for(;j<lines.size()&&j-i<128&&!bad;++j){
                    auto const& part=masked[j];
                    size_t q=j==i?start:0;
                    size_t first=q;skip(part,first);
                    if(first<part.size()&&part[first]=='#')continue;
                    for(;q<part.size();++q){
                        char c=part[q];
                        if(c=='('||c=='[')++parentheses;
                        else if(c==')'||c==']')--parentheses;
                        else if(c=='{'||c=='}'||(c==','&&parentheses==0)){bad=true;break;}
                        else if(c==';'&&parentheses==0){end=q;break;}
                        if(parentheses<0){bad=true;break;}
                    }
                    if(end!=std::string::npos)break;
                }
                if(!bad&&j<lines.size()&&end!=std::string::npos&&
                   masked[j].find_first_not_of(" \t\r",end+1)==std::string::npos){
                    std::string expr;
                    for(size_t k=i;k<=j;++k){
                        size_t a=k==i?start:0;
                        size_t b=k==j?end:lines[k].size();
                        expr+=lines[k].substr(a,b-a);
                        if(k!=j)expr+='\n';
                    }
                    auto id=std::to_string(count++);
                    body+=type+" "+name+";\nvoid optGlobalInit"+id+"(){\n"+
                          name+"="+expr+";\n}\n#define OPTLEVI_INIT_"+id+" 1\n";
                    // Preserve trailing comments, including a multi-line
                    // block comment that begins after the semicolon.
                    body+=lines[j].substr(end+1)+"\n";
                    calls+="#ifdef OPTLEVI_INIT_"+id+"\noptGlobalInit"+id+
                           "();\n#endif\n";
                    i=j;continue;
                }
            }
        }
        for(char c:mask){if(c=='{')++depth;else if(c=='}')--depth;}
        body+=lines[i]+"\n";
    }
    if(!count)return source;
    optReplaceIdentifier(body,"main","optLeviGlobalMain");
    return body+"\nvoid main(){\n"+calls+"optLeviGlobalMain();\n}\n";
}

// Narrow compatibility profile for BSL's v10 desktop GLSL. GLES requires
// explicit int-to-float conversion where desktop GLSL allows it. These
// rules preserve the integer uniforms, loop counters and preprocessor
// settings; they only change the known mixed-type arithmetic sites.
// Unknown packs and unmatched expressions are left to the compiler.
static std::string optTranslateBslNumeric(std::string source){
    if(source.find("BSL Shaders v10 Series")==std::string::npos)return source;
    static const std::pair<char const*,char const*> rules[]={
        {"eyeBrightnessSmooth.y / 240.0","float(eyeBrightnessSmooth.y) / 240.0"},
        {"isEyeInWater == 1.0","float(isEyeInWater) == 1.0"},
        {"frameCounter * 0.618","float(frameCounter) * 0.618"},
        {"exp2(i + dither)","exp2(float(i) + dither)"},
        {"0.0512 / shadowMapResolution","0.0512 / float(shadowMapResolution)"},
        {"noVignetteColor * RETRO_FILTER_DEPTH","noVignetteColor * float(RETRO_FILTER_DEPTH)"},
        {"/ RETRO_FILTER_DEPTH","/ float(RETRO_FILTER_DEPTH)"},
        {"testCoord.x > -2","testCoord.x > -2.0"},
        {"testCoord.x < 3","testCoord.x < 3.0"},
        {"testCoord.y > 0","testCoord.y > 0.0"},
        {"testCoord.y < 1","testCoord.y < 1.0"},
        {"1.0 - isEyeInWater","1.0 - float(isEyeInWater)"},
        {"worldPos.y + cameraPosition.y - FOG_HEIGHT_Y","worldPos.y + cameraPosition.y - float(FOG_HEIGHT_Y)"},
        {"CLOUD_DENSITY * 0.125","float(CLOUD_DENSITY) * 0.125"},
        {"1.0 / samples","1.0 / float(samples)"},
        {"* CLOUD_DENSITY * 0.5","* float(CLOUD_DENSITY) * 0.5"},
        {"currentStep * CLOUD_THICKNESS","currentStep * float(CLOUD_THICKNESS)"},
        {"float cloudThickness = CLOUD_THICKNESS","float cloudThickness = float(CLOUD_THICKNESS)"},
        {"furthestPlane < 0","furthestPlane < 0.0"},
        {"min(planeDifference / sampleLength, maxSamples) + 1","min(planeDifference / sampleLength, float(maxSamples)) + 1.0"},
        {"(moonNormalX * moonNormalX + moonNormalY * moonNormalY) > 1","(moonNormalX * moonNormalX + moonNormalY * moonNormalY) > 1.0"},
    };
    auto token=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='_'||c=='.';};
    for(auto const& rule:rules){
        std::string from=rule.first,to=rule.second;
        size_t at=0;
        while((at=source.find(from,at))!=std::string::npos){
            size_t end=at+from.size();
            // Do not turn an already-correct 0.0 into 0.0.0, or match
            // an identifier prefix, a longer numeric literal, or a swizzle.
            if((token(from.front())&&at&&token(source[at-1]))||
               (token(from.back())&&end<source.size()&&token(source[end]))){
                at=end;continue;
            }
            source.replace(at,from.size(),to);at+=to.size();
        }
    }
    return source;
}

static std::string optTranslateFinal(std::string source,bool originalVertex=false) {
    // Translate desktop GLSL 120 directives to GLES3 without claiming full Iris.
    std::istringstream lines(source);
    std::string line,clean;
    while(std::getline(lines,line)) {
        size_t start=line.find_first_not_of(" \t\r");
        if(start!=std::string::npos &&
           (line.compare(start,8,"#version")==0 ||
            line.compare(start,36,"#extension GL_ARB_shader_texture_lod")==0)) {
            clean+='\n';
            continue;
        }
        clean+=line;
        clean+='\n';
    }
    source=std::move(clean);
    optReplaceAll(source,"texture2D(","texture(");
    optReplaceAll(source,"textureCube(","texture(");
    optReplaceAll(source,"texture2DLod(","optTexture2DLod(");
    optReplaceAll(source,"shadow2D(","optShadow2D(");
    optReplaceAll(source,"gl_FragColor","optFragColor");
    optReplaceAll(source,"gl_FragData[0]","optFragColor");
    optReplaceAll(source,"gl_TexCoord[0]",originalVertex?"optLegacyTexCoord":"vec4(vTexCoord,0.0,1.0)");
    optReplaceAll(source,"varying ","in ");
    return optGlesCompatibilityHeader()+
        "layout(location=0) out vec4 optFragColor;\n"+
        (originalVertex?"in vec4 optLegacyTexCoord;\n":"")+
        optTranslateGlobalInitializers(optTranslateBslNumeric(std::move(source)));
}

// RenderDragonShaderLoader's input mapping catalog distinguishes pipeline
// roles (colortexN) from actual native attachments. A Java pass is not
// permitted to invent scene attachments; the runtime checks reflected samplers
// against the buffers produced by earlier passes.
// Source order and target location follow Iris composite/DRAWBUFFERS rules.
static bool optParseCompositeTargets(std::string const& source,
                                     std::vector<unsigned>& targets,
                                     std::string& error) {
    // Only recognized comment directives are used. GLSL preprocessor-dependent
    // directives are not silently guessed.
    auto rt=source.find("RENDERTARGETS:");
    auto db=source.find("DRAWBUFFERS:");
    if(rt!=std::string::npos && db!=std::string::npos){
        error="Both RENDERTARGETS and DRAWBUFFERS are present";return false;
    }
    size_t pos=rt!=std::string::npos?rt:db;
    targets.clear();
    if(pos==std::string::npos){targets.push_back(0);return true;}
    bool modern=rt!=std::string::npos;
    pos+=modern?14:12;
    auto end=source.find_first_of("\r\n*",pos);
    if(end==std::string::npos)end=source.size();
    auto token=source.substr(pos,end-pos);
    std::set<unsigned> used;
    if(modern){
        std::istringstream in(token);std::string item;
        while(std::getline(in,item,',')){
            auto first=item.find_first_not_of(" \t");
            auto last=item.find_last_not_of(" \t");
            if(first==std::string::npos || last-first>1){
                error="Only explicit colortex0..15 render targets are supported";return false;
            }
            unsigned target=0;
            for(size_t i=first;i<=last;++i){
                if(item[i]<'0'||item[i]>'9'){error="Invalid render target";return false;}
                target=target*10+unsigned(item[i]-'0');
            }
            if(target>=16){error="Only colortex0..15 render targets are supported";return false;}
            if(!used.insert(target).second){error="Repeated render target";return false;}
            targets.push_back(target);
        }
    } else {
        for(char ch:token){
            if(ch==' '||ch=='\t')continue;
            if(ch<'0'||ch>'9'){error="Unsupported DRAWBUFFERS target";return false;}
            auto target=unsigned(ch-'0');
            if(!used.insert(target).second){error="Repeated render target";return false;}
            targets.push_back(target);
        }
    }
    if(targets.empty()){error="Empty render target directive";return false;}
    return true;
}

static std::string optTranslateComposite(std::string source,size_t outputs,bool originalVertex=false) {
    // optTranslateFinal handles legacy desktop syntax and removes #version.
    // Introduce separate fragment outputs for locations 1..N, not aliases.
    source=optTranslateFinal(std::move(source),originalVertex);
    std::string extra;
    for(size_t i=1;i<outputs;++i){
        auto name="optFragColor"+std::to_string(i);
        optReplaceAll(source,"gl_FragData["+std::to_string(i)+"]",name);
        extra+="layout(location="+std::to_string(i)+") out vec4 "+name+";\n";
    }
    auto at=source.find("layout(location=0)");
    if(at!=std::string::npos)source.insert(at,extra);
    return source;
}

// Legacy fixed-function inputs for a Java fullscreen quad only. These are
// screen-space coordinates, NOT fabricated Minecraft camera/geometry matrices.
static void optReplaceIdentifier(std::string& source,std::string const& from,
                                 std::string const& to) {
    auto ident=[](char ch){return (ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
                                  (ch>='0'&&ch<='9')||ch=='_';};
    size_t pos=0;
    while((pos=source.find(from,pos))!=std::string::npos){
        size_t end=pos+from.size();
        if((pos==0||!ident(source[pos-1]))&&(end==source.size()||!ident(source[end]))){
            source.replace(pos,from.size(),to);pos+=to.size();
        }else pos=end;
    }
}
static std::string optTranslateFullscreenVertex(std::string source) {
    std::istringstream input(source);
    std::string line,clean;
    while(std::getline(input,line)){
        auto at=line.find_first_not_of(" \t\r");
        if(at!=std::string::npos&&(line.compare(at,8,"#version")==0||
           line.compare(at,36,"#extension GL_ARB_shader_texture_lod")==0)){
            clean+='\n';continue;
        }
        clean+=line;clean+='\n';
    }
    optReplaceIdentifier(clean,"varying","out");
    optReplaceIdentifier(clean,"attribute","in");
    optReplaceAll(clean,"gl_TexCoord[0]","optLegacyTexCoord");
    optReplaceAll(clean,"gl_TextureMatrix[0]","mat4(1.0)");
    optReplaceIdentifier(clean,"gl_Vertex","optFullscreenPosition()");
    optReplaceIdentifier(clean,"gl_MultiTexCoord0","optFullscreenPosition()");
    optReplaceIdentifier(clean,"gl_ModelViewProjectionMatrix","optScreenProjection");
    optReplaceIdentifier(clean,"gl_ProjectionMatrix","optScreenProjection");
    optReplaceIdentifier(clean,"gl_ModelViewMatrix","mat4(1.0)");
    optReplaceIdentifier(clean,"ftransform","optFullscreenTransform");
    optReplaceAll(clean,"texture2D(","texture(");
    optReplaceAll(clean,"texture2DLod(","optTexture2DLod(");
    optReplaceAll(clean,"shadow2D(","optShadow2D(");
    return optGlesCompatibilityHeader()+std::string(R"GLSL(out vec4 optLegacyTexCoord;
const mat4 optScreenProjection=mat4(
    2.0,0.0,0.0,0.0, 0.0,2.0,0.0,0.0,
    0.0,0.0,1.0,0.0, -1.0,-1.0,0.0,1.0);
vec4 optFullscreenPosition(){
    const vec2 corners[6]=vec2[6](
        vec2(0.0,0.0),vec2(1.0,0.0),vec2(1.0,1.0),
        vec2(0.0,0.0),vec2(1.0,1.0),vec2(0.0,1.0));
    return vec4(corners[gl_VertexID],0.0,1.0);
}
vec4 optFullscreenTransform(){return optScreenProjection*optFullscreenPosition();}
)GLSL")+optTranslateGlobalInitializers(optTranslateBslNumeric(std::move(clean)));
}

// Preserve the original conditional branches around routing comments. Turn
// each route into a macro assignment and emit only the selected output
// declaration after the pack's source has been preprocessed by the GLES driver.
static bool optTranslateRoutedComposite(std::string source,bool originalVertex,
                                        std::string& translated,std::string& error){
    std::vector<std::vector<unsigned>> routes{{0}};
    std::istringstream lines(source);
    std::string line,body="#define OPTLEVI_DRAW_ROUTE 0\n";
    size_t outputs=1;
    while(std::getline(lines,line)){
        auto first=line.find_first_not_of(" \t\r");
        auto last=line.find_last_not_of(" \t\r");
        bool route=false;
        if(first!=std::string::npos&&line.compare(first,2,"/*")==0&&last>first+2&&
           line.compare(last-1,2,"*/")==0){
            auto start=line.find_first_not_of(" \t",first+2);
            route=start!=std::string::npos&&
                  (line.compare(start,12,"DRAWBUFFERS:")==0||
                   line.compare(start,14,"RENDERTARGETS:")==0);
        }
        if(route){
            std::vector<unsigned> targets;
            if(!optParseCompositeTargets(line,targets,error))return false;
            if(routes.size()>=64){error="Too many conditional render-target mappings";return false;}
            outputs=std::max(outputs,targets.size());
            body+="#undef OPTLEVI_DRAW_ROUTE\n#define OPTLEVI_DRAW_ROUTE "+
                  std::to_string(routes.size())+"\n";
            routes.push_back(std::move(targets));
        }else{
            body+=line;body+='\n';
        }
    }
    // The pack writes temporary globals, not fixed hardware outputs. The
    // wrapper copies them into the active, explicitly named output locations.
    translated=optTranslateComposite(std::move(body),outputs,originalVertex);
    optReplaceIdentifier(translated,"main","optLeviShaderMain");
    auto temp=[](size_t i){return std::string("optFragColor")+(i?std::to_string(i):"");};
    for(size_t i=0;i<outputs;++i){
        auto name=temp(i);
        optReplaceAll(translated,"layout(location="+std::to_string(i)+") out vec4 "+name+";",
                      "vec4 "+name+";");
    }
    translated+='\n';
    for(size_t i=0;i<routes.size();++i){
        translated+=(i?"#elif ":"#if ");
        translated+="OPTLEVI_DRAW_ROUTE == "+std::to_string(i)+"\n";
        for(size_t slot=0;slot<routes[i].size();++slot)
            translated+="layout(location="+std::to_string(slot)+") out vec4 optRouteTarget"+
                        std::to_string(routes[i][slot])+";\n";
        translated+="void main(){optLeviShaderMain();\n";
        for(size_t slot=0;slot<routes[i].size();++slot)
            translated+="optRouteTarget"+std::to_string(routes[i][slot])+"="+temp(slot)+";\n";
        translated+="}\n";
    }
    translated+="#endif\n";
    return true;
}


// Use the driver preprocessor to resolve shader option flags in the same
// expanded fragment source and Java profile. Only boolean option expressions
// are supported; fail closed on other property expression syntax.
static bool optBuildEnabledProbe(std::string const& source,std::string const& expression,
                                 std::string& probe,std::string& error){
    std::string condition;
    // An absent property enables the pass; mipmap directives may still
    // require metadata reflection even without a program.enabled rule.
    if(expression.empty())condition="1";
    auto alpha=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
    auto digit=[](char c){return c>='0'&&c<='9';};
    for(size_t i=0;i<expression.size();){
        char c=expression[i];
        if(c==' '||c=='\t'||c=='\r'){++i;continue;}
        if(alpha(c)){
            size_t begin=i++;
            while(i<expression.size()&&(alpha(expression[i])||digit(expression[i])))++i;
            auto name=expression.substr(begin,i-begin);
            if(name=="true")condition+=" 1 ";
            else if(name=="false")condition+=" 0 ";
            else condition+=" defined("+name+") ";
        }else if(c=='!'||c=='('||c==')'||c=='0'||c=='1'){
            condition+=' ';condition+=c;condition+=' ';++i;
        }else if((c=='&'||c=='|')&&i+1<expression.size()&&expression[i+1]==c){
            condition+=' ';condition+=expression.substr(i,2);condition+=' ';i+=2;
        }else{error="Unsupported program.enabled expression: "+expression;return false;}
    }
    if(condition.empty()||condition.size()>4096){error="Empty or oversized program.enabled expression";return false;}
    // Strip comments before collecting directives, including continuation
    // lines. No executable GLSL from a disabled program is compiled.
    std::istringstream input(source);std::string line,body;
    bool block=false,continuation=false,hasMipmap=false;
    while(std::getline(input,line)){
        for(size_t i=0;i<line.size();++i){
            if(block){
                if(i+1<line.size()&&line[i]=='*'&&line[i+1]=='/'){
                    line[i]=line[i+1]=' ';++i;block=false;
                }else line[i]=' ';
            }else if(i+1<line.size()&&line[i]=='/'&&line[i+1]=='*'){
                line[i]=line[i+1]=' ';++i;block=true;
            }else if(i+1<line.size()&&line[i]=='/'&&line[i+1]=='/'){
                line.resize(i);break;
            }
        }
        auto first=line.find_first_not_of(" \t\r");
        bool include=continuation;
        if(!continuation&&first!=std::string::npos&&line[first]=='#'){
            auto start=line.find_first_not_of(" \t",first+1);
            if(start!=std::string::npos){
                size_t end=start;
                while(end<line.size()&&alpha(line[end]))++end;
                auto word=line.substr(start,end-start);
                include=word=="define"||word=="undef"||word=="if"||word=="ifdef"||
                    word=="ifndef"||word=="elif"||word=="else"||word=="endif"||word=="error";
            }
        }
        if(!include){
            continuation=false;
            // Iris/OptiFine pass-local constants control mip generation.
            // Turn declarations into metadata macros in the same conditional
            // branches; never activate constants hidden in comments.
            std::string compact;
            for(char c:line)if(c!=' '&&c!='\t'&&c!='\r')compact+=c;
            for(unsigned n=0;n<16;++n){
                std::string canonical="constboolcolortex"+std::to_string(n)+"MipmapEnabled=";
                std::string legacy;
                if(n<4){
                    static const char* names[]={"gcolor","gdepth","gnormal","composite"};
                    legacy=std::string("constbool")+names[n]+"MipmapEnabled=";
                }else if(n<8)legacy="constboolgaux"+std::to_string(n-3)+"MipmapEnabled=";
                bool yes=compact==canonical+"true;"||(!legacy.empty()&&compact==legacy+"true;");
                bool no=compact==canonical+"false;"||(!legacy.empty()&&compact==legacy+"false;");
                if(yes||no){
                    hasMipmap=true;
                    std::string marker="OPTLEVI_MIP_INPUT_"+std::to_string(n);
                    body+="#undef "+marker+"\n";
                    if(yes)body+="#define "+marker+" 1\n";
                    break;
                }
            }
            continue;
        }
        auto last=line.find_last_not_of(" \t\r");
        continuation=last!=std::string::npos&&line[last]=='\\';
        body+=line+'\n';
    }
    if(block||continuation){error="Unterminated directive in program.enabled probe";return false;}
    if(expression.empty()&&!hasMipmap){probe.clear();return true;}
    std::string declarations,additions;
    for(unsigned n=0;n<16;++n){
        auto index=std::to_string(n);
        auto guard="#ifdef OPTLEVI_MIP_INPUT_"+index+"\n";
        declarations+=guard+"uniform float optMipInput"+index+";\n#endif\n";
        additions+=guard+"value+=optMipInput"+index+";\n#endif\n";
    }
    probe=optGlesCompatibilityHeader()+body+"\n#if "+condition+"\n"+declarations+
        "layout(location=0) out vec4 optEnabledOutput;\nvoid main(){float value=1.0;\n"+
        additions+"optEnabledOutput=vec4(value);}\n"
        "#else\nlayout(location=0) out vec4 optDisabledOutput;\nvoid main(){optDisabledOutput=vec4(0.0);}\n#endif\n";
    return true;
}

// Attachment format directives are metadata, including the conventional
// block-comment form used by BSL. Preserve the declared precision rather
// than silently reducing all intermediate images to RGBA8.
static bool optReadColorFormats(std::string const& source,
                                std::vector<std::pair<unsigned,std::string>>& formats,
                                std::string& error){
    std::istringstream input(source);
    std::string line;
    while(std::getline(input,line)){
        auto comment=line.find("//");
        if(comment!=std::string::npos)line.resize(comment);
        std::string compact;
        for(char c:line)if(c!=' '&&c!='\t'&&c!='\r')compact+=c;
        if(compact.rfind("constint",0)!=0)continue;
        auto eq=compact.find("Format=",8);
        if(eq==std::string::npos||compact.empty()||compact.back()!=';')continue;
        auto name=compact.substr(8,eq-8);
        auto format=compact.substr(eq+7,compact.size()-eq-8);
        int index=-1;
        if(name.rfind("colortex",0)==0&&name.size()>8&&name.size()<=10){
            unsigned n=0;bool valid=true;
            for(size_t i=8;i<name.size();++i){
                if(name[i]<'0'||name[i]>'9'){valid=false;break;}
                n=n*10+unsigned(name[i]-'0');
            }
            if(valid&&n<16)index=int(n);
        }else{
            static char const* aliases[]={"gcolor","gdepth","gnormal","composite",
                "gaux1","gaux2","gaux3","gaux4"};
            for(unsigned i=0;i<8;++i)if(name==aliases[i])index=int(i);
        }
        // A declaration of an unused extra attachment is harmless. Actual
        // references beyond the backend limit still reject at reflection.
        if(index<0)continue;
        bool valid=!format.empty()&&format.size()<=32;
        for(char c:format)if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))valid=false;
        if(!valid){error="Malformed attachment format: "+name;return false;}
        bool duplicate=false;
        for(auto const& old:formats)if(old.first==unsigned(index)){
            if(old.second!=format){
                error="Conflicting attachment formats for colortex"+std::to_string(index);return false;
            }
            duplicate=true;break;
        }
        if(!duplicate)formats.emplace_back(unsigned(index),std::move(format));
    }
    return true;
}

// Decode only explicit pack-owned PNG resources. ZIP member reads are
// separately bounded from shader source reads; decoded dimensions are
// checked before allocation. No placeholder noise/geometry image is made.
static bool optLoadCompositeTextures(
    OptPackInfo const& pack,std::string const& properties,
    std::vector<modmenu::android::shaderworld::PackTexture>& images,
    std::string& error){
    std::istringstream input(properties);
    std::string line;
    std::set<std::string> used;
    size_t total=0;
    auto trim=[](std::string const& s){
        auto first=s.find_first_not_of(" \t\r");
        if(first==std::string::npos)return std::string{};
        return s.substr(first,s.find_last_not_of(" \t\r")-first+1);
    };
    while(std::getline(input,line)){
        auto eq=line.find('=');
        if(eq==std::string::npos)continue;
        auto key=trim(line.substr(0,eq));
        std::string sampler;
        bool noise=key=="texture.noise";
        if(noise)sampler="noisetex";
        else if(key.rfind("texture.composite.",0)==0)
            sampler=key.substr(std::string("texture.composite.").size());
        else continue;
        auto path=trim(line.substr(eq+1));
        bool valid=!sampler.empty()&&sampler.size()<64;
        for(char c:sampler)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                                  (c>='0'&&c<='9')||c=='_'))valid=false;
        if(!valid||!used.insert(sampler).second||images.size()>=8||
           path.empty()||path[0]=='/'||!optEnds(optLower(path),".png")){
            error="Unsupported composite texture property: "+key;return false;
        }
        std::string bytes;
        if(!optReadJavaMember(pack,"shaders/"+path,bytes,4*1024*1024)){
            error="Cannot read pack texture: "+path;return false;
        }
        auto* raw=reinterpret_cast<unsigned char const*>(bytes.data());
        int width=0,height=0,channels=0;
        if(!stbi_info_from_memory(raw,int(bytes.size()),&width,&height,&channels)||
           width<=0||height<=0||width>2048||height>2048||
           std::uint64_t(width)*height*4+total>16ull*1024*1024){
            error="Invalid or oversized pack PNG: "+path;return false;
        }
        auto* pixels=stbi_load_from_memory(raw,int(bytes.size()),&width,&height,&channels,4);
        if(!pixels){error="Cannot decode pack PNG: "+path;return false;}
        size_t size=size_t(width)*height*4;
        modmenu::android::shaderworld::PackTexture image;
        image.sampler=std::move(sampler);image.width=width;image.height=height;
        image.nearest=noise;
        image.rgba.assign(pixels,pixels+size);
        stbi_image_free(pixels);
        total+=size;images.push_back(std::move(image));
    }
    return true;
}

static bool optBuildJavaPipeline(
    OptPackInfo const& pack,
    std::vector<modmenu::android::shaderworld::Pass>& passes,
    std::string& error) {
    using modmenu::android::shaderworld::Pass;
    OptJavaArchiveScope archiveScope;
    passes.clear();
    gOptJavaSourceError.clear();
    // An Iris shader can include arbitrary geometry stages. We implement
    // the post-render composite subset only. Do not claim a G-buffer
    // pipeline merely because some post passes happen to compile.
    // Prefer dimension-specific programs, then the root program.
    // Do not select Nether or End while using the Overworld adapter.
    auto resolve=[&](std::string const& name,std::string& member,std::string& raw){
        for(auto const& prefix:{std::string("shaders/world0/"),std::string("shaders/")}){
            auto candidate=prefix+name+".fsh";
            if(optReadJavaMember(pack,candidate,raw)){
                member=std::move(candidate);return true;
            }
        }
        return false;
    };
    // Buffer flip overrides, program scales and per-buffer dimensions require
    // distinct semantics. Reject those rather than presenting an invalid graph.
    std::string properties;
    std::vector<std::pair<std::string,std::string>> enabledRules;
    if(optReadJavaMember(pack,"shaders/shaders.properties",properties)){
        std::istringstream input(properties);std::string line;
        while(std::getline(input,line)){
            auto at=line.find_first_not_of(" \t\r");
            if(at==std::string::npos||line[at]=='#')continue;
            auto eq=line.find('=',at);
            if(eq!=std::string::npos){
                auto last=line.find_last_not_of(" \t\r",eq-1);
                auto key=line.substr(at,last-at+1);
                if(key.rfind("program.",0)==0&&optEnds(key,".enabled")){
                    enabledRules.emplace_back(key,line.substr(eq+1));
                    continue;
                }
            }
            for(auto const* prefix:{"flip.","size.buffer.","scale.composite",
                                    "blend.composite","program.composite","program.world0.composite"}){
                if(line.compare(at,std::strlen(prefix),prefix)==0){
                    error="Unsupported Java pipeline property: "+line.substr(at,120);
                    return false;
                }
            }
        }
    }
    size_t total=0;
    std::vector<std::pair<unsigned,std::string>> colorFormats;
    auto append=[&](std::string const& name,bool finalPass)->bool {
        std::string member,raw;
        if(!resolve(name,member,raw)){
            if(finalPass){error="Missing Overworld final.fsh";return false;}
            return true;
        }
        std::string expanded;std::set<std::string> stack;size_t bytes=0;
        if(!optExpandJavaMember(pack,member,expanded,stack,bytes,0)){
            error=gOptJavaSourceError;return false;
        }
        if(!optReadColorFormats(expanded,colorFormats,error)){error=member+": "+error;return false;}
        std::string expression,probe;
        bool hasRule=false;
        // Dimension-specific properties override root defaults.
        for(auto const& rule:enabledRules)
            if(rule.first=="program."+name+".enabled"){expression=rule.second;hasRule=true;}
        for(auto const& rule:enabledRules)
            if(rule.first=="program.world0/"+name+".enabled"){expression=rule.second;hasRule=true;}
        if(hasRule&&finalPass){error="Conditional final pass is not supported";return false;}
        if(hasRule&&expression.find_first_not_of(" \t\r")==std::string::npos){
            error=member+": empty program.enabled expression";return false;
        }
        if(!optBuildEnabledProbe(expanded,expression,probe,error)){
            error=member+": "+error;return false;
        }
        // Static fallback target for final/diagnostic passes. Composite
        // targets are reflected after the driver evaluates conditional GLSL.
        std::vector<unsigned> targets{0};
        // Resolve the vertex companion from the SAME dimension/program as
        // the fragment. Its include tree is independent, as in Java.
        std::string vertexMember=member.substr(0,member.size()-4)+".vsh";
        std::string vertexRaw,vertex;
        if(optReadJavaMember(pack,vertexMember,vertexRaw)){
            std::string vertexExpanded;
            std::set<std::string> vertexStack;size_t vertexBytes=0;
            if(!optExpandJavaMember(pack,vertexMember,vertexExpanded,vertexStack,vertexBytes,0)){
                error=gOptJavaSourceError;return false;
            }
            if(!optReadColorFormats(vertexExpanded,colorFormats,error)){error=vertexMember+": "+error;return false;}
            vertex=optTranslateFullscreenVertex(std::move(vertexExpanded));
        }
        std::string translated;
        if(finalPass)translated=optTranslateComposite(std::move(expanded),1,!vertex.empty());
        else if(!optTranslateRoutedComposite(std::move(expanded),!vertex.empty(),translated,error)){
            error=member+": "+error;return false;
        }
        total+=translated.size()+vertex.size()+probe.size();
        if(total>4*1024*1024 || passes.size()>=24){
            error="Java composite graph exceeds source/pass budget";return false;
        }
        passes.push_back(Pass{member,std::move(translated),std::move(targets),finalPass,std::move(vertex),!finalPass});
        passes.back().enabledProbe=std::move(probe);
        return true;
    };
    // Ordered composite and composite1...composite22, then final. Deferred,
    // shadow and geometry are not appended to the wrong render phase.
    if(!append("composite",false))return false;
    for(unsigned i=1;i<=22;++i)
        if(!append("composite"+std::to_string(i),false))return false;
    if(!append("final",true))return false;
    passes.front().colorFormats=std::move(colorFormats);
    if(!optLoadCompositeTextures(pack,properties,passes.front().packTextures,error)){
        passes.clear();return false;
    }
    return !passes.empty();
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
out vec2 texCoord;
out vec2 vTexCoord;
void main(){
    vec2 pos=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));
    texcoord=pos;
    texCoord=pos;
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
        GLint aspect=glGetUniformLocation(program,"aspectRatio");
        if(aspect>=0)glUniform1f(aspect,float(kPreviewW)/float(kPreviewH));
        GLint time=glGetUniformLocation(program,"frameTimeCounter");
        if(time>=0)glUniform1f(time,0.0f);
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
        gOptPreviewReport=gOptJavaSourceError.empty()?
            "No directly testable final/composite shader found":gOptJavaSourceError;
        gOptPreviewFile.clear();
        return;
    }
    gOptPreviewFile=origin;
    gOptPreviewReport.clear();
    gOptPreviewSuccess=optRenderFragment(src,gOptPreviewReport);
}
