#!/usr/bin/env python3
"""Embed the Levi API pack into a native LeviPack, following verified Android layout.

The resource pack is installed by LeviLauncher from the native package:
  OptLevi/resources/minecraft_resource_packs/LeviAPI/
This differs from distributing a standalone .mcpack alongside the native mod.

For local tests, --reference may point to the owner's corrected .levipack ZIP;
it provides the untouched high-resolution icon and optional minecraft.ttf.
The reference is never redistributed or committed as a whole.
"""
import argparse
import json
import base64
import re
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "build/optlevi-arm64-v8a/OptLevi-0.1.0-arm64-v8a.levipack"
UI = ROOT / "build/OptLevi-LeviAPI-0.7.0.mcpack"
OUTPUT = ROOT / "build/OptLevi-0.7.0-arm64-v8a.levipack"
PREFIX = "OptLevi/resources/minecraft_resource_packs/LeviAPI/"
ATLAS_HEADER = ROOT / "src/ui/OptLeviBedrockAtlas.inc"
ATLAS_IN_PACK = "OptLevi/assets/ui/bedrock26_atlas.png"

def load_embedded_atlas():
    code = ATLAS_HEADER.read_text(encoding="utf-8")
    match = re.search(r'kOptAtlasPngBase64\[\]\s*=\s*"([A-Za-z0-9+/=]+)"', code)
    if not match:
        raise ValueError("Embedded Bedrock UI PNG data missing")
    png = base64.b64decode(match.group(1), validate=True)
    import struct
    if png[:8] != bytes((137, 80, 78, 71, 13, 10, 26, 10)):
        raise ValueError("Corrupt UI atlas PNG")
    assert struct.unpack(">II", png[16:24]) == (128, 128)
    return png

def package(native=NATIVE, resource=UI, output=OUTPUT, reference=None, high_res_icon=None):
    assert native.is_file(), native
    assert resource.is_file(), resource
    output.parent.mkdir(parents=True, exist_ok=True)
    reference_icon = None
    reference_font = None
    if reference:
        with ZipFile(reference) as original:
            reference_icon = original.read("OptLevi/icon.png")
            optional_font = "OptLevi/resources/minecraft.ttf"
            if optional_font in original.namelist():
                reference_font = original.read(optional_font)
    if high_res_icon:
        reference_icon = high_res_icon.read_bytes()
    with ZipFile(native) as n, ZipFile(resource) as resource_zip, ZipFile(output, "w", ZIP_DEFLATED) as out:
        manifest = json.loads(n.read("OptLevi/manifest.json"))
        assert manifest["entry"] == "libOptLevi.so"
        manifest["version"] = "0.7.0"
        manifest["icon"] = "icon.png"
        manifest["overwrite_folders"] = ["resources/minecraft_resource_packs"]
        # Keep actual override entries synchronized with the files stored in the ZIP.
        manifest["overwrite_files"] = ["icon.png"]
        if reference_font:
            manifest["overwrite_files"].append("resources/minecraft.ttf")
        icon = reference_icon or n.read("OptLevi/icon.png")
        assert icon.startswith(bytes((137, 80, 78, 71, 13, 10, 26, 10)))
        # A lossless 200x200 original icon must be supplied to avoid the old 40x40 regression.
        # This fails clearly instead of silently degrading the icon.
        import struct
        w, h = struct.unpack(">II", icon[16:24])
        if w < 200 or h < 200:
            raise ValueError(
                f"Original high-resolution icon required; got {w}x{h}. "
                "Pass --high-res-icon or --reference containing the owner's 200x200 PNG."
            )
        for info in n.infolist():
            if info.is_dir() or info.filename in ("OptLevi/manifest.json", "OptLevi/icon.png"):
                continue
            if info.filename.startswith("OptLevi/resources/minecraft_resource_packs/"):
                continue
            out.writestr(info.filename, n.read(info.filename))
        out.writestr("OptLevi/manifest.json", json.dumps(manifest, indent=2) + "\n")
        out.writestr("OptLevi/icon.png", icon)
        # The native .so embeds this PNG for runtime GPU upload. A second
        # copy in the pack makes the pixel assets auditable and reusable.
        out.writestr(ATLAS_IN_PACK, load_embedded_atlas())
        if reference_font:
            out.writestr("OptLevi/resources/minecraft.ttf", reference_font)
        for info in resource_zip.infolist():
            if info.is_dir():
                continue
            payload = icon if info.filename == "pack_icon.png" else resource_zip.read(info.filename)
            out.writestr(PREFIX + info.filename, payload)
    with ZipFile(output) as z:
        names = set(z.namelist())
        assert "OptLevi/libOptLevi.so" in names
        assert ATLAS_IN_PACK in names
        assert z.read(ATLAS_IN_PACK) == load_embedded_atlas()
        assert PREFIX + "manifest.json" in names
        assert PREFIX + "ui/start_screen.json" in names
        actual = json.loads(z.read("OptLevi/manifest.json"))
        assert actual["overwrite_folders"] == ["resources/minecraft_resource_packs"]
        assert actual["entry"] == "libOptLevi.so"
        assert z.read(PREFIX + "pack_icon.png") == z.read("OptLevi/icon.png")
    print("Validated OptLevi package:", output)
    print("Embedded icon:", f"{w}x{h}")
    print("Embedded the actual Bedrock texture atlas for the native ImGui GPU renderer")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", type=Path, default=NATIVE)
    ap.add_argument("--resource", type=Path, default=UI)
    ap.add_argument("--output", type=Path, default=OUTPUT)
    ap.add_argument("--reference", type=Path, help="Owner's corrected package, local use only")
    ap.add_argument("--high-res-icon", type=Path, help="Original unscaled OptLevi PNG")
    args = ap.parse_args()
    package(args.native, args.resource, args.output, args.reference, args.high_res_icon)
