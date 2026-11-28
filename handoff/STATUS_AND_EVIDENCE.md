# Current status and evidence (captured 2026-11-27)

Branch snapshot: `1987a576413a504fcf1df3ffb541fee7f69a73e6`.

## What is actually proven

- The repository contains 103 tracked files (~625,551 bytes) at this snapshot. Submodules are pinned references, not included in source archive downloads.
- ARM64 Actions and host GLES tests were successful on earlier commits, e.g. `397a3ac3181ba6f5819848b9da9a6c77c53629be`: https://github.com/Spike26-ht1/Optlevi/actions/runs/45907265018
- Original `OptLevi-Lab-WorldFX` postprocessor runs on device: user's screenshot shows a selected WorldFX ZIP and UI text **"Java composite world graph rendered ... (3 passes)"**, 807 frames, waiting 0, errors 0, CPU ~0.693 ms. The user explicitly reported **no visual world shadows or BSL effect**. These counters only represent successful backbuffer composite submission, not the Java world renderer.
- GLSL program parsing, shadow-map FBO generation and phase coordination have host test coverage, but **real stock Minecraft world geometry/shadow output is NOT established**.
- The owner's screenshot shows BSL, Bliss, WorldFX and Photon ZIPs in menu; only WorldFX is selected there. Do not infer BSL loaded from seeing it listed.

## Most recent CI is red — investigate this first

- Latest commit `1987a576413a504fcf1df3ffb541fee7f69a73e6` (status checked): both workflows failed.
  - https://github.com/Spike26-ht1/Optlevi/actions/runs/45913833395
  - https://github.com/Spike26-ht1/Optlevi/actions/runs/45913827973
- Failure step: `Execute original OptLevi Lab ZIPs through Java loader and GLES`.
- Exact failing test message:
  `Native depth contact shading did not darken geometry edge | Native terrain depth effect rendered: OptLevi-Lab-Terrain-0.1.zip (screen-space contact shading)`
- The test terminates with a subprocess exit status 1 in `scripts/test-lab-shaders.py`, run by `tests/lab-shaders-gles.cpp`.
- That is an incorrect **GPU pixel result**, not an automatic proof the hook runs in real Bedrock. Do not "fix" by removing/weaken the assertion; inspect depth reconstruction, reversed compare/occlusion sign, sampling kernel, test depth geometry and source/destination FBO ownership.

## Other known limitations

- Native hook currently identifies programs by a limited observed uniform signature; target 26.60/26.62 may use different signatures/layout.
- No confirmed ownership of stock Bedrock block ID/state, per-material albedo atlas, chunk-local transforms, correct sky/block light and every render phase.
- No proven full BSL terrain/water/shadow gameplay.
- The experimental `Lab Terrain` custom effect is not equivalent to BSL sun-cast shadows and must be identified as such.
- The prior WorldFX "rendered" status was misleading; a newer commit improved its description but latest native-terrain CI failed.
- Full Iris features, some special formats/rendering directives and non-Overworld dimensions remain partial.

## Device evidence requested for next test

From inside a world, open **Shaders -> Log -> Copy renderer log**. Collect the draw hook count, recognized terrain program count, observed uniform names, vertex attribute layout, native shader error/phase counters, FPS, game version and platform. If a clear in-world screenshot shows no effect, report that result without attributing it to the shader ZIP alone.
