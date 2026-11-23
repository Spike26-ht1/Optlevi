#!/usr/bin/env python3
"""OptLevi shader browser static contract checks (not proof of shader loading)."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ui = (ROOT/"src/ui/OptLeviShaderScreen.inc").read_text(encoding="utf-8")
fabric = (ROOT/"src/ui/OptLeviFabricScreen.inc").read_text(encoding="utf-8")
prep = (ROOT/"scripts/prepare-fabric-real-screen.py").read_text(encoding="utf-8")
catalog = (ROOT/"src/ui/OptLeviShaderCatalog.inc").read_text(encoding="utf-8")
manifest = json.loads((ROOT/"resources/levi_api/manifest.json").read_text())

assert manifest["header"]["version"] == [0,8,0]
assert '#include "OptLeviShaderScreen.inc"' in fabric
assert "drawOptLeviShadersScreen(display);" in fabric
assert 'ofButton("##of_shaders","Shaders"' in fabric
assert "OptLeviShaderScreen.inc" in prep

# Do not fake shader activation or break the existing Fabric Mods frontend.
for contract in (
    "paths::selfDir()", ' / "shaderpacks"',
    "std::filesystem::directory_iterator", "create_directories",
    "gOptCustomBuffersFound", "gOptShadersVisible = false;",
    "Backend: Android OpenGL ES / RenderDragon",
    "Java Iris / Arke backend: not available on Android",
    "Detection only - no shader was loaded or activated.",
    'ofButton("##shader_back","Back to Mods"',
    'ofButton("##shader_refresh","Refresh"',
    'ofButton("##shader_done","Done"',
):
    assert contract in ui, contract
for contract in ('ofButton("##of_done","Done"', "ModRegistry::instance().rescan()",
                 "openConfigFor(mod)", "toggleEnabled(mod)"):
    assert contract in fabric, contract
for required in ('optEnds(name,".zip")', 'optEnds(name,".mcpack")',
                 'optEnds(name,".material.bin")', 'optInspectZip',
                 'optClassify', 'optExportPack', 'copy_options::skip_symlinks'):
    assert required in catalog, required
assert 'ofButton("##shader_export","Export"' in ui
assert 'OptLeviShaderCatalog.inc' in prep
assert "reloadMaterial(" not in ui
assert "glCompileShader" not in ui

print("OptLevi 0.7 shader browser source integration and honest backend state: OK")
print("The shaders archive browser is ready for ARM64 compilation, not Java shader loading.")
