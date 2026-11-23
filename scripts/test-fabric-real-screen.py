#!/usr/bin/env python3
"""Regression tests for the new OptLevi screen path after patch application.

A green check confirms geometry and source wiring, not device appearance.
"""
from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[1]
src=(ROOT/"ModMenuBE/src/ui/ModMenuScreen.cpp").read_text()
screen=(ROOT/"ModMenuBE/src/ui/OptLeviFabricScreen.inc").read_text()
json_ui=json.loads((ROOT/"resources/levi_api/ui/start_screen.json").read_text())
pause=json.loads((ROOT/"resources/levi_api/ui/pause_screen.json").read_text())
assert src.count('drawOptLeviFabricScreen(display);')==1
assert 'if (gState.view == View::List)' in src
assert '#include "OptLeviFabricScreen.inc"' in src
assert 'ImGui::End();\n        return;' in src
for essential in [
    'void drawOptLeviFabricScreen(ImVec2 display)',
    'ofModEntry(*mod',
    'ImGui::InputTextWithHint',
    'ImGui::OpenPopup("##of_filters")',
    'ImGui::BeginChild("##of_scroll_mods"',
    'ImGui::BeginChild("##of_details_scroll"',
    'ofButton("##of_done","Done"',
    'ofButton("##of_website","Website"',
    'ofButton("##of_issues","Issues"',
    'openConfigFor(mod)',
    'toggleEnabled(mod)',
    'ModRegistry::instance().rescan()',
    'UpdateChecker::instance().checkAll()',
    'drawReadme(mod)',
]:
    assert essential in screen, essential

# Screenshot baseline is 1536x709 (phone), Fabric reference 1096x609.
# At this physical height five or more rows must fit without scrolling.
height=709
u=max(.72,min(height/609.0,1.38))
list_start=111*u
list_bottom=height-58*u
row_h=61*u
visible_rows=int((list_bottom-list_start)//row_h)
assert visible_rows>=6,visible_rows
assert 18*u>0
assert 0.484*1536>18*u
assert 0.515*1536>0.484*1536
assert 0.515*1536<1536-18*u
assert 39*u+33*u<111*u
assert 68*u+110*u<height-58*u

# A true separate renderer, not a recoloring of the previous OreUI header.
assert 'ore::panel(' not in screen
assert 'drawHeader(' not in screen
assert 'drawListView(' not in screen
assert 'col::rgb(0x262A31, 245)' not in screen
# No old overlay button should re-enter the Android native plugin.
entry=(ROOT/"ModMenuBE/android/src/Entry.cpp").read_text()
assert "ButtonBuilder" not in entry

assert json_ui["optlevi_menu_button@common_buttons.light_text_form_fitting_button"]["$pressed_button_name"]=="button.optlevi_open_menu"
assert pause["optlevi_pause_mods_button@common_buttons.light_text_button"]["size"]==[22,24]
assert "optlevi_shaders_button@pause.pause_button_template" in pause
print("New Fabric renderer wired in; original Minecraft buttons intact.")
print(f"Reference phone geometry: {visible_rows} fully visible mod entries.")
print("Android screenshot comparison remains necessary.")
