#!/usr/bin/env python3
"""Build deterministic original OptLevi Lab shader ZIPs."""
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED
root = Path(__file__).resolve().parents[1]
out = root / "build" / "lab-shaders"
out.mkdir(parents=True, exist_ok=True)
for name in ("OptLevi-Lab-Lite", "OptLevi-Lab-World", "OptLevi-Lab-WorldFX", "OptLevi-Lab-Terrain"):
    folder = root / "shaderpacks" / name
    target = out / (name + ("-0.2.zip" if name.endswith("WorldFX") else "-0.1.zip"))
    with ZipFile(target, "w") as z:
        for path in sorted((folder / "shaders").rglob("*")):
            if path.is_file():
                entry = ZipInfo(path.relative_to(folder).as_posix(), (2026, 11, 26, 0, 0, 0))
                entry.compress_type = ZIP_DEFLATED
                z.writestr(entry, path.read_bytes())
    with ZipFile(target) as z:
        assert z.testzip() is None
        assert "shaders/final.fsh" in z.namelist()
    print(target)
