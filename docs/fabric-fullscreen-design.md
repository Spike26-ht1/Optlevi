# OptLevi 0.4.0 — Fabric-style screen replacement

## Why this is a separate renderer

On the user's Minecraft Bedrock v26.62 screenshot, OptLevi 0.3.0 still appeared like AFAlimru ModMenuBE's original menu despite multiple palette and panel changes. Most importantly, the device showed an oversized panel framing, an unnecessarily tall mod row and UI controls, a large top search area and a heavy bottom toolbar. Fabric Mod Menu's list is structurally different.

**Version 0.4.0 removes AFAlimru's original View::List from the render path**. The native ImGui implementation lives in `src/ui/OptLeviFabricScreen.inc` and is copied into `ModMenuBE/src/ui` by `scripts/prepare-fabric-real-screen.py`. Only native mod-registry/configuration/icon-cache functionality is reused. `View::Config` and `View::Json` subviews still use the existing backend's editors for now.

## Layout measured against the reference

Fabric Mod Menu reference, provided by the user: **1096 × 609** image. Android target screenshot: **1536 × 709** image. The layout uses scale `clamp(screen_height / 609, 0.72, 1.38)` for pixel dimensions so it does not inherit the oversized launcher ImGui UI scale.

| Element | Reference pattern | OptLevi native target |
| --- | --- | --- |
| Main background | Minecraft world behind subtly tinted panels, no AFAlimru window frame | Full-screen translucent backdrop; two independent dark panes |
| Title | Small, centered over left half | Centered Mods label using installed Android font |
| Search | One line at top left; tiny funnel on right | Black field and custom drawn funnel icon |
| Filter UI | Appears when filter icon is clicked | Popup with libraries, sorting and update action |
| Mod count | Thin line immediately above list | Count above independently scrolling list |
| Mod rows | Many compact icon/name/description rows; selected outline | 59 reference units per row, approx. 7 visible on owner's phone |
| Details | Compact selected-mod header and contextual project links | Icon, name, version, authors, Website and Issues buttons |
| Lower details | Description, links, license, credits | Scrollable text content, native configuration actions collapsed below |
| Bottom actions | Two Minecraft-styled grey buttons | Refresh and Done, pixel-border button renderer |
| Backend | Fabric Loader API (Java) | Bedrock/LeviLauncher Preloader and ModMenuBE registry (native) |

Fabric Java bytecode and the original copyrighted Minecraft font are **not** included in OptLevi. Buttons and list cells are drawn with ImGui primitives; the original Fabric sprites are not copied.

## Native controls maintained

Mod discovery and loading state, mod icon selection, description lookup, links, configuration, enable/disable, update-checking, restart warnings and readme rendering remain connected to the native backend. The existing Bedrock JSON-UI main-menu and pause-menu Mods buttons (plus the Shaders placeholder) are not changed by this update. The original OptLevi 200×200 project icon remains in the combined LeviPack.

## Verification and release boundaries

GitHub Actions verifies the alternate renderer is injected into `drawFrame()`, the existing original list screen is bypassed when the menu is open, the six-or-more-row geometry check for the owner's phone, the C++ ARM64 build, and the combined resource-injected `.levipack`.

A successful build does **not** prove visual fidelity or touch interaction in Minecraft. The owner must test on a real device, checking all selected-mod actions, list scrolling, filter popup, and Android keyboard behavior, then provide a screenshot for precise adjustments.

No merge into `main` is performed without explicit authorization.
