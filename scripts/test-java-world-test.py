#!/usr/bin/env python3
"""Experimental Java final/composite world pass: source and safety checks.

Real OpenGL / RenderDragon effects require physical-device validation.
"""
from pathlib import Path
import json

R=Path(__file__).resolve().parents[1]
runtime=(R/"src/android/ShaderWorldRuntime.cpp").read_text()
api=(R/"src/android/ShaderWorldRuntime.h").read_text()
ui=(R/"src/ui/OptLeviShaderScreen.inc").read_text()
prep=(R/"scripts/prepare-java-world-test.py").read_text()
fabricprep=(R/"scripts/prepare-fabric-real-screen.py").read_text()
version=json.loads((R/"resources/levi_api/manifest.json").read_text())
assert version["header"]["version"]==[0,12,0]
for symbol in (
 "void requestEnable(std::string translatedFragment",
 "void requestDisable()",
 "void beforeSwap(int width,int height)",
 "Status status()",
 "void stop()",
 "glCompileShader",
 "glLinkProgram",
 "glGetActiveUniform",
 "Unsupported world shader uniform",
 "glCopyTexSubImage2D",
 "glDrawArrays(GL_TRIANGLES,0,3)",
 "GL_FRAMEBUFFER_BINDING" if False else "GL_READ_FRAMEBUFFER_BINDING",
 "glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(readFbo))",
 "glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(drawFbo))",
 "glUseProgram(GLuint(program))",
 "glViewport(viewport[0],viewport[1],viewport[2],viewport[3])",
 "glColorMask(colorMask[0]",
 "glDisable(GL_SCISSOR_TEST)",
 "glDisable(GL_DEPTH_TEST)",
 "sourceTex",
 "requestedActive=false",
 "ShaderWorldRuntime.h",
):
    assert symbol in (runtime+api),symbol
for symbol in (
    "shaderworld::requestEnable",
    "shaderworld::requestDisable",
    "optReadJavaShader(*selected,src,origin)",
    "optTranslateFinal(src)",
    "Experimental world postprocess disabled",
    "Compiling selected Java final pass",
):
    assert symbol in ui,symbol
for symbol in (
    'src/ShaderWorldRuntime.cpp',
    "ShaderWorldRuntime.h",
    "shaderworld::beforeSwap(worldW,worldH)",
    "shaderworld::stop()",
):
    assert symbol in prep,symbol
assert 'ShaderWorldRuntime.h' in fabricprep
assert "glBindFramebuffer(GL_FRAMEBUFFER,0)" in runtime
assert "for(char const* name" in runtime
assert "requestEnable" in api and "requestDisable" in api
print("OptLevi 0.10: opt-in Java final-pass GPU postprocessor and GL-state restore checks OK")
print("No complete Iris/OptiFine pass pipeline is claimed by this test.")
