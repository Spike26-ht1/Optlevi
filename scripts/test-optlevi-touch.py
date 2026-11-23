#!/usr/bin/env python3
"""Fast static checks for the calibrated OptLevi Android Mods touch bridge."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
cpp = (ROOT / "src/android/GameUiTouchBridge.cpp").read_text(encoding="utf-8")
ui = json.loads((ROOT / "resources/levi_api/ui/start_screen.json").read_text(encoding="utf-8"))

def fraction(name: str):
    match = re.search(r"constexpr float " + name + r" = ([0-9.]+)f;", cpp)
    if not match:
        raise AssertionError(f"Missing touch bound {name}")
    return float(match.group(1))

l, r = fraction("kMinX"), fraction("kMaxX")
t, b = fraction("kMinY"), fraction("kMaxY")
assert 0 <= l < r <= 1 and 0 <= t < b <= 1

def hit(x: float, y: float, w=1536.0, h=709.0):
    return l * w <= x <= r * w and t * h <= y <= b * h

assert hit(436, 526), "Owner screenshot Mods button center must be tappable"
assert hit(387, 510), "Owner screenshot Mods button left edge must be tappable"
assert not hit(750, 300), "The Play button must not trigger OptLevi"
assert not hit(1270, 582), "The Dressing Room button must not trigger OptLevi"
assert not hit(430, 350), "The Options button must not trigger OptLevi"

button = ui["optlevi_menu_button@common_buttons.light_text_form_fitting_button"]
assert button["$pressed_button_name"] == "button.optlevi_open_menu"
assert ui["start_screen_content"]["modifications"][0]["control_name"] == "skin_panel"

for required in (
    "pl::input::registerTouchCallback(onTouch)",
    "openMenu();",
    "isHudScreenOpen",
    "isPauseMenuOpen",
    "gCapturedPointer.store(-1)",
):
    assert required in cpp, required

print("Mods button original layout and calibrated Android touch bounds: OK")
print("Note: no test can prove actual in-game click delivery or menu visibility")
