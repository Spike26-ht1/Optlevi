#!/usr/bin/env python3
"""Load the shipped Lab ZIPs through the actual Java pack loader and GLES engine."""
import ast
import os
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
subprocess.run(["python3", str(root / "scripts/build-lab-shaders.py")], check=True)
tree = ast.parse((root / "scripts/test-java-bsl.py").read_text())
harness = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == "HARNESS" for t in n.targets))
prefix = harness.split("int main(int argc", 1)[0]
source = (root / "src/ui/OptLeviShaderGlesTest.inc").read_text()
loader = source[source.index("static void optReplaceAll("):source.index("static GLuint optCompile(")]
with TemporaryDirectory() as folder:
    temp = Path(folder)
    # Lab packs contain no PNGs. Reject PNG decoding rather than pretending
    # to test the Android image decoder on the host.
    header = """
#include "ShaderWorldRuntime.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <map>
#include <vector>
static int stbi_info_from_memory(unsigned char const*,int,int*,int*,int*){return 0;}
static unsigned char* stbi_load_from_memory(unsigned char const*,int,int*,int*,int*,int){return nullptr;}
static void stbi_image_free(void*){}
"""
    harness_path = temp / "lab.cpp"
    harness_path.write_text(header + prefix + loader + (root / "tests/lab-shaders-gles.cpp").read_text())
    exe = temp / "lab"
    subprocess.run(["c++","-std=c++17","-O2","-pthread","-I",str(root),"-I",str(root/"src/android"),
                    str(harness_path),str(root/"src/android/ShaderWorldRuntime.cpp"),
                    "-lEGL","-lGLESv2","-lz","-o",str(exe)],check=True)
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1")
    subprocess.run([str(exe),str(root/"build/lab-shaders")],env=env,check=True)
