# OptLevi 0.10.0 — experimental Java fragment shader in-world test

This version builds upon OptLevi 0.9.0's real Android document picker, ZIP import, Iris-style screen and working GLES3 fragment compilation/preview.

## Implemented: optional in-world single-pass effect

`src/android/ShaderWorldRuntime.cpp` integrates with the **existing** game-only `eglSwapBuffers` hook used to present ModMenuBE/OptLevi's Android ImGui screen. It does not patch RenderDragon's binary or replace `.material.bin` resources.

When the user explicitly selects a Java-style ZIP/folder with `shaders/final.fsh` or `shaders/composite.fsh`, enables shaders and taps **Apply**:

1. Source extraction and bounded ZIP inflate (max 64 MiB ZIP / 256 KiB shader) reuse OptLevi's 0.9 loader.
2. OptLevi translates a small GLSL 120 compatibility subset (varying, texture2D, gl_FragColor, gl_TexCoord0) into GLES3.
3. The shader is queued and compiled **on the next game EGL swap frame**, never on the import callback thread.
4. The runtime verifies link success and rejects **unknown uniform names** that require Iris-only scene buffers or game state.
5. An existing GLES3 backbuffer is copied into a color texture, and the `final` fragment is drawn as one fullscreen postprocessing pass to the backbuffer before ImGui overlays.
6. The current GL program, VAO, draw/read framebuffer, viewport, active textures, PBO, depth/color write mask and GL enable states are restored.
7. Apply Off, disable Shaders or unload the mod to turn off the postprocess. If compilation, texture allocation or framebuffer copy fails, the effect shuts down and the status is shown in the Shaders screen.

The **Test GLES Shader** button still supports isolated 320×180 offscreen preview and compiler logs before touching the game image.

## Deliberate restrictions

This is a **real rendering effect for a restricted class of Java-style final/composite shaders**, not a complete Iris backend. Unsupported features include multiple gbuffer passes, MRT buffers, shadows, persistent framebuffers, depth uniforms, temporal history, geometry shader stages, shaderpack property options, recursive includes, and Java version-specific macros. An imported shaderpack being listed does not imply that its world pass will compile or render correctly.

Shader input is restricted to explicit user interaction. No auto-compilation or GPU shader activation occurs when a ZIP is merely imported. Existing Minecraft resource-pack injection, OptLevi ModMenu UI, and the native pixel-art buttons are preserved.

This code has only been compiled under Android ARM64 GitHub Actions. **Device testing is required** to establish actual world-postprocess performance and correctness on Minecraft Bedrock 26.62 and the player's graphics backend.

## Recommended device test (simple original shader)

Download the separate original `OptLevi-GLES-Shader-Test.zip`, containing `shaders/final.fsh` plus a minimal matching vertex shader. It gently desaturates a source texture, making it easy to tell if the rendering pass changes the game.

- Install OptLevi 0.10.0 and remove or disable the previous 0.9/0.8 native module.
- Open Mods > Shaders > Import Shader Pack, choose the test ZIP, and verify it appears in the list.
- Select the test pack. First press **Test GLES Shader**; the small preview should show a desaturated test gradient.
- With the selected pack and **Shaders: Enabled (experimental)**, press **Apply**, then Done. Return to the Minecraft world and look for reduced saturation.
- Open the Shaders screen and check for **Experimental GLES3 world pass active** or a specific driver diagnostic if unsupported.
- Select Off, Apply, then Done to restore ordinary Minecraft colors.
- Confirm the pause menu, menus, touch controls and game performance still behave correctly. If shaders cause GPU trouble, disable the OptLevi module in LeviLauncher.

## Backend context

Arke (https://github.com/LiteLDev/Arke) implements Java shader integration for Windows Direct3D 12, not the Android EGL/RenderDragon route used here. Magic Loader demonstrates Android compatibility but its renderer is not incorporated in this project. `Custom Buffers` may provide necessary future texture inputs; OptLevi 0.10 currently only reads the final onscreen color buffer.

## Next engineering milestones

- Tested supported-uniform matrix for Minecraft Bedrock 26.62, reliable frame capture on supported GPUs, and performance counters.
- Proper `#include` and `shaders.properties` handling; expand shaderpack settings and compiler diagnostics.
- Resolve texture/depth/shadow inputs, separate Java framebuffers, multi-pass configuration and resource lifetime.
- Per-backend support (OpenGL ES vs Vulkan) and stable version-validated RenderDragon integration.

Do not claim arbitrary Iris/OptiFine Java shaderpacks work until these steps are implemented and verified.
