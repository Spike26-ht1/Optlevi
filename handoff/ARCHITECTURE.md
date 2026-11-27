# Current architecture — OptLevi

## Two entirely different shader ecosystems

| Independent Java shader engine (primary target) | RenderDragon material.bin pathway (separate) |
| --- | --- |
| Original Java shader ZIP (`shaders/world0/*.vsh/.fsh`, `shaders.properties`, includes) | Bedrock `.material.bin` binary materials |
| Iris/OptiFine compatibility parsing, preprocessor, GLSL -> GLES3 translation | Lazurite unpack/pack, resource materials and RenderDragon compiled stages |
| Native GL program compilation, geometry G-buffer and deferred/shadow graph | Engine material replacement via MBL2 / renderer mods |
| Needs real Bedrock meshes, semantics, texture/lightmap, camera, draw phases | Material-specific definitions and engine ABI |
| Must not fake depth/shadows from one captured screen image | Does not magically implement Java's GBuffers/Iris graph |

These paths are architecturally distinct. The owner explicitly postponed material.bin work. Do NOT conflate Custom Buffers with the new Java loader.

## High-level dataflow

```
LeviLauncher-Android + ModMenuBE (pinned Git submodules)
   -> native LeviPack .so + Levi API resource UI
   -> EGL/ImGui shaders menu + import:
      src/ui/OptLeviShaderScreen.inc / OptLeviJavaPack.inc / OptLeviShaderGlesTest.inc
      src/android/ShaderImportBridge.cpp (Android document picker)
   -> Java shader ZIP parsing + includes, profiles, computed uniforms
      ShaderPackProfiles.h / ShaderPackUniforms.h / ShaderMeshMapping.h
   -> shaderworld::Pass graph, linked original GLES3 shader programs
      src/android/ShaderWorldRuntime.{h,cpp}
   -> native GL interception:
      src/android/ShaderRenderBridge.{h,cpp}
      draw hooks glDrawElements/Arrays/Instanced, glBindFramebuffer
      -> native geometry and world inputs (STILL INCOMPLETE)
   -> shadow geometry / depth snapshots / opaque/deferred/translucent/
      composite/final phases and GLES backbuffer presentation
```

## Key files and contracts

- `src/ui/OptLeviJavaPack.inc`: bounded ZIP loading (path/safe expansion/import), original shader pack data.
- `src/ui/OptLeviShaderGlesTest.inc`: source translator and `optBuildJavaPipeline`; profiles, program enables, geometry stage pairs, alpha tests, routes, buffer size directives.
- `src/android/ShaderWorldRuntime.h`: public `Pass`, `GeometryProgram`, `WorldUniform`, `GeometryDraw`, `WorldScene`, `renderJavaWorldScene`, `renderShadowScene`, `renderNativeTerrainPhase`.
- `src/android/ShaderWorldRuntime.cpp`: program lifetime, EGL/context/surface guards, FBOs, reflection, depth/color snapshots, history, half-res colortex buffers, deferred/composite render and native phase.
- `src/android/ShaderRenderBridge.cpp`: native draw hooks and native signature inspection; observes `u_viewProj`, `ViewPositionAndTime`, `s_MatTexture`. Explicit OptLevi marker uniforms are NOT present in ordinary stock materials.
- `src/android/ShaderCameraMath.h`: finite projection/inversion, Java shadow light-space camera.
- `src/android/ShaderMeshMapping.h`: block.properties + packed-quad Java vertex conversion; requires verified block identity and matching GPU vertex semantics.
- `src/android/ShaderPackUniforms.h`: bounded `shaders.properties` expression engine, Java custom uniforms and `smooth` history.
- `src/android/ShaderPackProfiles.h`: shader-pack quality profiles / inherited declarations.
- `src/ui/OptLeviShaderScreen.inc`: user-facing selection, test/Apply, renderer logs and metrics.
- `src/android/ShaderImportBridge.cpp`: safe Android document picker import.
- `scripts/prepare-java-world-test.py`: injects native shader runtime+hooks into upstream native build from submodules.
- `scripts/build-lab-shaders.py`: creates original Lab ZIP test shader packs.
- `scripts/package-optlevi.py`: bundles `libOptLevi.so`, shaders and Levi API UI into complete LeviPack.
- `.github/workflows/optlevi-android.yml`: host Java ZIP/GLES regression + Android ARM64 build + artifacts.

## World phase contract

- Actual geometry needs the original `gbuffers_* / shadow*` vertex/fragment programs and correct mapped VAO attributes (positions, color, normal, UVs, entity/material IDs, tangents, lightmap).
- Correct phase order: world/opaque -> pre-hand/pre-translucent depth snapshots -> deferred -> translucent/water -> composite/final, plus separate sun/light-space shadow scene.
- `renderJavaWorldScene` is currently callable with an explicit `WorldScene` but **no verified stock-Bedrock mesh producer calls it**. Its GLES tests use controlled synthetic meshes.
- `renderShadowScene` renders real geometry into shadow FBOs **only when supplied** real caster batches and actual camera/light uniforms. It cannot conjure scene shadow data.
- `renderNativeTerrainPhase` is a separate custom effect that samples an observed native framebuffer depth. It is **screen-space contact shading**, not BSL shadow maps.
- The legacy `beforeSwap` path can composite the *already rendered* game frame, including HUD. Its success counters do not prove GBuffers or Java shaders are running during actual world draws.

## Loader and artifacts

Repository project targets Android ARM64 GLES3; the native `.so` is inside `.levipack`. Levi API resource pack adjusts main/pause menu, separate from shaders. Development branch contains no proprietary Minecraft engine binaries or full third-party shader bundles. BSL 10.1.8 belongs to the user's test inputs and should be selected for actual validation; never paste entire copyrighted shader contents into repository.

See `docs/native-renderer-bridge.md` for details and `tests/java-composite-gles.cpp` for validated micro-fixtures.


**Current template update:** the historical source snapshot above is preserved for reference. The active branch now builds the checked-in `native-template/` using `build/native-template/` as the disposable staging tree. The external menu-template submodule was removed; native config ABI and original legal notices remain. See `native-template/PROVENANCE.md`.
