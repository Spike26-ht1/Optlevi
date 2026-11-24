#!/usr/bin/env python3
"""Install opt-in experimental GLES3 Java final-pass world postprocessor.

Uses the existing, game-frame-specific EGL swap hook. No offsets, code
patches or material.bin file replacement are introduced.
"""
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
NATIVE=ROOT/"ModMenuBE/android"

def once(path,old,new):
    data=path.read_text(encoding="utf-8")
    if data.count(old)!=1:
        raise RuntimeError(f"{path}: expected one anchor {old!r}, found {data.count(old)}")
    path.write_text(data.replace(old,new),encoding="utf-8")

def main():
    for name in ("ShaderWorldRuntime.cpp","ShaderWorldRuntime.h","ShaderRenderBridge.cpp","ShaderRenderBridge.h","ShaderCameraMath.h"):
        shutil.copy2(ROOT/"src/android"/name,NATIVE/"src"/name)
    once(NATIVE/"CMakeLists.txt","    src/ShaderImportBridge.cpp\n",
         "    src/ShaderImportBridge.cpp\n    src/ShaderWorldRuntime.cpp\n    src/ShaderRenderBridge.cpp\n")
    overlay=NATIVE/"src/EglOverlay.cpp"
    once(overlay,'#include "State.h"\n',
         '#include "State.h"\n#include "ShaderWorldRuntime.h"\n#include "ShaderRenderBridge.h"\n')
    once(overlay,'''    if (game) {
        if (st.menuOpen.load(std::memory_order_relaxed) || ui::wantsFrame()) {''',
         '''    if (game) {
        shaderbridge::PresentGuard phaseGuard;
        EGLint worldW=0,worldH=0;
        eglQuerySurface(display,surface,EGL_WIDTH,&worldW);
        eglQuerySurface(display,surface,EGL_HEIGHT,&worldH);
        if (worldW>0 && worldH>0) shaderworld::beforeSwap(worldW,worldH);
        if (st.menuOpen.load(std::memory_order_relaxed) || ui::wantsFrame()) {''')
    once(overlay,'    return ok;\n}\n\n} // namespace modmenu::android::overlay',
         '    if (ok) shaderbridge::install();\n    return ok;\n}\n\n} // namespace modmenu::android::overlay')
    entry=NATIVE/"src/Entry.cpp"
    once(entry,'#include "ShaderImportBridge.h"\n',
         '#include "ShaderImportBridge.h"\n#include "ShaderWorldRuntime.h"\n')
    once(entry,"    shaderimport::uninstall();\n",
         "    shaderworld::stop();\n    shaderimport::uninstall();\n")
    print("Opt-in, single-pass world shader bound to the existing game-specific EGL swap")

if __name__=="__main__":
    main()
