# OptLevi

OptLevi is an Android-focused Bedrock enhancement project inspired by the OptiFine/Forge ecosystem.

## Repository layout

- `LeviLauncher-Android/` — official LeviLauncher Android source, tracked as a Git submodule.
- `ModMenuBE/` — upstream ModMenuBE source, tracked as a Git submodule.
- `patches/modmenube/` — OptLevi compatibility patch for the selected LeviLauncher/Preloader revision.
- `docs/modmenu-android.md` — build and device-test instructions.
- `.github/workflows/modmenu-android.yml` — ARM64 Android build validation.

## Upstream components

| Component | Upstream | Pinned revision | License |
| --- | --- | --- | --- |
| LeviLauncher Android | https://github.com/LiteLDev/LeviLauncher-Android | `d544df8cafab731ba7686dfb88951602f13293ef` | Apache-2.0 |
| ModMenuBE | https://github.com/AFAlimru/ModMenuBE | `dcca382ec0a15e3849e9a65dcdf8fe64480d3a71` (0.2.1) | MIT, AFAlimru |

These are separate upstream projects. Their source and licenses remain identifiable; OptLevi's MIT license applies only to OptLevi-authored files.

## Clone

Clone with submodules so both source trees are populated:

```bash
git clone --recurse-submodules https://github.com/Spike26-ht1/Optlevi.git
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## ModMenuBE Android build

The current focus is the Android ARM64 ModMenuBE package. See [docs/modmenu-android.md](docs/modmenu-android.md).

The GitHub Actions workflow builds a `.levipack` artifact for `arm64-v8a`. A successful CI build proves compilation and packaging only; the package still needs to be installed and tested in LeviLauncher on a physical Android device.

## License boundaries

- OptLevi-authored material: MIT (see `LICENSE`).
- LeviLauncher Android: Apache License 2.0 (see `LeviLauncher-Android/LICENSE`).
- ModMenuBE: MIT, copyright AFAlimru (see `ModMenuBE/LICENSE`).

Do not replace upstream license files or attribution when modifying, distributing, or integrating these projects.
