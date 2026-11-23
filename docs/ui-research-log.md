# OptLevi — Minecraft interface research log

This log records **observed Bedrock UI definitions, mod integration tests and development decisions**. It is intended as a living, versioned reference for development across the game, not a claim that every screen is already implemented.

## Source and test baseline — 2026-11-23

- **Observed device:** Minecraft Bedrock v26.62 on Android, via LeviLauncher, 1536 × 709 screenshot. The owner confirmed that the **main-menu Mods button opens OptLevi**.
- **Bedrock UI reference:** `OptLevi_folder.zip/resource_pack/levi_api/ui/pause_screen.json` and `start_screen.json` supplied by the project owner. These are UI source references, not entire files to redistribute.
- **Current resource pack:** `resources/levi_api/` overrides only the controls OptLevi needs.
- **Native integration:** Preloader-Android `pl::input::registerTouchCallback` receives raw events through `MinecraftActivity.dispatchTouchEvent`. This is position-based, not a genuine Minecraft UI action callback.
- **UI goal:** Fabric Mod Menu-like split layout with a searchable mod list on the left, module details on the right, visible selected-row accent, compact spacing and a Minecraft-native button style.
- **Screenshot baseline:** main-menu Mods button appears near the center-left and opens OptLevi. Pause menu shows an open-book control in the bottom-left icon row and a full-width “Procure Complementos!” marketplace action beneath Options.

## Screen interface inventory

| Screen | Source definition or reference | What we observed | OptLevi decision | Validation |
| --- | --- | --- | --- | --- |
| Start / main menu | `start_screen.json` / `start_screen_content` | Mods button renders and native touch bridge opens menu | Preserve the working position, style and bridge | Owner confirmed working |
| Pause / in-game menu | `pause_screen.json` / `pause_screen_content` | Lower-left icon row includes the open-book action | Add Mods after `small_achievements_button` inside `smaller_buttons_panel/stacked_column` | Needs device test |
| Pause marketplace slot | `pause.store_button_panel` / `pause.store_button` | Large action labeled “Procure Complementos!” | Replace the whole store button panel with a native-styled **Shaders** placeholder in the *same position* | Needs device test |
| Pause compact marketplace slot | `pause.store_button_small_with_tooltip` | Secondary marketplace icon layout exists for certain screen modes | Replace secondary marketplace icon with Shaders too, not just main button | Needs device test |
| Global resource packs | `OptLevi/resources/minecraft_resource_packs/LeviAPI` | In-game global resource pack successfully activates | Keep combined native `.levipack` manifest `overwrite_folders` and embedded UI pack | Passed GitHub CI and device injection |
| Mod list and details | Upstream `ModMenuBE/src/ui/ModMenuScreen.cpp` | OptLevi uses native ImGui menu drawn over Bedrock | Fabric-inspired compact rows, search, details panel, action buttons and top title | Runtime rendering on new build pending |
| World HUD | Preloader `isHudScreenOpen` | Must not intercept gameplay touch | Keep pause and start hit zones gated by native menu-state checks | Needs device test |
| Other screens | Bedrock UI source folder under user-supplied Levi API | No targeted implementation yet | Inventory relevant layout IDs and interface events before modifying them | Research only |

## Pause screen structure from the supplied reference

`pause.menu_background@pause.transparent_background` contains `button_panel`, whose controls include `store_button_panel@pause.store_button_panel` (main store action) and `smaller_buttons@pause.smaller_buttons_panel`. The latter has `stacked_column` with `small_achievements_button` (book), `small_how_to_play_button` (help), and other icon actions. The game's `store_button_panel` may be hidden when `#store_button_secondary_visible` is active. Thus both the primary store panel and compact store control must be replaced to avoid the old Marketplace action appearing in another layout.

## Technical event contract

- Main-menu Mods uses `button.optlevi_open_menu`.
- Pause Mods uses `button.optlevi_open_pause_menu`.
- Pause Shaders uses `button.optlevi_shaders_placeholder`, which is **not functional yet**.
- The Android touch callback is a **calibrated coordinate fallback**; its button-region logic must differentiate menu state and avoid accidental activation in gameplay.
- Actual Minecraft UI actions should eventually be received via a version-validated native `MinecraftScreenController` handler instead of screen-coordinate matching.

## Study notes to add during future gameplay checks

Record the precise Minecraft version, screen, UI scale/aspect ratio, control ID, displayed behavior, tap region, relevant log messages, changes introduced and result. Never include tokens, private player data, chat messages or credentials in diagnostic logs.

## Open questions

1. Does `pause.smaller_buttons_panel/stacked_column` injection appear directly after the book icon on all Android UI scales?
2. Does the full-size Shaders replacement appear in the former “Procure Complementos!” slot, without hidden marketplace shortcuts?
3. Does the native Preloader `isPauseMenuOpen()` state become true early enough for touch input on this screen?
4. Can a verified Bedrock screen-controller hook replace approximate hit areas in Minecraft 26.62?
5. What nonblocking press animation or transitional effect can be implemented without stealing Minecraft's touch state?
