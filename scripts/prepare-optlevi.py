#!/usr/bin/env python3
"""Stage the checked-in OptLevi native template into a disposable build tree."""
from pathlib import Path
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "native-template"
SRC = ROOT / "build" / "native-template"

def main():
    assert (TEMPLATE / "android/src/Entry.cpp").is_file(), "OptLevi native template missing"
    # The following preparation scripts operate only on this disposable copy.
    if SRC.exists():
        shutil.rmtree(SRC)
    shutil.copytree(TEMPLATE, SRC)
    png = (ROOT / "assets/optlevi-original-200.png").read_bytes()
    assert png[:8] == bytes((137,80,78,71,13,10,26,10))
    assert struct.unpack(">II",png[16:24]) == (200,200)
    (SRC / "assets/icon.png").write_bytes(png)
    cmake = (SRC / "android/CMakeLists.txt").read_text()
    assert 'set(MOD_ID "OptLevi")' in cmake
    assert 'set(MOD_LIBRARY_NAME "OptLevi")' in cmake
    print("Staged checked-in OptLevi native template; source remains unchanged")

if __name__ == "__main__":
    main()
