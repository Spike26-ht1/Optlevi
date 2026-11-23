#!/usr/bin/env python3
"""Wire Android's built-in SAF document picker to OptLevi's native shader menu.

The pinned LeviLauncher already implements MinecraftActivity.openPreloaderDocumentPicker
and calls PreloaderInput.nativeOnDocumentResult before removing its temp file.
No launcher APK patch, external floating button, or private storage permission needed.
"""
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"ModMenuBE"

def once(path,old,new):
    s=path.read_text(encoding="utf-8")
    if s.count(old)!=1:raise RuntimeError(f"{path}: anchor appears {s.count(old)} times: {old!r}")
    path.write_text(s.replace(old,new),encoding="utf-8")

def main():
    target=SRC/"android/src"
    for name in ("ShaderImportBridge.h","ShaderImportBridge.cpp"):
        shutil.copy2(ROOT/"src/android"/name,target/name)
    once(SRC/"android/CMakeLists.txt",
         "    src/GameUiTouchBridge.cpp\n",
         "    src/GameUiTouchBridge.cpp\n    src/ShaderImportBridge.cpp\n")
    entry=target/"Entry.cpp"
    once(entry,'#include "GameUiTouchBridge.h"\n',
         '#include "GameUiTouchBridge.h"\n#include "ShaderImportBridge.h"\n')
    once(entry,"    gameui::install();\n",
         "    gameui::install();\n    shaderimport::install();\n")
    once(entry,"    gameui::uninstall();\n",
         "    shaderimport::uninstall();\n    gameui::uninstall();\n")
    print("LeviLauncher built-in Android document picker connected to OptLevi native mod")

if __name__=="__main__":
    main()
