# OptLevi

OptLevi is an Android-focused Bedrock enhancement project inspired by the OptiFine/Forge ecosystem.

## Repository layout

- `LeviLauncher-Android/` — upstream LeviLauncher Android source, tracked as a Git submodule.
- `LICENSE` — MIT license for original OptLevi-authored material in this repository.

## LeviLauncher upstream

The launcher source is maintained by **LeviMC / LiteLDev** and is included without modifying its upstream files.

- Upstream: https://github.com/LiteLDev/LeviLauncher-Android
- Pinned revision: `d544df8cafab731ba7686dfb88951602f13293ef`
- Upstream license: Apache License 2.0 (see `LeviLauncher-Android/LICENSE` after initializing the submodule).

Initialize or clone the submodule with:

```bash
git submodule update --init --recursive
```

When cloning OptLevi for the first time:

```bash
git clone --recurse-submodules https://github.com/Spike26-ht1/Optlevi.git
```

## Important

OptLevi's MIT license applies to OptLevi-authored material only. It does not replace the licenses of included third-party projects. Preserve the upstream Apache-2.0 license and notices when using or redistributing LeviLauncher code.

The launcher is currently included as a pinned upstream dependency; this repository setup does not claim that OptLevi integration or an Android build has been completed.
