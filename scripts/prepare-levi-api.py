#!/usr/bin/env python3
"""Replace the OptLevi prototype's floating launcher button with module-only registration.

The JSON-UI button lives in resources/levi_api/ui/start_screen.json. Binding
button.optlevi_open_menu to an Android MinecraftScreenController callback is
separate, version-specific native-hook work; this script does NOT claim it exists.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ModMenuBE"

def replace_once(path, old, new):
    s = path.read_text(encoding="utf-8")
    assert s.count(old) == 1, f"{path}: expected one occurrence of {old!r}, got {s.count(old)}"
    path.write_text(s.replace(old, new), encoding="utf-8")

def main():
    entry = SRC / "android/src/Entry.cpp"
    data = entry.read_text(encoding="utf-8-sig")
    assert "namespace modmenu::android::entry {" in data
    assert "namespace modmenu::ui {" in data
    start = data.index("namespace modmenu::android::entry {")
    # Eliminate all ButtonBuilder and floating position code from the translation unit.
    entry.write_text("""#include "Entry.h"
#include "ModMenuMod.h"
#include "State.h"
#include "LauncherHud.h"
#include <pl/ModMenu.hpp>

namespace modmenu::android::entry {
namespace {
constexpr char kModuleId[] = "optlevi";
bool gModule = false;
}

void openButtonLayout() {
    closeMenu();
    launcher::openModMenu();
}

void resetButtonLayout() {
    // Button placement belongs to the resource-pack UI, not an Android overlay.
}

void install() {
    auto const& modId = ModMenuMod::instance().getSelf().getId();
    gModule = pl::modmenu::ModuleBuilder(kModuleId, "OptLevi")
        .description("OptLevi mod browser based on ModMenuBE; JSON UI entry in resource pack")
        .modId(modId)
        .defaultEnabled(true)
        .onToggle([](std::string_view, bool enabled) {
            if (!enabled) closeMenu();
        })
        .registerModule();
    log().info("OptLevi native module {}", gModule ? "registered" : "FAILED");
}

void uninstall() {
    if (gModule) pl::modmenu::unregisterModule(kModuleId);
    gModule = false;
}
} // namespace modmenu::android::entry

namespace modmenu::ui {
void openButtonLayout() { android::entry::openButtonLayout(); }
void resetButtonLayout() { android::entry::resetButtonLayout(); }
} // namespace modmenu::ui
""", encoding="utf-8")

    # Fabric Mod Menu-inspired desktop layout, restrained dark cards and blue accents.
    theme = SRC / "src/ui/OreTheme.h"
    for old, new in [
        ("inline constexpr ImU32 Panel        = rgb(0x313233);", "inline constexpr ImU32 Panel        = rgb(0x24262C);"),
        ("inline constexpr ImU32 PanelLight   = rgb(0x48494A);", "inline constexpr ImU32 PanelLight   = rgb(0x323640);"),
        ("inline constexpr ImU32 PanelEdge    = rgb(0x5A5B5C);", "inline constexpr ImU32 PanelEdge    = rgb(0x454A57);"),
        ("inline constexpr ImU32 Inset        = rgb(0x242425);", "inline constexpr ImU32 Inset        = rgb(0x1A1C21);"),
        ("inline constexpr ImU32 TextDim      = rgb(0xB1B2B5);", "inline constexpr ImU32 TextDim      = rgb(0xBBC1CC);"),
        ("inline constexpr ImU32 Green        = rgb(0x3C8527);", "inline constexpr ImU32 Green        = rgb(0x467BEA);"),
        ("inline constexpr ImU32 GreenHover   = rgb(0x2A641C);", "inline constexpr ImU32 GreenHover   = rgb(0x315EB8);"),
        ("inline constexpr ImU32 GreenLip     = rgb(0x1D4D13);", "inline constexpr ImU32 GreenLip     = rgb(0x233F86);"),
        ("inline constexpr ImU32 GreenEdge    = rgb(0x6CC349);", "inline constexpr ImU32 GreenEdge    = rgb(0x77A5FF);"),
        ("inline constexpr ImU32 Selection    = rgb(0xFFFFFF);", "inline constexpr ImU32 Selection    = rgb(0x79A7FF);"),
    ]:
        replace_once(theme, old, new)

    menu = SRC / "src/ui/ModMenuScreen.cpp"
    for old, new in [
        ('titleText = tr("title");', 'titleText = "OptLevi  /  Mods";'),
        ("float leftW = std::max(s(320), total * 0.42f);",
         "float leftW = std::clamp(total * 0.38f, std::min(s(220), total * 0.30f), total * 0.50f);"),
        ("col::rgb(0x2A2A2B), false", "col::rgb(0x1A1C21), false"),
        ("ore::panel(dl, min, max, selected ? col::PanelLight : (hovered ? col::rgb(0x3B3C3E) : col::Panel), true);",
         "ore::panel(dl, min, max, selected ? col::PanelLight : (hovered ? col::rgb(0x303543) : col::Panel), false);"),
        ("if (selected) dl->AddRect(min, max, col::Selection, 0, 0, s(2));",
         "if (selected) dl->AddRectFilled(min, {min.x + s(4), max.y}, col::Selection);"),
        ("ore::panel(dl, min, {min.x + width, min.y + height}, col::PanelLight, true);",
         "ore::panel(dl, min, {min.x + width, min.y + height}, col::PanelLight, false);"),
        ("float       height = s(66);", "float       height = s(62);"),
        ("float  iconSize = s(46);", "float  iconSize = s(42);"),
        ("float headerH = s(60), footerH = s(58), pad = s(14);",
         "float headerH = s(56), footerH = s(52), pad = s(12);"),
        ('ore::searchField("##search", gState.search, sizeof(gState.search), tr("search"), leftW - s(4));',
         'ore::searchField("##search", gState.search, sizeof(gState.search), tr("search"), leftW - s(4));\\n    ImGui::Spacing();'),
        ('    ImGui::BeginChild("list", {0, 0}, ImGuiChildFlags_None);',
         '    ImGui::TextDisabled("Mods: %d", static_cast<int>(mods.size()));\\n    ImGui::Separator();\\n    ImGui::BeginChild("list", {0, 0}, ImGuiChildFlags_None);'),
        ('    if (gState.view == View::List) {\\n#ifdef _WIN32',
         '    if (gState.view == View::List) {\\n        if (placeRight("##done_mods", tr("done"), ore::Variant::Primary, false)) requestClose("Done");\\n#ifdef _WIN32'),
    ]:
        replace_once(menu, old, new)
    print("Removed launcher floating button and applied OptLevi Fabric-like menu styling")

if __name__ == "__main__":
    main()
