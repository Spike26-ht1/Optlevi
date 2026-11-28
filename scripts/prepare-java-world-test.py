#!/usr/bin/env python3
"""Install opt-in experimental GLES3 Java final-pass world postprocessor.

Uses the existing, game-frame-specific EGL swap hook. No offsets, code
patches or material.bin file replacement are introduced.
"""
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
NATIVE=ROOT/"build/native-template/android"

def once(path,old,new):
    data=path.read_text(encoding="utf-8")
    if data.count(old)!=1:
        raise RuntimeError(f"{path}: expected one anchor {old!r}, found {data.count(old)}")
    path.write_text(data.replace(old,new),encoding="utf-8")

def main():
    for name in ("ShaderWorldRuntime.cpp","ShaderWorldRuntime.h","ShaderRenderBridge.cpp","ShaderRenderBridge.h","ShaderCameraMath.h","ShaderMeshMapping.h","ShaderPackUniforms.h","ShaderPackProfiles.h","ShaderDiagnostics.h","ShaderLogExport.h","ShaderLogExport.cpp","ShaderHookTrace.h"):
        shutil.copy2(ROOT/"src/android"/name,NATIVE/"src"/name)
    once(NATIVE/"CMakeLists.txt","    src/ShaderImportBridge.cpp\n",
         "    src/ShaderImportBridge.cpp\n    src/ShaderWorldRuntime.cpp\n    src/ShaderRenderBridge.cpp\n    src/ShaderLogExport.cpp\n")
    overlay=NATIVE/"src/EglOverlay.cpp"
    once(overlay,'#include "State.h"\n',
         '#include "State.h"\n#include "ShaderWorldRuntime.h"\n#include "ShaderRenderBridge.h"\n#include "ShaderLogExport.h"\n')
    once(overlay,'''    if (game) {
        if (st.menuOpen.load(std::memory_order_relaxed) || ui::wantsFrame()) {''',
         '''    if (game) {
        shaderbridge::PresentGuard phaseGuard;
        EGLint worldW=0,worldH=0;
        eglQuerySurface(display,surface,EGL_WIDTH,&worldW);
        eglQuerySurface(display,surface,EGL_HEIGHT,&worldH);
        if (worldW>0 && worldH>0) shaderworld::beforeSwap(worldW,worldH);
        shaderdiagnostics::persistMediaLog();
        if (st.menuOpen.load(std::memory_order_relaxed) || ui::wantsFrame()) {''')
    once(overlay,'    return ok;\n}\n\n} // namespace modmenu::android::overlay',
         '    if (ok) shaderbridge::install();\n    return ok;\n}\n\n} // namespace modmenu::android::overlay')
    entry=NATIVE/"src/Entry.cpp"
    once(entry,'#include "ShaderImportBridge.h"\n',
         '#include "ShaderImportBridge.h"\n#include "ShaderWorldRuntime.h"\n')
    once(entry,"    shaderimport::uninstall();\n",
         "    shaderworld::stop();\n    shaderimport::uninstall();\n")
    lifecycle=NATIVE/"src/ModMenuMod.cpp"
    once(lifecycle,'#include "platform/Jni.h"\n',
         '#include "platform/Jni.h"\n#include "ShaderDiagnostics.h"\n#include "ShaderLogExport.h"\n')
    once(lifecycle,"    jni::init(mSelf.getJavaVM());\n",
         '    jni::init(mSelf.getJavaVM());\n    shaderdiagnostics::record("Lifecycle","OptLevi enabled (automatic renderer logging)");\n    shaderdiagnostics::persistMediaLog(true);\n')
    once(lifecycle,'    if (!overlay::install()) {\n        mSelf.getLogger().error("eglSwapBuffers hook failed; the menu cannot be drawn");\n    }\n',
         '    if (!overlay::install()) {\n        mSelf.getLogger().error("eglSwapBuffers hook failed; the menu cannot be drawn");\n        shaderdiagnostics::record("Hooks","EGL swap hook failed");\n    }else{\n        shaderdiagnostics::record("Hooks","EGL swap hook installed");\n    }\n    shaderdiagnostics::persistMediaLog(true);\n')
    once(lifecycle,"    entry::uninstall();\n",
         '    entry::uninstall();\n    shaderdiagnostics::record("Lifecycle","OptLevi disabled");\n    shaderdiagnostics::persistMediaLog(true);\n')
    print("Opt-in Java runtime and automatic Android media diagnostics installed")

if __name__=="__main__":
    main()
