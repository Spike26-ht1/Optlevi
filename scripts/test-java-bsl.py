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

print("Java pack C++ reader: ZIP / folder / nested includes / dimensions / safety checks OK")
print("A real BSL final pass still needs a GLES driver/device test and multi-pass scene inputs.")
