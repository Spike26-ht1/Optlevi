#!/usr/bin/env python3
"""Add one atomic pause-to-Shaders route to the pinned Android State."""
from pathlib import Path
p=Path(__file__).resolve().parents[1]/"ModMenuBE/android/src/State.h"
s=p.read_text(encoding="utf-8")
old="    std::atomic<bool> menuOpen{false};"
assert s.count(old)==1
p.write_text(s.replace(old,old+"\n    std::atomic<bool> shaderOpenRequested{false};"),encoding="utf-8")
print("Installed Shaders menu route flag")
