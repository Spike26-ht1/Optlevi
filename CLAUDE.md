# OptLevi — Claude development handoff

> START HERE. Source snapshot: `1987a576413a504fcf1df3ffb541fee7f69a73e6` (branch `feature/java-composite-pipeline-v13`, PR #19). This handoff is not proof of working BSL gameplay.

**Mission:** Make **real Java/Iris-style shaders** render Minecraft Bedrock world geometry, water, deferred outputs, and sun-cast shadows under LeviLauncher Android GLES3. First repair the actual world integration and its failing host test; then test BSL 10.1.8 on hardware. Do not confuse a successful fullscreen color postprocess with the Java shader pipeline.

**CRITICAL DISTINCTION:** RenderDragon **`.material.bin` shaders**, modified via Lazurite / resource packs / Custom Buffers or MBL2, are a **different system**. The requested target is an independent native Java shader pack loader (ZIP source programs, GLSL translator, GLES3 programs, native world draw interception and buffers). Do not convert the project into a `.material.bin` pack and do not edit existing materials to claim BSL compatibility.

Read in order:
1. `handoff/README.md`
2. `handoff/ARCHITECTURE.md`
3. `handoff/STATUS_AND_EVIDENCE.md`
4. `handoff/NEXT_STEPS.md`
5. `handoff/FILE_MANIFEST.md`
6. Source files listed in the architecture document

**Ground rules:** Work on the existing branch or a child feature branch, keep commits small and reproducible, do not commit to `main`, do not merge without owner authorization. Run the host GLES tests and ARM64 Actions, quote actual logs, and do not report in-world success until the owner demonstrates it on a real Android device. Avoid screen-only overlays masquerading as Java shader integration. Do not add fabricated normals, shadow maps, depth, or G-buffers. Diagnose and preserve fail-closed behavior when required world inputs are absent.

Full tracked source snapshot: https://github.com/Spike26-ht1/Optlevi/archive/1987a576413a504fcf1df3ffb541fee7f69a73e6.zip
Repo: https://github.com/Spike26-ht1/Optlevi
PR: https://github.com/Spike26-ht1/Optlevi/pull/19
