#!/usr/bin/env python3
"""Compile the real OptLevi Java GLSL pack reader on Linux and exercise ZIP includes.

This tests source parsing independently of GLES and does not claim a working
Minecraft rendering backend. Synthetic fixtures only; no third-party shaders.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZipFile, ZIP_DEFLATED
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <zlib.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

struct OptPackInfo {
    std::filesystem::path path;
    bool folder = false;
};
static constexpr size_t kMaxShaderBytes = 256*1024;
static constexpr size_t kMaxZipBytes = 64*1024*1024;
static std::string optLower(std::string s) {
    for(char& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return s;
}
static bool optEnds(std::string const& s,char const* extension) {
    size_t n=std::strlen(extension);
    return s.size()>=n && s.compare(s.size()-n,n,extension)==0;
}
static unsigned optLE16(unsigned char const* p) {
    return unsigned(p[0])|(unsigned(p[1])<<8);
}
static unsigned optLE32(unsigned char const* p) {
    return optLE16(p)|(optLE16(p+2)<<16);
}
static bool optReadBounded(std::filesystem::path const& path,
                           std::string& out,size_t limit) {
    std::error_code ec;
    auto n=std::filesystem::file_size(path,ec);
    if(ec||!n||n>limit)return false;
    std::ifstream in(path,std::ios::binary);
    if(!in)return false;
    out.resize(size_t(n));
    in.read(out.data(),std::streamsize(out.size()));
    return size_t(in.gcount())==out.size();
}
#include "src/ui/OptLeviJavaPack.inc"
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    OptPackInfo p;
    p.path=argv[1];
    p.folder=std::string(argv[2])=="folder";
    std::string src,origin;
    if(!optReadJavaFinalPass(p,src,origin)) {
        std::cerr << gOptJavaSourceError << "\n";
        return 3;
    }
    std::cout << "ORIGIN=" << origin << "\n" << src;
}
'''
ENTRIES = {
    "wrapper/shaders/world-1/final.fsh": "#define NETHER\n// bad dimension\n",
    "wrapper/shaders/world0/final.fsh": (
        "#version 120\n#define FSH\n"
        '#include "/program/final.glsl"\n'
    ),
    "wrapper/shaders/program/final.glsl": (
        '#include "/lib/settings.glsl"\n'
        "varying vec2 texCoord;\n"
        "uniform sampler2D colortex1;\n"
        "void main(){gl_FragColor=texture2D(colortex1,texCoord);}\n"
    ),
    "wrapper/shaders/lib/settings.glsl": "#define BSL_TEST_INCLUDED 1\n",
}

def archive(path, items):
    with ZipFile(path, "w", ZIP_DEFLATED) as z:
        for name, code in items.items():
            z.writestr(name, code)

with TemporaryDirectory() as d:
    tmp = Path(d)
    src = tmp / "harness.cpp"
    exe = tmp / "harness"
    src.write_text(HARNESS)
    subprocess.run(
        ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-O0",
         "-I", str(ROOT), str(src), "-lz", "-o", str(exe)],
        check=True,
    )
    good = tmp / "good.zip"
    archive(good, ENTRIES)
    r = subprocess.run([str(exe), str(good), "zip"], capture_output=True, text=True, check=True)
    assert "ORIGIN=shaders/world0/final.fsh" in r.stdout, r.stdout[:1000]
    assert "#define BSL_TEST_INCLUDED 1" in r.stdout
    assert "uniform sampler2D colortex1;" in r.stdout
    assert "#include" not in r.stdout
    assert "NETHER" not in r.stdout

    # Test a directory shaderpack as well as the imported .zip format.
    folder = tmp / "unpacked"
    for name, body in ENTRIES.items():
        p = folder / name.removeprefix("wrapper/")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(body)
    r = subprocess.run([str(exe), str(folder), "folder"], capture_output=True, text=True, check=True)
    assert "ORIGIN=shaders/world0/final.fsh" in r.stdout

    # Do not permit shader includes to leave shaders/.
    bad = dict(ENTRIES)
    bad["wrapper/shaders/world0/final.fsh"] = '#include "../../outside.glsl"\n'
    archive(tmp / "bad.zip", bad)
    r = subprocess.run([str(exe), str(tmp / "bad.zip"), "zip"], capture_output=True, text=True)
    assert r.returncode == 3 and "Unsafe GLSL #include path" in r.stderr

    # Do not allow recursive include cycles.
    cycle = dict(ENTRIES)
    cycle["wrapper/shaders/lib/settings.glsl"] = '#include "/program/final.glsl"\n'
    archive(tmp / "cycle.zip", cycle)
    r = subprocess.run([str(exe), str(tmp / "cycle.zip"), "zip"], capture_output=True, text=True)
    assert r.returncode == 3 and "depth/cycle" in r.stderr

# Exercise the actual C++ translation helpers from the renderer include,
# without requiring Android or copying copyrighted shader pack contents.
with TemporaryDirectory() as d:
    tmp = Path(d)
    code = (ROOT / "src/ui/OptLeviShaderGlesTest.inc").read_text()
    begin = code.index("static void optReplaceAll(")
    end = code.index("static bool optLoadCompositeTextures(")
    (tmp / "translate.inc").write_text(code[begin:end])
    (tmp / "translate.cpp").write_text(r'''
#include <algorithm>
#include <cassert>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
#include "translate.inc"
int main(){
    std::string legacy=R"(#version 120
#define FEATURE 1
uniform float frameTime;
float a = frameTime * 2.0;
#if FEATURE
float b = a + 1.0;
#else
float b = a + 2.0;
#endif
vec2 c = vec2(
    a, b);
void main(){
  float local = 1.0;
  /*DRAWBUFFERS:01*/
  gl_FragData[0] = vec4(c,local,1.0);
  gl_FragData[1] = vec4(1.0);
}
)";
    std::string fragment,error;
    assert(optTranslateRoutedComposite(legacy,false,fragment,error));
    assert(fragment.find("#define MC_VERSION 12100")!=std::string::npos);
    assert(fragment.find("float a;")!=std::string::npos);
    assert(fragment.find("a= frameTime * 2.0;")!=std::string::npos);
    assert(fragment.find("float local = 1.0;")!=std::string::npos);
    assert(fragment.find("optGlobalInit0();")<fragment.find("optGlobalInit1();"));
    assert(fragment.find("optRouteTarget1")!=std::string::npos);
    auto vertex=optTranslateFullscreenVertex("#version 120\nvarying vec2 uv;\nvoid main(){uv=gl_MultiTexCoord0.xy;gl_Position=ftransform();}\n");
    assert(vertex.find("optFullscreenPosition().xy")!=std::string::npos);
    std::string probe;
    assert(optBuildEnabledProbe("#define TAA\n/* #define RETRO_FILTER */\n// #define DOF\n", "TAA && !RETRO_FILTER",probe,error));
    assert(probe.find("defined(TAA)")!=std::string::npos);
    assert(probe.find("#define RETRO_FILTER")==std::string::npos);
    assert(probe.find("#define DOF")==std::string::npos);
    assert(!optBuildEnabledProbe("","TAA; bad",probe,error));
    assert(optBuildEnabledProbe("#ifdef AUTO_EXPOSURE\nconst bool colortex0MipmapEnabled = true;\n#endif\n/* const bool colortex1MipmapEnabled = true; */\n","",probe,error));
    assert(probe.find("#ifdef AUTO_EXPOSURE\n#undef OPTLEVI_MIP_INPUT_0\n#define OPTLEVI_MIP_INPUT_0 1\n#endif")!=std::string::npos);
    assert(probe.find("#define OPTLEVI_MIP_INPUT_1 1")==std::string::npos);
    assert(optBuildEnabledProbe("void main(){}","",probe,error)&&probe.empty());
    assert(optBuildEnabledProbe("const bool colortex2Clear = false;\nconst bool gaux1Clear = false;\n","",probe,error));
    assert(probe.find("#define OPTLEVI_HISTORY_2 1")!=std::string::npos);
    assert(probe.find("#define OPTLEVI_HISTORY_4 1")!=std::string::npos);
    assert(optBuildEnabledProbe("// const bool colortex3Clear = false;\n","",probe,error)&&probe.empty());
    auto numeric=optTranslateBslNumeric(
        "// BSL Shaders v10 Series\n"
        "float light=eyeBrightnessSmooth.y / 240.0;\n"
        "if(testCoord.y > 0 && testCoord.y < 1.0){}\n"
        "float value=1.0 / samples;\n");
    assert(numeric.find("float(eyeBrightnessSmooth.y) / 240.0")!=std::string::npos);
    assert(numeric.find("testCoord.y > 0.0 && testCoord.y < 1.0")!=std::string::npos);
    assert(numeric.find("1.0.0")==std::string::npos);
    assert(numeric.find("1.0 / float(samples)")!=std::string::npos);
    assert(optTranslateBslNumeric("1.0 / samples")=="1.0 / samples");
    auto geometry=optTranslateGeometryVertex("#version 120\nattribute vec4 mc_Entity;\nvarying vec4 color;\nvoid main(){color=gl_Color;gl_Position=ftransform();}\n");
    assert(geometry.find("in vec4 optPosition;")!=std::string::npos);
    assert(geometry.find("in vec4 mc_Entity;")!=std::string::npos);
    assert(geometry.find("gl_VertexID")==std::string::npos);
    assert(geometry.find("optGeometryTransform()")!=std::string::npos);
    std::string geometryFragment;
    assert(optTranslateGeometryFragment("#version 120\nuniform sampler2D texture;\nvoid main(){\n/* DRAWBUFFERS:13 */\ngl_FragData[0]=texture2D(texture,vec2(0.5));gl_FragData[1]=vec4(1.0);\n}\n",geometryFragment,error));
    assert(geometryFragment.find("uniform sampler2D optGeometryAtlas;")!=std::string::npos);
    assert(geometryFragment.find("texture(optGeometryAtlas,")!=std::string::npos);
    assert(geometryFragment.find("optRouteTarget3")!=std::string::npos);
    std::string alpha;
    assert(optAlphaCondition("GREATER 0.005",alpha,error));
    assert(alpha=="optFragColor.a > (0.005)");
    assert(optTranslateGeometryFragment("#version 120\nvoid main(){gl_FragData[0]=vec4(1.0);}\n",geometryFragment,error,alpha));
    assert(geometryFragment.find("if (!(optFragColor.a > (0.005))) discard;")!=std::string::npos);
    assert(optAlphaCondition("off",alpha,error)&&alpha.empty());
    assert(optAlphaCondition("NEVER 0",alpha,error)&&alpha=="false");
    assert(!optAlphaCondition("GREATER 2",alpha,error));
    std::vector<std::pair<unsigned,std::string>> formats;
    assert(optReadColorFormats("/*\nconst int colortex0Format = R11F_G11F_B10F; // HDR\nconst int gaux3Format = RGBA16;\n*/\n// const int colortex1Format = R8;\n",formats,error));
    assert(formats.size()==2&&formats[0].first==0&&formats[0].second=="R11F_G11F_B10F");
    assert(formats[1].first==6&&formats[1].second=="RGBA16");
    assert(!optReadColorFormats("const int colortex0Format = RGBA8;\n",formats,error));
    std::cout << "Original vertex, global initializers and conditional route translation: OK\n";
}
''')
    exe = tmp / "translate"
    subprocess.run(["c++", "-std=c++17", "-O0", str(tmp / "translate.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

print("Java pack C++ reader: ZIP / folder / nested includes / dimensions / safety checks OK")
print("A real BSL final pass still needs a GLES driver/device test and multi-pass scene inputs.")
