#!/usr/bin/env python3
"""Regenerate OptLevi's 26.60 Bedrock UI texture atlas from the owner's ZIP.

Requires: pip install Pillow
The archive is not stored in Git. PNG source pixels are copied 1:1,
with transparent padding, into a 128x128 texture for GPU nine-slicing.
Usage:
  python scripts/generate-bedrock-atlas.py /path/to/levi_packs.zip --check
  python scripts/generate-bedrock-atlas.py /path/to/levi_packs.zip --write

The checked-in header embeds the generated PNG for GLES3 texture upload.
"""
import argparse
import base64
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import re
from zipfile import ZipFile
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
HEADER=ROOT/"src/ui/OptLeviBedrockAtlas.inc"
SOURCES=[
 ("ButtonNormal","gui/newgui/buttons/border/base.png",2),
 ("ButtonHover","gui/newgui/buttons/border/hover.png",2),
 ("ButtonPressed","gui/newgui/buttons/border/basePress.png",2),
 ("ButtonHoverPressed","gui/newgui/buttons/border/hoverPress.png",2),
 ("ButtonDisabled","ui/disabledButton.png",1),
 ("PanelDark","ui/edit_box_indent.png",2),
 ("PanelLight","ui/background_panel.png",4),
 ("PanelOutline","ui/panel_outline.png",1),
 ("Input","ui/edit_box_indent.png",2),
 ("InputHover","ui/edit_box_indent_hover.png",2),
 ("ScrollHandle","ui/ScrollHandle.png",1),
 ("ScrollRail","ui/ScrollRail.png",0),
 ("CheckOff","gui/newgui/buttons/checkbox/checkbox_unchecked.png",0),
 ("CheckOn","gui/newgui/buttons/checkbox/checkbox_checked.png",0),
 ("CheckOffHover","gui/newgui/buttons/checkbox/checkbox_unchecked_hover.png",0),
 ("CheckOnHover","gui/newgui/buttons/checkbox/checkbox_checked_hover.png",0),
]

def generate(archive):
    atlas=Image.new("RGBA",(128,128),(0,0,0,0))
    rects=[]
    with ZipFile(archive) as z:
        for i,(name,source,corner) in enumerate(SOURCES):
            original=Image.open(BytesIO(z.read(source))).convert("RGBA")
            x=3+(i%4)*30
            y=3+(i//4)*30
            assert x+original.width<=128 and y+original.height<=128,source
            atlas.paste(original,(x,y))
            # Exact pixel-copy verification.
            assert atlas.crop((x,y,x+original.width,y+original.height)).tobytes()==original.tobytes()
            rects.append((name,x,y,original.width,original.height,corner))
    out=BytesIO()
    atlas.save(out,"PNG",optimize=True,compress_level=9)
    return out.getvalue(),rects

def render_header(png,rects):
    encoded=base64.b64encode(png).decode("ascii")
    return "\n".join([
        "// Bedrock 26.60 UI atlas generated without resampling from the owner's PNG archive.",
        "// Packed: 16 UI sprites in a 128x128 PNG; decoded on the Android GPU render thread.",
        "// Reproducible using scripts/generate-bedrock-atlas.py with the supplied levi_packs.zip.",
        "#pragma once",
        "enum class OptSprite : unsigned {",
        *[f"    {name}," for name,*_ in rects],
        "    Count",
        "};",
        "struct OptSpriteRect {int x; int y; int w; int h; int corner;};",
        "static constexpr int kOptAtlasWidth=128, kOptAtlasHeight=128;",
        "static constexpr OptSpriteRect kOptAtlasRects[]={",
        *[f"    {{{x}, {y}, {w}, {h}, {corner}}}, // {name}"
          for name,x,y,w,h,corner in rects],
        "};",
        'static constexpr char kOptAtlasPngBase64[]=',
        f'"{encoded}";',
        ""
    ])

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("archive",type=Path)
    parser.add_argument("--check",action="store_true")
    parser.add_argument("--write",action="store_true")
    parser.add_argument("--png",type=Path)
    args=parser.parse_args()
    png,rects=generate(args.archive)
    content=render_header(png,rects)
    checked=HEADER.read_text(encoding="utf-8") if HEADER.exists() else ""
    if args.check:
        expected=re.search(r'kOptAtlasPngBase64\[\]\s*=\s*"([A-Za-z0-9+/=]+)"',checked)
        assert expected, "Checked-in atlas PNG not found"
        assert base64.b64decode(expected.group(1))==png, "Atlas source pixels differ"
        print("Exact atlas PNG bytes match committed version")
    if args.write:
        HEADER.write_text(content,encoding="utf-8")
    if args.png:
        args.png.write_bytes(png)
    print("Source sprites:",len(rects),"; Atlas:",len(png),"bytes")
    print("Atlas SHA-256:",sha256(png).hexdigest())

if __name__=="__main__":
    main()
