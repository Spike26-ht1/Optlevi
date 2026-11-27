#!/usr/bin/env python3
"""Fabric Mod Menu-inspired structural redesign for OptLevi's native ImGui browser.

Runs AFTER scripts/prepare-levi-api.py. Changes actual screen geometry, panel
composition, controls, cards and widget rendering — not only palette colors.
Keeps functional mod discovery, config, enable/disable and update actions.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "build/native-template" / "src" / "ui"

def replace_once(path, old, new):
    content = path.read_text(encoding="utf-8")
    count = content.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one anchor, found {count}: {old!r}")
    path.write_text(content.replace(old, new), encoding="utf-8")

def main():
    screen = SRC / "ModMenuScreen.cpp"
    replacements = [
        # Fabric's in-game world remains visible beneath lighter, translucent panels.
        ("dl->AddRectFilled({0, 0}, display, col::Dim);",
         "dl->AddRectFilled({0, 0}, display, col::rgb(0x11131A, 120));"),
        ("float margin = full ? 0.0f : std::max(s(24), display.y * 0.04f);",
         "float margin = full ? 0.0f : std::max(s(12), display.y * 0.025f);"),
        ("float width  = full ? display.x : std::min(display.x - margin * 2, s(1280));",
         "float width  = full ? display.x : std::min(display.x - margin * 2, s(1510));"),
        ("ore::panel(dl, min, max, col::Panel, true);",
         "dl->AddRectFilled(min, max, col::rgb(0x15171C, 188));"),
        ('titleText = "OptLevi  /  Mods";',
         'titleText = "Mods";'),
        ('float headerH = s(56), footerH = s(52), pad = s(12);',
         'float headerH = s(46), footerH = s(50), pad = s(12);'),
        ('ore::panel(dl, min, {min.x + width, min.y + height}, col::PanelLight, false);',
         'dl->AddRectFilled(min, {min.x + width, min.y + height}, col::rgb(0x111319, 145));'),
        # Roughly equal list and detail columns, closer to Fabric's desktop list.
        ('float leftW = std::clamp(total * 0.38f, std::min(s(220), total * 0.30f), total * 0.50f);',
         'float leftW = std::clamp(total * 0.47f, s(270), total * 0.53f);'),
        ('ImGui::BeginChild("left", {leftW, h});',
         'ImGui::BeginChild("left", {leftW, h}, ImGuiChildFlags_AlwaysUseWindowPadding);'),
        ('ImGui::TextDisabled("Mods: %d", static_cast<int>(mods.size()));',
         'ImGui::TextDisabled("Showing %d mods", static_cast<int>(mods.size()));'),
        ('ore::panel(ImGui::GetWindowDrawList(), dmin, bodyMax, col::rgb(0x1A1C21), false);',
         'ImGui::GetWindowDrawList()->AddRectFilled(dmin, bodyMax, col::rgb(0x111318, 142));'),
        # Rows: flatter, tighter, with a clear Fabric-style 3px selected stripe.
        ('float       height = s(62);', 'float       height = s(58);'),
        ('float  iconSize = s(42);', 'float  iconSize = s(40);'),
        ('ore::panel(dl, min, max, selected ? col::PanelLight : (hovered ? col::rgb(0x303543) : col::Panel), false);',
         'dl->AddRectFilled(min, max, selected ? col::rgb(0x343A45, 225) : (hovered ? col::rgb(0x343841, 170) : col::rgb(0x202228, 135)));'),
        ('if (selected) dl->AddRectFilled(min, {min.x + s(4), max.y}, col::Selection);',
         'if (selected) dl->AddRectFilled(min, {min.x + s(3), max.y}, col::rgb(0xD1D5DB));'),
        # Details: more breathing room and clear typographic hierarchy.
        ('float iconSize = s(76);', 'float iconSize = s(72);'),
        ('ImGui::SetCursorScreenPos({dmin.x + s(14), dmin.y + s(14)});',
         'ImGui::SetCursorScreenPos({dmin.x + s(20), dmin.y + s(16)});'),
        ('ImGui::BeginChild("details", {bodyMax.x - dmin.x - s(20), h - s(20)});',
         'ImGui::BeginChild("details", {bodyMax.x - dmin.x - s(30), h - s(24)});'),
        # Footer: Fabric's Done is a centered primary action; utilities on right.
        ('if (placeRight("##done_mods", tr("done"), ore::Variant::Primary, false)) requestClose("Done");',
         'ImGui::SetCursorScreenPos({min.x + width * 0.5f - s(65), by});\n'
         '        if (ore::button("##done_mods", tr("done"), ore::Variant::Primary, {s(130), s(40)})) requestClose("Done");'),
        # No heavy dark footer band: present the game behind it.
        ('dl->AddRectFilled({min.x + s(2), max.y - footerH}, {max.x - s(2), max.y - footerH + s(2)}, col::Outline);',
         'dl->AddRectFilled({min.x + s(2), max.y - footerH}, {max.x - s(2), max.y - footerH + s(1)}, col::rgb(0x777C85, 130));'),
    ]
    for old, new in replacements:
        replace_once(screen, old, new)

    # Use flat Fabric-like buttons and neutral blue/grey surfaces, replacing
    # the original raised 3D Bedrock-style widget frame in the NATIVE menu.
    theme_cpp = SRC / "OreTheme.cpp"
    old_frame = """    float press = held ? lip : 0;
    dl->AddRectFilled(min, max, color::Outline);
    // lip
    dl->AddRectFilled({min.x + o, min.y + o}, {max.x - o, max.y - o}, p.lip);
    // face
    ImVec2 fmin{min.x + o, min.y + o + press};
    ImVec2 fmax{max.x - o, max.y - o - lip + press};
    dl->AddRectFilled(fmin, fmax, hovered ? p.hover : p.face);
    // inner light edge (top + left)
    float e = s(2);
    dl->AddRectFilled(fmin, {fmax.x, fmin.y + e}, p.edge);
    dl->AddRectFilled(fmin, {fmin.x + e, fmax.y}, p.edge);"""
    new_frame = """    dl->AddRectFilled(min, max, color::rgb(0x090B10, 240));
    ImVec2 fmin{min.x + o, min.y + o};
    ImVec2 fmax{max.x - o, max.y - o};
    dl->AddRectFilled(fmin, fmax,
        held ? color::rgb(0x576174, 255) : hovered ? p.hover : p.face);
    if (hovered && !held)
        dl->AddRect(fmin, fmax, color::rgb(0xD3D8E2, 230), 0, 0, s(1));"""
    replace_once(theme_cpp, old_frame, new_frame)
    replace_once(theme_cpp, 'float  lip      = s(4);', 'float  lip      = s(0);')
    replace_once(theme_cpp, 'st.FrameBorderSize  = s(2);', 'st.FrameBorderSize  = s(1);')
    replace_once(theme_cpp, 'st.ScrollbarSize    = s(14);', 'st.ScrollbarSize    = s(10);')
    replace_once(theme_cpp, 'c[ImGuiCol_ChildBg]             = ImVec4(0, 0, 0, 0);',
                 'c[ImGuiCol_ChildBg]             = ImVec4(0, 0, 0, 0);')
    theme_h = SRC / "OreTheme.h"
    for old, new in [
        ("inline constexpr ImU32 Panel        = rgb(0x24262C);",
         "inline constexpr ImU32 Panel        = rgb(0x21242B, 218);"),
        ("inline constexpr ImU32 PanelLight   = rgb(0x323640);",
         "inline constexpr ImU32 PanelLight   = rgb(0x333842, 225);"),
        ("inline constexpr ImU32 Dark         = rgb(0x48494A);",
         "inline constexpr ImU32 Dark         = rgb(0x434852);"),
        ("inline constexpr ImU32 DarkHover    = rgb(0x5A5B5C);",
         "inline constexpr ImU32 DarkHover    = rgb(0x555D6C);"),
        ("inline constexpr ImU32 Gray         = rgb(0xD0D1D4);",
         "inline constexpr ImU32 Gray         = rgb(0xBFC3CC);"),
    ]:
        replace_once(theme_h, old, new)
    print("Fabric-inspired structural OptLevi UI redesign applied")

if __name__ == "__main__":
    main()
