#!/usr/bin/env python3
"""Replace the native OptLevi Mods list screen, not merely its theme.

Run after prepare-fabric21-fidelity.py. The list screen is drawn through an
independently maintained Fabric-like UI; AFAlimru's mod registry, config,
icon cache, update checker, and modal views remain as backend dependencies.
"""
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
MENU=ROOT/"ModMenuBE/src/ui/ModMenuScreen.cpp"
INCLUDE=ROOT/"src/ui/OptLeviFabricScreen.inc"
DEST=ROOT/"ModMenuBE/src/ui/OptLeviFabricScreen.inc"


def once(body, old, new):
    n=body.count(old)
    if n!=1:
        raise RuntimeError(f"Expected one OptLevi UI anchor (got {n}): {old!r}")
    return body.replace(old,new)


def main():
    if not INCLUDE.is_file():
        raise RuntimeError("Custom Fabric renderer source is missing")
    shutil.copy2(INCLUDE,DEST)
    # Both native GPU sprite helpers must be co-located with the included
    # renderer, because the application loads these PNG bytes at runtime.
    for name in ("OptLeviBedrockAtlas.inc", "OptLeviBedrockTexture.inc", "OptLeviShaderScreen.inc"):
        original=ROOT/"src"/"ui"/name
        if not original.is_file():
            raise RuntimeError(f"Bedrock atlas dependency missing: {original}")
        shutil.copy2(original,DEST.parent/name)
    src=MENU.read_text(encoding="utf-8")
    src=once(src, "#include <imgui_internal.h>",
        "#include <imgui_internal.h>\n"
        "#include <EGL/egl.h>\n"
        "#include <GLES3/gl3.h>\n"
        "#include <stb_image.h>\n"
        "#include <cstdint>\n"
        "#include <vector>")

    # Include into the same C++ namespace/translation unit to reuse registry
    # and mod actions, while replacing the entire View::List rendering path.
    src=once(src,
             "void drawConfirm() {",
             '#include "OptLeviFabricScreen.inc"\n\nvoid drawConfirm() {')

    anchor="    dl->AddRectFilled({0, 0}, display, col::rgb(0x11131A, 145));"
    replacement="""    // The Fabric list no longer enters the legacy OreUI framed dialog.
    // Config/JSON editor subviews continue using the native backend.
    if (gState.view == View::List) {
        drawOptLeviFabricScreen(display);
        drawConfirm();
        ImGui::End();
        return;
    }
    dl->AddRectFilled({0, 0}, display, col::rgb(0x11131A, 145));"""
    src=once(src,anchor,replacement)
    MENU.write_text(src,encoding="utf-8")
    assert 'drawOptLeviFabricScreen(display);' in MENU.read_text()
    print("Replaced the native OptLevi list View renderer with Fabric full-screen UI")


if __name__=="__main__":
    main()
