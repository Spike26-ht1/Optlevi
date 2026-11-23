#!/usr/bin/env python3
"""Install Android native touch fallback for the visible OptLevi JSON-UI Mods button.

The resource pack draws the vanilla-style Minecraft button. This code adds
a native Preloader input listener, NOT an Android floating overlay. The click
zone is calibrated to the original visible position at 1536x709. This is
not a true Bedrock screen-controller event hook, and must be device-tested.
"""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "ModMenuBE"
NATIVE = UPSTREAM / "android"

def replace_once(path: Path, old: str, new: str):
    content = path.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one occurrence of {old!r}, got {count}")
    path.write_text(content.replace(old, new), encoding="utf-8")

def main():
    for filename in ("GameUiTouchBridge.cpp", "GameUiTouchBridge.h"):
        source = ROOT / "src" / "android" / filename
        destination = NATIVE / "src" / filename
        if not source.is_file() or not destination.parent.is_dir():
            raise RuntimeError(f"Source or native output directory missing: {source}")
        shutil.copyfile(source, destination)

    cmake = NATIVE / "CMakeLists.txt"
    replace_once(cmake, "    src/Entry.cpp\n", "    src/Entry.cpp\n    src/GameUiTouchBridge.cpp\n")

    entry = NATIVE / "src" / "Entry.cpp"
    replace_once(entry, '#include "LauncherHud.h"\n',
                 '#include "LauncherHud.h"\n#include "GameUiTouchBridge.h"\n')
    replace_once(entry,
                 '    log().info("OptLevi native module {}", gModule ? "registered" : "FAILED");',
                 '    gameui::install();\n'
                 '    log().info("OptLevi native module {}", gModule ? "registered" : "FAILED");')
    replace_once(entry,
                 'void uninstall() {\n    if (gModule)',
                 'void uninstall() {\n    gameui::uninstall();\n    if (gModule)')
    print("OptLevi native Preloader touch fallback installed; JSON UI button remains in resource pack")

if __name__ == "__main__":
    main()
