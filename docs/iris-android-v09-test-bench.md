# OptLevi 0.9.0 — Iris-style Android import and GLES shader test bench

## Exact scope and verified platform contracts

**Test environment:** Minecraft Bedrock Android 26.62 in LeviLauncher, OptLevi native ARM64. OptLevi 0.8.0 already detected Java/Iris/OptiFine ZIPs but did not import or compile them.

The pinned launcher `d544df8...` provides `MinecraftActivity.openPreloaderDocumentPicker(mimeType)`, which invokes Android's `ACTION_OPEN_DOCUMENT`. The Activity copies the chosen document to its private cache and calls `PreloaderInput.nativeOnDocumentResult(success,path,name,error)`, **then deletes the temporary copy immediately**. In pinned Preloader SDK `26ff71e...`, the document callback is not registered for a native mod. OptLevi 0.9 registers just that JNI method through `RegisterNatives`, without overriding touch/keyboard callbacks, and copies the temporary file synchronously before Android deletes it.

## Implemented

- Iris-style centralized shaderpack list, enable/disable state for **test previews**, refresh, Cancel/Apply/Done, and clearly visible **Import Shader Pack**.
- Android system Documents picker (not a guessed `/sdcard` directory). Accepts `.zip`, `.mcpack`, and `.material.bin`; the file gets copied into `<OptLevi>/shaderpacks`.
- Import safeguards: basename sanitization, no overwrite of existing files, 64 MiB cap, magic check for ZIP archives, no untrusted archive extraction.
- An **explicit** `Test GLES Shader` button. It reads one candidate Java `shaders/final.fsh` / `composite.fsh` (from a loose folder or ZIP), bounds decompression to 256 KiB, tries a limited GLSL 120/330 -> GLES3 compatibility translation, **really calls** `glCompileShader` + `glLinkProgram`, renders an offscreen test frame into a 320×180 OpenGL ES framebuffer, and shows the driver compile/link log.
- A built-in diagnostic shader tests this rendering pipeline independently of third-party Java packs. It operates on a synthetic gradient, not Minecraft's 3D render.
- Restores OpenGL FBO/program/VAO/texture/viewport/blending/depth/scissor state after the preview, so it does not deliberately replace Minecraft materials.

## Not yet supported

- No arbitrary Iris Java `gbuffers` shader stage, full `shaders.properties` option processing, recursive #include compilation, framebuffer history, shadows, multiple color attachments, Minecraft camera/game uniforms, or renderer-level material reload.
- **No world shader injection.** A Java shader successfully compiling in a preview does **not** mean it changes Minecraft's graphics. `Apply` saves only the test selection and the screen explicitly explains this. The separate RenderDragon adapter is still needed.
- On Vulkan devices the OpenGL ES preview cannot run. The UI should report no suitable GLES context rather than claim success.
- Shader Pack Settings are not complete. Native rendering integration is not available on current Android Arke (public Arke is a Windows D3D12 LeviLamina mod). Magic Loader is useful for compatibility observations but its Android implementation has not been incorporated here.

## Functional Android test checklist

1. Install OptLevi 0.9.0 in place of 0.8.0 and launch Minecraft 26.62.
2. Open `Mods > Shaders` and tap **Import Shader Pack**. Android Documents picker should open. Choose a shader ZIP and return.
3. Confirm import status, rescan, file appears in pack list, and duplicated imports create a distinct filename rather than overwriting.
4. With **Vanilla / Off** selected, tap **Test GLES Shader**. The built-in diagnostic fragment should compile and show a small offscreen preview.
5. Select a simple pack that contains a self-contained `shaders/final.fsh`, tap **Test GLES Shader**, and open **Compiler log** if unsupported constructs cause a real GLSL failure.
6. Test **Cancel**, **Apply**, **Done**, settings navigation, repeat opening and closing.
7. Confirm gameplay remains unchanged: this is a GPU shader diagnostic, not the full Java RenderDragon loader.

Record only shader file metadata, compile diagnostics, and OptLevi version in issue reports. Do not upload Minecraft game binaries or private player data.

## Next engineering milestones

- Versioned `#include` and `shaders.properties` support with validated option UI.
- Test individual fragment passes using **real captured scene textures** and selected color/depth buffers instead of a synthetic gradient (requires security and GPU state testing).
- Renderer-specific RenderDragon/Custom Buffers integration with tested Minecraft 26.62 hook locations and exact graphics-backend detection.
- Correct uniform mapping and multi-pass pipeline with per-shader compatibility reporting, before declaring a fully working Android Iris/OptiFine shader loader.

Sources: owner-supplied Iris 1.11.7 package; pinned LeviLauncher/Preloader code; Arke project documentation, https://github.com/LiteLDev/Arke; Iris properties reference, https://shaders.properties/current/reference/shadersproperties/overview/.
