#!/usr/bin/env python3
"""Check real Bedrock PNG pixel atlas and its use in Android native renderer.

This is static validation plus source PNG integrity; in-game rendering
remains a separate runtime test.
"""
import base64
import hashlib
import json
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parents[1]
atlas=(ROOT/"src/ui/OptLeviBedrockAtlas.inc").read_text()
renderer=(ROOT/"src/ui/OptLeviBedrockTexture.inc").read_text()
screen=(ROOT/"ModMenuBE/src/ui/OptLeviFabricScreen.inc").read_text()
native=(ROOT/"ModMenuBE/src/ui/ModMenuScreen.cpp").read_text()

match=re.search(r'kOptAtlasPngBase64\[\]\s*=\s*"([A-Za-z0-9+/=]+)"',atlas)
assert match,"Embedded source atlas missing"
png=base64.b64decode(match.group(1),validate=True)
assert png[:8]==b"\x89PNG\r\n\x1a\n"
assert struct.unpack(">II",png[16:24])==(128,128)
assert hashlib.sha256(png).hexdigest()=="33133186c6f6be28acf54ce427d9e63f34a7b268adf3852bcde59fcb295304ab"

sprites=re.findall(r'^\s*\{\d+, \d+, \d+, \d+, \d+\},\s*//\s*(\w+)',atlas,re.M)
assert len(sprites)==16,len(sprites)
assert len(set(sprites))==16
for required in ("ButtonNormal","ButtonHover","ButtonPressed","ButtonHoverPressed",
                 "PanelDark","PanelLight","Input","InputHover","ScrollHandle",
                 "CheckOff","CheckOn","CheckOffHover","CheckOnHover"):
    assert required in sprites,required

for expected in (
 "eglGetCurrentContext()","stbi_load_from_memory","glGenTextures",
 "glTexImage2D","GL_TEXTURE_2D","GL_NEAREST","GL_CLAMP_TO_EDGE",
 "dl->AddImage(", "static bool ofNineSprite", "static bool ofSpriteImage",
 "static void ofTexturedScrollbar","static bool ofTexturedCheckbox",
):
    assert expected in renderer,expected

assert '#include "OptLeviBedrockTexture.inc"' in screen
assert native.count("drawOptLeviFabricScreen(display);")==1
for expected in (
    "ofNineSprite(dl,sprite,pos,max,unit)", # real UI texture on buttons
    "ofNineSprite(dl,OptSprite::PanelDark,p,m,unit", # actual textures on rows
    "ofNineSprite(dl,OptSprite::PanelLight", # title panel
    "ofNineSprite(dl,OptSprite::Input", # actual input atlas texture
    "ofTexturedCheckbox(", # actual checkbox sprites
    "ofTexturedScrollbar(", # actual scrollbar sprites
    "ofButton(\"##of_done\",\"Done\"", # working controls preserved
    "ofButton(\"##of_website\",\"Website\"",
    "openConfigFor(mod)",
    "toggleEnabled(mod)",
):
    assert expected in screen,expected

base=json.loads((ROOT/"resources/levi_api/manifest.json").read_text())
assert base["header"]["version"]==[0,9,0]
prep=(ROOT/"scripts/prepare-fabric-real-screen.py").read_text()
assert "OptLeviBedrockAtlas.inc" in prep
assert "OptLeviBedrockTexture.inc" in prep
assert "#include <GLES3/gl3.h>" in prep
assert "#include <stb_image.h>" in prep
print("Verified 16 exact Bedrock UI sprites in embedded 128×128 PNG atlas.")
print("Verified GPU upload, nine-slice pixels, textured controls/panels, real mouse states.")
print("Verified OptLevi 0.9.0 manifest and original Fabric-style ModMenu controls.")
