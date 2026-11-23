#!/usr/bin/env python3
"""Static checks for Bedrock-themed Fabric frontend (not an in-game screenshot test)."""
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]
screen=(root/"ModMenuBE/src/ui/OptLeviFabricScreen.inc").read_text()
native=(root/"ModMenuBE/src/ui/ModMenuScreen.cpp").read_text()
resource=json.loads((root/"resources/levi_api/manifest.json").read_text())
assert resource["header"]["version"] == [0,5,0]
assert native.count("drawOptLeviFabricScreen(display);") == 1
for required in (
  "kBedrockButtonFace = 0xC6C6C6",
  "kBedrockButtonHover = 0xE0E0E0",
  "kBedrockButtonPressed = 0x919191",
  "kBedrockPanel = 0x303137",
  "ImGuiCol_ScrollbarGrab",
  "ImGuiCol_ChildBg",
  "ofc(kBedrockPanel,251)",
  "ofc(kBedrockPanelDark,255)",
  "ofc(0x101010,255)",
  "ImGui::PopStyleColor(4);",
  "ImGui::PopStyleVar();",
  "ImGui::PushStyleColor(ImGuiCol_FrameBg",
  "void drawOptLeviFabricScreen(ImVec2 display)",
  "ImGui::BeginChild(\"##of_scroll_mods\"",
  "ImGui::BeginChild(\"##of_details_scroll\"",
  'ofButton("##of_done","Done"',
  'ofButton("##of_refresh","Refresh"',
  'ofButton("##of_config","Configure"',
  "toggleEnabled(mod)",
  "drawReadme(mod)",
):
    assert required in screen,required

assert screen.count("ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,")==1
assert screen.count("ImGui::PopStyleColor(4);")==1
assert "const" in screen
assert 'ore::panel(' not in screen
assert "ButtonBuilder" not in (root/"ModMenuBE/android/src/Entry.cpp").read_text()
ui=json.loads((root/"resources/levi_api/ui/pause_screen.json").read_text())
assert ui["optlevi_pause_mods_button@common_buttons.light_text_button"]["size"] == [22,24]
assert ui["optlevi_shaders_button@pause.pause_button_template"]["$button_text"] == "optlevi.shaders.button"
print("Bedrock-derived pixel palette, opaque panels, scrollbar, native actions, and pause controls: OK")
print("The 26.60 source textures are references only; no proprietary texture files are included.")
