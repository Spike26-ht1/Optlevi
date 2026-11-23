#!/usr/bin/env python3
"""OptLevi Iris import + GLES3 test bench structural tests.

Actual Android SAF import and driver compilation require the device test;
CI ensures the paths are real, connected, and clearly labeled.
"""
import json
from pathlib import Path

R=Path(__file__).resolve().parents[1]
ui=(R/"src/ui/OptLeviShaderScreen.inc").read_text()
gl=(R/"src/ui/OptLeviShaderGlesTest.inc").read_text()
android=(R/"src/android/ShaderImportBridge.cpp").read_text()
api=(R/"src/android/ShaderImportBridge.h").read_text()
prep=(R/"scripts/prepare-fabric-real-screen.py").read_text()
installer=(R/"scripts/prepare-iris-import.py").read_text()
fabric=(R/"src/ui/OptLeviFabricScreen.inc").read_text()
catalog=(R/"src/ui/OptLeviShaderCatalog.inc").read_text()
manifest=json.loads((R/"resources/levi_api/manifest.json").read_text())

assert manifest["header"]["version"]==[0,11,0]
assert '#include "OptLeviShaderScreen.inc"' in fabric
assert "drawOptLeviShadersScreen(display);" in fabric
assert "OptLeviShaderGlesTest.inc" in ui and "OptLeviShaderGlesTest.inc" in prep
assert "ShaderImportBridge.h" in prep
assert 'ofButton("##iris_import","Import Shader Pack"' in ui
assert "modmenu::android::shaderimport::openPicker()" in ui
assert "shaderimport::install();" in installer
assert "shaderimport::uninstall();" in installer
assert "src/ShaderImportBridge.cpp" in installer
assert "GLESv3 z" in installer
assert "nativeOnDocumentResult" in android
assert "openPreloaderDocumentPicker" in android
assert "RegisterNatives" in android
assert "paths::selfDir()" in android
assert "std::filesystem" in android
assert "kMaxPackSize=64ULL*1024*1024" in android
assert "copy_options::none" in android
assert "isZip(input)" in android
assert "pending.exchange(false)" in android
assert 'ofButton("##iris_cancel","Cancel"' in ui
assert 'ofButton("##iris_apply","Apply"' in ui
assert 'ofButton("##iris_done","Done"' in ui
assert 'ofButton("##iris_test","Test GLES Shader"' in ui
assert "glCompileShader" in gl and "glLinkProgram" in gl
assert "glFramebufferTexture2D" in gl and "glDrawArrays" in gl
assert "glGetShaderInfoLog" in gl
assert "inflateInit2(&zs,-MAX_WBITS)" in gl
assert "kMaxShaderBytes=256*1024" in gl
assert "glDeleteProgram(program)" in gl
assert "glBindFramebuffer(GL_FRAMEBUFFER,GLuint(savedFbo))" in gl
assert "glUseProgram(GLuint(savedProgram))" in gl
assert "glViewport(savedViewport[0]" in gl
assert 'optReadZipShader' in gl
assert 'optTranslateFinal' in gl
assert "Full Java shader loader" not in ui
assert "shaderworld::requestEnable(" in ui
assert "shaderworld::requestDisable()" in ui
assert 'optEnds(name,".zip")' in catalog and "optInspectZip" in catalog
assert "reloadMaterial(" not in ui and "glDrawArrays" not in ui
print("OptLevi 0.9: Android system picker, safe import, Iris UI and real GLES shader preview connected.")
print("Important: this proves code wiring only; world shader integration remains separate.")
