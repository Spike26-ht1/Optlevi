# OptLevi — Levi API resource-pack UI (Android WIP)

## Goal
Use a vanilla Bedrock JSON-UI button rather than the floating `pl::modmenu::ButtonBuilder` overlay. The button should sit below Dressing Room (Vestiário), like the screenshot supplied by the project owner. It should open a Fabric Mod Menu-like native mod list.

## Reference inspected
The user supplied Playback 0.4.1 for Minecraft 26.10. The relevant patterns are:

- `resources/ui/start_screen.json`: define a button inheriting a Bedrock `start.main_button`, give it `$pressed_button_name` and insert it using `modifications`.
- `src/playback/screen/MainMenuHooks.cpp`: register `button.playback_open_replays` through `MainMenuScreenController` / `StartMenuScreenController` with `registerButtonPressedHandler`.
- `resources/manifest.json`: a valid Bedrock resource pack manifest.

Playback targets the **LeviLamina** native hooking API. This source is a reference, not a drop-in dependency of Android LeviLauncher. We have not imported Playback's GPL-licensed C++ code or its own assets.

## OptLevi implementation
- `resources/levi_api/ui/start_screen.json` overrides only the needed start-screen controls and inserts a Bedrock-styled Mods button after `skin_panel`.
- `button.optlevi_open_menu` is the identifier that Android native code must recognize.
- `resources/levi_api/manifest.json` is a valid format-2 Bedrock resource-pack manifest, with unique UUIDs.
- `scripts/build-resource-pack.py` packages the UI with the project icon as a standalone `.mcpack`.
- `scripts/prepare-levi-api.py` removes the previous floating OptLevi button registration and modifies the native menu's palette and column proportions toward a dark Fabric-like two-column mod browser.
- `ModMenuBE` remains a pinned upstream submodule with original attribution.

## Critical outstanding work
The **JSON-UI click is not wired to the Android native menu yet**. The original Playback code calls LeviLamina's `registerButtonPressedHandler` against Minecraft screen-controller hooks. Android LeviLauncher uses **Preloader SDK** and does not expose LeviLamina's game headers directly. The matching Bedrock Android 1.26.x UI hook / symbol must be independently located, implemented, and tested.

In particular:
1. Implement a version-checked native screen controller hook for `button.optlevi_open_menu`, forwarding to `modmenu::ui::requestOpen` or `requestToggle` on a safe thread.
2. Ensure the packaged resource pack is installed and enabled at **Global Resources**, or automate activation through the supported launcher mechanisms.
3. Verify the JSON-UI control renders below Dressing Room at the actual device UI scale.
4. Verify touch/gamepad navigation, native click callback, close/reopen, and version compatibility. No unverified hooking offsets should be shipped.
5. Continue evolving the menu layout beyond palette and spacing, while preserving installed-mod listing and settings functionality.

A green CI proves packaging and native compilation, **not** the start-screen click. Do not call this feature fully functional until the Android hook and on-device test are complete.

## Testing visual layout separately
```bash
python3 scripts/prepare-optlevi.py
python3 scripts/prepare-levi-api.py
python3 scripts/build-resource-pack.py
```

Import `build/OptLevi-LeviAPI-0.2.0.mcpack` as a Bedrock resource pack and enable it globally, then inspect the start screen. The button can appear without the native event callback, but clicking it will not open the native menu until the Android bridge is complete.


## 2026-11-23 device test: packaging fixed, button still inert

The project owner tested Minecraft v26.62 and provided screenshots showing:
- OptLevi API **active** in Global Resources.
- The native OptLevi mod **enabled** in LeviLauncher.
- The **Mods** button rendered on the Minecraft start screen, but pressing it did **not** open OptLevi.

They also provided `OptLevi-0.1.0_new style_arm64.levipack.zip`; inspection confirms this *working-for-injection* package uses exactly:

```
OptLevi/
  manifest.json
  icon.png                         (original 200×200 PNG)
  libOptLevi.so
  LICENSE
  resources/
    minecraft.ttf
    minecraft_resource_packs/
      LeviAPI/
        manifest.json
        pack_icon.png              (original 200×200 PNG)
        ui/_ui_defs.json
        ui/start_screen.json
        texts/en_US.lang
        texts/pt_BR.lang
        texts/languages.json
```

The corrected native manifest contains:

```json
{
  "type": "preload-native",
  "name": "OptLevi",
  "author": "BRSpike26",
  "version": "0.1.0",
  "entry": "libOptLevi.so",
  "icon": "icon.png",
  "overwrite_files": ["icon.png", "resources/minecraft.ttf"],
  "overwrite_folders": ["resources/minecraft_resource_packs"]
}
```

`scripts/package-optlevi.py` reproduces the embedded folder layout and automatically checks icon dimensions and manifest structure. Local use:

```sh
python3 scripts/package-optlevi.py \
  --native build/optlevi-arm64-v8a/OptLevi-0.1.0-arm64-v8a.levipack \
  --resource build/OptLevi-LeviAPI-0.2.0.mcpack \
  --reference /path/to/owner-corrected.levipack.zip
```

The user's original icon is **200×200**, SHA-256 `e6ef56829966646b0718d3ac785b7fbd19b5894449860dbead49c6ef3948e270`. A previous OptLevi script resized it to **40×40**. Do not scale it down again, and do not publish a purported corrected package using only a 40×40 substitute.

**Native event bridge remains blocked**: The JSON-UI `button.optlevi_open_menu` is a Bedrock `MinecraftScreenController` action. The Android Preloader's `pl::modmenu` module registration is a separate subsystem and does *not* automatically dispatch JSON-UI actions. Playback implements `MainMenuScreenController::$onOpen` and `StartMenuScreenController::_registerEventHandlers` hooks on LeviLamina (Windows). A corresponding, version-validated Android native hook has **not** been implemented or tested. Do not claim the Mods button is functional or ship a speculative pattern/offset.

The screenshot also places the button to the **left of Realms**, rather than below Dressing Room, so the JSON UI placement must be corrected against the observed screen rather than assumed from anchor constants.
