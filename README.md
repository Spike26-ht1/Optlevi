# OptLevi

OptLevi is an Android-focused Minecraft Bedrock modding project. The current experimental direction is a **Fabric Mod Menu-like native mod browser** opened from a **vanilla-styled Bedrock JSON UI button**.

## Project components

- `LeviLauncher-Android/` — pinned upstream LeviLauncher Android foundation.
- `ModMenuBE/` — pinned upstream ModMenuBE 0.2.1 codebase for native mod discovery/settings; OptLevi applies its own UI modifications at build time.
- `resources/levi_api/` — minimal JSON UI resource pack. Its **Mods** button is placed under the Dressing Room area, with event ID `button.optlevi_open_menu`.
- `scripts/prepare-optlevi.py` — rename/package the original native code under OptLevi identity.
- `scripts/prepare-levi-api.py` — remove the previous floating launcher button, style the native menu.
- `scripts/build-resource-pack.py` — generate `OptLevi-LeviAPI-0.2.0.mcpack`.
- `.github/workflows/optlevi-android.yml` — Android ARM64 `.levipack` and JSON UI `.mcpack` validation.
- `docs/levi-api-resource-ui.md` — design, Playback reference, known Android limitation and device tests.

## Current compatibility status

The original ModMenuBE test pack was successfully tested on an Android device by the project owner. That verification **does not** extend to the rewritten OptLevi UI.

The OptLevi JSON UI pack has a valid resource-pack manifest, vanilla-style button template and event identifier. **The event ID is not yet connected to the Android native screen-controller hook.** The Playback 0.4.1 reference demonstrates that pattern on LeviLamina, but Android LeviLauncher uses a different Preloader SDK and no stable equivalent callback has been identified for this Minecraft revision. The button's click must not be described as functional until the native bridge and on-device tests are complete.

## Clone

```sh
git clone --recurse-submodules https://github.com/Spike26-ht1/Optlevi.git
```

## Upstream acknowledgements

| Component | Upstream | Revision | License |
| --- | --- | --- | --- |
| LeviLauncher Android | https://github.com/LiteLDev/LeviLauncher-Android | `d544df8cafab731ba7686dfb88951602f13293ef` | Apache-2.0 |
| ModMenuBE | https://github.com/AFAlimru/ModMenuBE | `dcca382ec0a15e3849e9a65dcdf8fe64480d3a71` | MIT |

OptLevi's root MIT license applies only to OptLevi-authored material. Original upstream licenses and notices remain with their respective projects.
