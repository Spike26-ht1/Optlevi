# Claude task list — real world shader compatibility

## P0 — fix broken current branch

1. Get exact source snapshot and init submodules.
2. Reproduce `python3 scripts/test-lab-shaders.py` and the CI host-GLES failures. Find why native-depth contact shading test produces the wrong color. Preserve expected real GPU depth edge behavior.
3. Check the new `glBindFramebuffer` hooks for reentrancy, lifecycle, correct GLES3 entrypoints, and GL-state restoration. Ensure a plugin's intercepted draw cannot recursively call itself or replace arbitrary HUD framebuffers.
4. Run `python3 scripts/test-java-bsl.py`, `python3 scripts/test-java-world-test.py`, `python3 scripts/test-lab-shaders.py`, and package CI. Quote logs. Do not claim successful builds until Actions finishes.

## P1 — real native Bedrock world integration (the hard blocker)

1. On owner device, use `Shaders -> Log` to inspect actual 26.60/26.62 terrain draw program uniforms and linked VAO attributes. Avoid hardcoded offsets.
2. Select a stable capture point *before* HUD/postprocessing; identify the real game world FBO and its dimensions and attachments. Preserve depth/stencil, multisample behavior and GPU state.
3. Obtain actual vertex/index data, position and model transform; map to the Java input ABI; preserve instance draw semantics. Determine actual terrain block/material IDs and texture/lightmap coordinates from engine data, not derived guesses.
4. Hook phase boundaries so mapped original `gbuffers_terrain`, entities, hand, water, shadow casters execute in correct order, then `renderJavaWorldScene` is called using **actual** native draws; avoid duplicate vanilla/world rendering or feedback loops.
5. Ensure the original Java pack's actual uniforms, sampler bindings and attachment formats match each pass. Fail transparently on unsupported inputs.
6. Implement real light-space shadow caster replay and verify depth, alpha cutout, translucent color and camera conventions for BSL. Separate this from experimental screen-space contact shading.

## P2 — target BSL 10.1.8 and performance

1. Select BSL in user interface and actually enter the game; record first failing shader program or resource by name with GL logs.
2. Test real terrain lighting first, then sun shadows/opaque transparency, water, deferred, depth history, final composite. Check temporal artifacts, near-player fallback, sky/horizon and motion.
3. Keep the original shader ZIP intact; use progressive BSL profile settings and device-real FPS/per-frame GPU metrics where supported.
4. Validate safe disable/unload, dimension change, resize/surface recreation, switching shaders and absence of crashes.

## Acceptance criteria

- Native render path demonstrates the **actual world** materially changes due to a Java geometry program, not only an already composed framebuffer effect.
- Shadow depth has real light-space caster geometry and measurable visual occlusion; not a fabricated 2D darkening.
- BSL test produces correct world lighting, water and sun shadows with stable play, verified by user's on-device capture and log.
- Full ARM64 CI and host tests succeed, package downloadable, no `main` writes.

## Commands

```sh
git clone --recurse-submodules -b feature/java-composite-pipeline-v13 https://github.com/Spike26-ht1/Optlevi.git
cd Optlevi
python3 scripts/test-java-bsl.py
python3 scripts/test-java-world-test.py
python3 scripts/test-lab-shaders.py
python3 scripts/build-lab-shaders.py
# Android ARM64 CI: .github/workflows/optlevi-android.yml
```

Some tests require Linux EGL/GLES development libraries and a software OpenGL ES implementation; ARM64 packaging needs the pinned Android SDK, NDK, LeviLauncher and ModMenuBE.


**Current template update:** the historical source snapshot above is preserved for reference. The active branch now builds the checked-in `native-template/` using `build/native-template/` as the disposable staging tree. The external menu-template submodule was removed; native config ABI and original legal notices remain. See `native-template/PROVENANCE.md`.
