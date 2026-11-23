# OptLevi 0.7 — Android shaders research and test scope

Research date: 2026-11-23. This branch starts from the OptLevi 0.6 texture
renderer commit 3483f07851d674dfe4e354951ebd075a6a600b8d, **not main**.

## Verified upstream projects

| Project | Repository | Relevant behavior | Platform constraints |
| --- | --- | --- | --- |
| Arke | https://github.com/LiteLDev/Arke | Runs Java Iris / OptiFine packs in Bedrock using Direct3D 12; README defines IS_ARKE and ARKE_VERSION macros | Release metadata lists \`win-x64\` and LeviLamina 26.60.*; README lists Android support under Plans. Public repo at time of research publishes project docs/assets but does not expose the implementation source. |
| Custom Buffers | https://github.com/fairytail-s/custombuffer | Android LeviLauncher buffers \`s_ScreenBuffer\`, \`s_SceneDepthBuffer\`, shadow map samplers; work is conditional on actual shader use | README lists Android 9+, arm64-v8a and Minecraft 26.52. Do not assume 26.62 compatibility. |
| RenderDragonShaderLoader | https://github.com/honeymirror/RenderDragonShaderLoader | Iris-style shader-pack settings, scanning .zip packs, metadata and material reload pipeline | Documents Windows x64 and Bedrock 1.26.51 / LeviLamina 26.51.7; not an Android module. |
| LeviLauncher Android | https://github.com/LiteLDev/LeviLauncher-Android | Android native mod loading, Preloader input callbacks | Existing OptLevi ARM64 foundation. |

**Do not treat a Java GLSL ZIP as a Bedrock RenderDragon material.**
Arke's D3D12 pipeline is not a drop-in GLES3/Android engine. A real port
requires corresponding backend support, shader translation, uniforms,
passes, buffer lifecycle and game-version hooks. Custom Buffers supplies
useful scene textures but does not itself compile arbitrary Iris packs.

## Implemented in this test branch

- New, independent Iris-inspired \`src/ui/OptLeviShaderScreen.inc\`.
- Reachable through a **Shaders** button at the top of OptLevi's Mods UI.
- Creates \`<native OptLevi mod directory>/shaderpacks/\` on first scan.
- Scans up to 200 regular files ending in \`.zip\` or \`.mcpack\`
  without interpreting or executing them.
- Off/Vanilla option; archive selection for inspection **does not
  activate the pack**.
- Displays Custom Buffers **directory detection only** (not API readiness).
- Shows the Android backend and a clear "no shader loaded" notice.
- Refresh, Back to Mods, Done actions with native textured buttons.
- Existing start/pause Mods and Shaders placeholder JSON controls,
  full Fabric Mods frontend and 200x200 icon are retained.

## Explicitly not implemented

- Java/OptiFine/Iris shader compilation or execution on Android.
- RenderDragon material redirection, GPU shader pipeline injection.
- Custom Buffers integration beyond presence detection.
- Actual shader pack activation, configuration, hot reload or persistence.
- Direct pause-menu Shaders touch binding: it remains a placeholder until a
  reliable Bedrock controller event or device-verified native geometry is found.

## Reproducible checks

1. CI applies pinned ModMenuBE integration and copies the shader screen.
2. \`python3 scripts/test-shaders-screen.py\` verifies wiring and no fake
   compile/reload paths.
3. Existing atlas, pause, Mods and touch contracts are tested unchanged.
4. CMake cross-compiles ARM64 Android module and packages complete LeviPack.
5. User tests opening Mods -> Shaders inside Minecraft 26.62, scanning from
   mod-local \`shaderpacks/\`, refreshing, navigating Back and Done, and reports
   any crashes or rendering issues.

CI confirms compilation and assembly; it cannot verify in-game behavior or
shader rendering. Do not merge to main until the phone test is successful.
