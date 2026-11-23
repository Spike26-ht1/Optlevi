#!/usr/bin/env python3
"""Regression checks for the pause-menu UI and two distinct native Mods routes.

UI is checked against the structure inspected in the owner's supplied
Minecraft Bedrock pause_screen.json. This is not an in-game render test.
"""
import json
import re
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
PAUSE = ROOT / "resources/levi_api/ui/pause_screen.json"
UI_DEFS = ROOT / "resources/levi_api/ui/_ui_defs.json"
NATIVE = ROOT / "src/android/GameUiTouchBridge.cpp"

pause = json.loads(PAUSE.read_text(encoding="utf-8"))
defs = json.loads(UI_DEFS.read_text(encoding="utf-8"))
native = NATIVE.read_text(encoding="utf-8")

assert {"ui/start_screen.json", "ui/pause_screen.json"} <= set(defs["ui_defs"])
assert pause["namespace"] == "pause"

row = pause["smaller_buttons_panel/stacked_column"]["modifications"]
assert len(row) == 1
assert row[0]["operation"] == "insert_after"
assert row[0]["control_name"] == "small_achievements_button"
assert row[0]["value"] == {"optlevi_mods@pause.optlevi_pause_mods_panel": {}}

mods = pause["optlevi_pause_mods_button@common_buttons.light_text_button"]
assert mods["$pressed_button_name"] == "button.optlevi_open_pause_menu"
assert mods["$button_text"] == "optlevi.menu.short_button"
assert mods["size"] == [22, 24]
assert pause["optlevi_pause_mods_panel"]["size"] == [24, 26]
assert 0 < mods["$pressed_alpha"] < 1

shaders = pause["optlevi_shaders_button@pause.pause_button_template"]
assert shaders["$pressed_button_name"] == "button.optlevi_shaders_placeholder"
assert shaders["$button_text"] == "optlevi.shaders.button"

# The old store button must no longer be reachable through either UI slot.
assert pause["store_button_panel"]["controls"] == [{"shaders_button@pause.optlevi_shaders_button": {}}]
assert pause["store_button_small_with_tooltip"]["controls"] == [
    {"shaders_button@pause.optlevi_shaders_small": {}}
]
assert "button.menu_store" not in json.dumps(pause)
assert "marketplace" not in json.dumps(pause).lower()

bounds = {}
for name in ("kMinX","kMaxX","kMinY","kMaxY","kPauseMinX","kPauseMaxX","kPauseMinY","kPauseMaxY"):
    found = re.search(r"constexpr float\s+" + name + r"\s*=\s*([0-9.]+)f;", native)
    assert found, f"Missing native hit area constant: {name}"
    bounds[name] = float(found.group(1))

def hit(x, y, prefix="", w=1536, h=709):
    return (w*bounds["k"+prefix+"MinX"] <= x <= w*bounds["k"+prefix+"MaxX"]
        and h*bounds["k"+prefix+"MinY"] <= y <= h*bounds["k"+prefix+"MaxY"])

assert hit(436, 526), "Working main-menu Mods area moved"
assert not hit(436, 526, "Pause"), "Pause area overlaps start area"
assert hit(341, 610, "Pause"), "Compact pause Mods button expected beside the book"
assert not hit(450, 610, "Pause"), "Pause hit area must not cover adjacent help button"
assert not hit(450, 610), "Start area would intercept pause region"
assert not hit(1250, 610, "Pause"), "Dressing Room must remain independent"

assert "Target::Pause" in native and "Target::Start" in native
assert "isScreenCandidate(target)" in native and "openMenu();" in native

for name in ("en_US.lang","pt_BR.lang"):
    text = (ROOT / "resources/levi_api/texts" / name).read_text()
    assert "optlevi.menu.button=Mods" in text
    assert "optlevi.menu.short_button=M" in text
    assert "optlevi.shaders.button=Shaders" in text

archive = ROOT / "build/OptLevi-LeviAPI-0.2.0.mcpack"
if archive.is_file():
    with ZipFile(archive) as z:
        assert "ui/pause_screen.json" in z.namelist()
        assert "ui/start_screen.json" in z.namelist()
        assert json.loads(z.read("ui/pause_screen.json")) == pause

print("OptLevi pause UI contract, Marketplace replacement and start/pause hit areas: OK")
print("Shaders is intentionally a placeholder. Physical-device UI test still required.")
