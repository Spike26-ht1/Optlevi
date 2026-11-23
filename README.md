# OptLevi

OptLevi brings a **Fabric Mod Menu-inspired native mod browser** to Minecraft Bedrock on Android, with Minecraft-style **Mods** buttons rendered by a minimal Levi API resource pack.

## OptLevi 0.5.0 — Fabric layout, Bedrock 26.60 visual styling

The project owner has confirmed the **main-menu Mods button opens OptLevi in Minecraft Bedrock 26.62**. The pause menu now has an additional compact Mods button beside the book and a Shaders placeholder in the former Marketplace slot. A native Android Preloader touch bridge handles menu opening without registering a floating launcher button.

Version 0.5.0 builds on the independent renderer from 0.4.0, which replaced the old ModMenuBE **View::List** drawing path with an independent Fabric-style renderer, informed by the owner's supplied **Fabric Mod Menu 21.0.1** JAR:

- Balanced left mod list and right detail panes
- Search, icon-only filter, library visibility, sorting and visible mod count
- Selectable entries with icon, name, version, badges and description
- Details with version/authors, Website/Issues, description, license, credits and technical info
- Native mod Configure, Enable/Disable, Refresh and Update actions
- Fabric-like custom pixel-edge buttons, two-action bottom row (Refresh and Done), and physical-screen-height scaling independent of oversized Android UI settings
- Technical metadata and Bedrock-only configuration actions in a collapsible section rather than dominating the selected-mod details

The original project icon is preserved at 200×200. Shaders remains a placeholder; shader management is not implemented.

**Device validation:** Android ARM64 CI can compile and validate the LeviPack, but cannot prove that the latest UI matches Java pixel-by-pixel on an actual Minecraft screen. Fabric's Java-edition font and blur are not bundled. Android uses a coordinate-calibrated native touch fallback, not a complete Bedrock screen-controller event hook.

## Development layout

- `LeviLauncher-Android/` — pinned upstream LeviLauncher base
- `ModMenuBE/` — pinned upstream mod discovery, config and native renderer
- `resources/levi_api/` — embedded Minecraft JSON UI resources for main and pause menus
- `assets/optlevi-original-200.png` — original project icon (no resizing)
- `scripts/prepare-optlevi.py` — OptLevi native branding
- `scripts/prepare-levi-api.py` — remove obsolete floating launcher button
- `scripts/prepare-fabric-layout.py` — initial native theme/style modifications
- `scripts/prepare-fabric21-fidelity.py` — earlier Fabric layout work used as a compatibility baseline
- `src/ui/OptLeviFabricScreen.inc` — **new independent full-screen Mods frontend**, used instead of the old native list renderer
- `scripts/prepare-fabric-real-screen.py` — inject independent frontend into the native ModMenu build
- `scripts/prepare-optlevi-touch.py` — Android native menu touch routing
- `scripts/build-resource-pack.py` — standalone `OptLevi-LeviAPI-0.5.0.mcpack`
- `scripts/package-optlevi.py` — combined `OptLevi-0.5.0-arm64-v8a.levipack`
- `docs/bedrock-26.60-ui-study.md` — sampled UI texture reference and design decisions
- `docs/ui-research-log.md`, `docs/fabric-modmenu-21-reference.md`, and `docs/fabric-fullscreen-design.md` — UI study notes
- `.github/workflows/optlevi-android.yml` — ARM64 build and resource validation

## Getting the code

```sh
git clone --recurse-submodules https://github.com/Spike26-ht1/Optlevi.git
```

New work is developed on feature branches and reviewed through pull requests; not merged into `main` without explicit authorization.

## Attribution

| Component | Upstream | Revision | License |
| --- | --- | --- | --- |
| LeviLauncher Android | https://github.com/LiteLDev/LeviLauncher-Android | `d544df8cafab731ba7686dfb88951602f13293ef` | Apache-2.0 |
| ModMenuBE | https://github.com/AFAlimru/ModMenuBE | `dcca382ec0a15e3849e9a65dcdf8fe64480d3a71` | MIT |
| Fabric Mod Menu 21.0.1 (design reference only) | https://github.com/TerraformersMC/ModMenu | JAR supplied by project owner | MIT |

OptLevi does not embed Fabric Java classes or third-party Minecraft fonts. Upstream submodule licenses and notices remain with their respective projects.
