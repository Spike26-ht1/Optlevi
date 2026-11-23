#!/usr/bin/env python3
"""Prefer a locally installed Minecraft font, with OFL Monocraft fallback.

Do not redistribute Mojangles/Minecraft Ten; an end user may supply a legally
obtained TrueType font at <OptLevi>/resources/minecraft.ttf.
When available, our packaged OFL Monocraft provides a pixel-art style instead.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"ModMenuBE/src/ui/Fonts.cpp"
OLD="""    gBody  = addFont(atlas, kRegular, std::round(kBodySize * scale), sRanges.data(), 1.0f);
    gBold  = addFont(atlas, kBold, std::round(kBodySize * scale), sShown.data(), kBoldWeight);
    gTitle = addFont(atlas, kBold, std::round(kTitleSize * scale), sShown.data(), kBoldWeight);"""
NEW="""#ifndef _WIN32
    // The original Bedrock/Mojangles font is never bundled by OptLevi.
    // Local legal font takes priority; bundled SIL-OFL Monocraft is a fallback.
    auto localFont = paths::selfDir() / "resources" / "minecraft.ttf";
    auto freeFont = paths::selfDir() / "resources" / "monocraft.ttf";
    auto chosen = std::filesystem::exists(localFont) ? localFont : freeFont;
    gBody = addFontFile(atlas, chosen, 0, false,
                        std::round(kBodySize * scale), sRanges.data(), 1.0f);
    gBold = addFontFile(atlas, chosen, 0, false,
                        std::round(kBodySize * scale), sShown.data(), 1.12f);
    gTitle = addFontFile(atlas, chosen, 0, false,
                         std::round(kTitleSize * scale), sShown.data(), 1.12f);
    if (!gBody) gBody = addFont(atlas, kRegular, std::round(kBodySize * scale), sRanges.data(), 1.0f);
    if (!gBold) gBold = addFont(atlas, kBold, std::round(kBodySize * scale), sShown.data(), kBoldWeight);
    if (!gTitle) gTitle = addFont(atlas, kBold, std::round(kTitleSize * scale), sShown.data(), kBoldWeight);
#else
    gBody  = addFont(atlas, kRegular, std::round(kBodySize * scale), sRanges.data(), 1.0f);
    gBold  = addFont(atlas, kBold, std::round(kBodySize * scale), sShown.data(), kBoldWeight);
    gTitle = addFont(atlas, kBold, std::round(kTitleSize * scale), sShown.data(), kBoldWeight);
#endif"""
def main():
    s=SOURCE.read_text(encoding="utf-8")
    assert s.count(OLD)==1,"Upstream font initialization changed"
    assert s.count('#include "core/Log.h"')==1
    s=s.replace('#include "core/Log.h"','#include "core/Log.h"\n#include "core/Paths.h"')
    s=s.replace(OLD,NEW)
    SOURCE.write_text(s,encoding="utf-8")
    print("Installed OptLevi pixel-font preference with fallback")
if __name__=="__main__":
    main()
