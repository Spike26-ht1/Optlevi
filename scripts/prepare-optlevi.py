#!/usr/bin/env python3
"""Prepare OptLevi from pinned ModMenuBE; preserve original third-party source/attribution."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ModMenuBE"

def replace_once(file: Path, old: str, new: str):
    value = file.read_text(encoding="utf-8")
    count = value.count(old)
    if count != 1:
        raise RuntimeError(f"{file}: expected exactly one occurrence of {old!r}, got {count}")
    file.write_text(value.replace(old, new), encoding="utf-8")

def main():
    if not (SRC / "android/src/Entry.cpp").exists():
        raise SystemExit("Initialize the ModMenuBE submodule before running this script")
    asset = ROOT / "assets" / "optlevi-original-200.png"
    png = asset.read_bytes()
    if png[:8] != bytes((137, 80, 78, 71, 13, 10, 26, 10)):
        raise RuntimeError("Original OptLevi icon is not PNG")
    size = struct.unpack(">II", png[16:24])
    if size != (200, 200):
        raise RuntimeError(f"Full resolution OptLevi icon required, got {size}")
    (SRC / "assets/icon.png").write_bytes(png)

    cm = SRC / "android/CMakeLists.txt"
    for old, new in [
        ("project(ModMenuBE_Android LANGUAGES C CXX)", "project(OptLevi_Android LANGUAGES C CXX)"),
        ('set(MOD_ID "ModMenuBE")', 'set(MOD_ID "OptLevi")'),
        ('set(MOD_NAME "ModMenuBE")', 'set(MOD_NAME "OptLevi")'),
        ('set(MOD_AUTHOR "AFAlimru")', 'set(MOD_AUTHOR "BRSpike26, AFAlimru")'),
        ('set(MOD_VERSION "0.2.1")', 'set(MOD_VERSION "0.1.0")'),
        ('set(MOD_LIBRARY_NAME "ModMenuBE")', 'set(MOD_LIBRARY_NAME "OptLevi")'),
    ]:
        replace_once(cm, old, new)

    # Do not register or configure a floating launcher button here.
    # scripts/prepare-levi-api.py supplies the native module-only entrypoint.

    mod = SRC / "android/src/ModMenuMod.cpp"
    replace_once(mod, 'modmenu::log::info("ModMenuBE loaded from {} ({} mods found)"',
                 'modmenu::log::info("OptLevi menu loaded from {} ({} mods found)"')
    print("Prepared OptLevi 0.1.0 with original 200×200 icon and no floating button")

if __name__ == "__main__":
    main()
