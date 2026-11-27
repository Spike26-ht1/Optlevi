# HANDOFF — OptLevi source and architecture

**Source of truth:** `Spike26-ht1/Optlevi`, `feature/java-composite-pipeline-v13` at `1987a576413a504fcf1df3ffb541fee7f69a73e6` before this documentation commit. The GitHub branch archive contains all the repository's **tracked** project files plus this handoff after commit. Git submodules are references, not embedded archives, and must be fetched separately with `git submodule update --init --recursive`.

## What the owner is asking for

- Implement a genuine Java/Iris-style shader loader for Minecraft Bedrock Android on LeviLauncher.
- Primary end-to-end compatibility target: BSL Shaders 10.1.8 (actual lit terrain, dynamic shadows, water and sky, not postprocessed screenshot).
- Experimental original Lab shader packs are useful as incremental verification targets, but are **not** the final renderer.
- Material.bin is a separate RenderDragon shader-pack path and is **not** the substitute for Java shader deployment.
- Preserve Mod Menu + shaders UI and the ARM64 LeviPack distribution workflow.
- Review latest native crash and shader diagnostics where provided; fix actual build/test failures.
- Work within PR #19 / feature branch; no main merge.

## Files in handoff

- `CLAUDE.md` — concise entrypoint/instructions.
- `handoff/ARCHITECTURE.md` — actual subsystem boundaries, dataflow, API.
- `handoff/STATUS_AND_EVIDENCE.md` — verified and unverified results, latest failures.
- `handoff/NEXT_STEPS.md` — implementation priorities / acceptance criteria.
- `handoff/FILE_MANIFEST.md` — exact tracked file paths/sizes as observed at source commit.
- Existing `docs/native-renderer-bridge.md` — detailed bridge design and historical notes.
- Existing source `src/android`, `src/ui`, `shaderpacks`, `tests`, `scripts`, `resources`, CI.

## Source snapshot

https://github.com/Spike26-ht1/Optlevi/archive/1987a576413a504fcf1df3ffb541fee7f69a73e6.zip

**Privacy:** This handoff is intended for collaborators authorized by the repository owner. Review code and any included screenshots before sharing outside the project.


**Current template update:** the historical source snapshot above is preserved for reference. The active branch now builds the checked-in `native-template/` using `build/native-template/` as the disposable staging tree. The external menu-template submodule was removed; native config ABI and original legal notices remain. See `native-template/PROVENANCE.md`.
