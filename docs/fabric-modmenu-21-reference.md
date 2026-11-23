# OptLevi — Fabric Mod Menu 21.0.1 visual fidelity study

This document records the structure inspected in the owner's uploaded `modmenu-21.0.1.zip` (a Fabric 21.0.1 Java Edition JAR). It is the comparison reference for the **native ImGui** implementation in Minecraft Bedrock/LeviLauncher.

## What the reference actually contains

The supplied JAR is a compiled Fabric mod, not an OptLevi/Android plugin. Its `fabric.mod.json` declares Mod Menu **21.0.1**, MIT license, Fabric loader dependencies and Minecraft **26.3+**. We examined its class signatures and bytecode using `javap` locally, without copying the Java implementation into the Bedrock project.

Verified GUI classes:
- `com.terraformersmc.modmenu.gui.ModsScreen`
- `com.terraformersmc.modmenu.gui.widget.ModListWidget`
- `com.terraformersmc.modmenu.gui.widget.entries.ModListEntry`
- `com.terraformersmc.modmenu.gui.widget.DescriptionListWidget`
- `com.terraformersmc.modmenu.gui.widget.entries.ParentEntry` / `ChildEntry`

The reference creates **two principal panes**, with list pane width approximately half the total window minus a small gap. Its standard mod list uses **36 Minecraft GUI units per row** (23 in compact mode); each entry draws a mod icon, translated name, badges, and, in standard mode, a wrapped summary. The left pane has an editable search field near the top, a filter toggle and sorting/library options. The right pane reflects the selected mod, with an icon, name, version, authors, website/issues links, description, license/credits and configuration action. The central `Done` action closes the menu. Minecraft Java Edition supplies its own fonts, background and widget rendering — not portable to ImGui or Bedrock.

The uploaded JAR also includes English/Brazilian Portuguese localization and the original mod's GUI icon sprites. We did not commit or redistribute those JAR sprites or fonts.

## Native implementation mapping

| Fabric Mod Menu element | OptLevi native adaptation | Implementation status |
| --- | --- | --- |
| Centered Mods title | Small title centered over left pane | Implemented |
| Approximately equal list/detail columns | 50% / 50% split with separate details scroll | Implemented |
| Search & filter control | Text search, Filters toggle, library visibility and sorting | Implemented |
| Visible mod count | Count line under filters, above list | Implemented |
| Icon/name/badges/description rows | Flat selectable 76-unit entries, summary wrap | Implemented |
| Details header | Large icon, selected name, version, authors | Implemented |
| Website and Issues | Link buttons where the installed mod supplies actual URLs; disabled otherwise | Implemented |
| Description, License and Credits | Ordered text sections, with technical metadata collapsed below | Implemented |
| Configure mod | Native OptLevi config API / JSON editor | Preserved |
| Enable/disable mod | Native Bedrock/LeviLauncher enable toggle, restart notice | Preserved (Bedrock-specific) |
| Close / Done | Centered grey Done button; touch-friendly close control | Implemented |
| Vanilla Java pixel font / blur | Android system font and tinted ImGui backgrounds | Not identical; no proprietary fonts redistributed |
| Keyboard and file drag/drop | Touch navigation and installed-mod scan | Android-specific |

## Changes from the screenshot dated 2026-11-23

The earlier native screen was **not** sufficiently similar: a large translucent overlay let paused-game text show through the mod information, badge-heavy compact cards hid mod descriptions, the bottom toolbar featured update controls more prominently than the Fabric `Done` action, and the default Android font was large.

`scripts/prepare-fabric21-fidelity.py` now rewrites the **actual** native `ModMenuScreen.cpp` list-card, details, header, list layout and footer functions. It also reduces UI typography size, strengthens panel contrast, and keeps all native mod controls in place. This script is applied *after* `scripts/prepare-fabric-layout.py` and is validated in the Android workflow.

## Build and validation

The GitHub Actions workflow must compile the ARM64 native library and produce a combined `.levipack` containing the project's original **200×200** icon and the Levi API resource pack, with working start-menu and pause-menu Mods controls preserved. Tests check the presence of list-search, detail link actions, config enable/disable, update checks, and Done.

CI can verify source geometry and native ELF packaging but **cannot measure visual similarity in-game**. Capture a 1536×709 screenshot and compare:
- Search field and filter immediately below the centered title
- At least three readable mod rows with icons and summaries
- Selected mod name/icon/version/authors, website/issues, description
- Technical metadata hidden until expanded
- Done centered on the bottom edge
- No obscuring by paused game buttons behind translucent content
- Start-menu and pause Mods buttons still open/close reliably

## Attribution and licensing

Fabric Mod Menu is MIT-licensed by the TerraformersMC project and contributors; ModMenuBE is an MIT-licensed dependency by AFAlimru. OptLevi independently recreates UI **behavior and organization**, not a bundled copy of Java bytecode. Keep upstream licenses and required notices in the project when redistributing.
