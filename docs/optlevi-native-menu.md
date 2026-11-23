# OptLevi 0.1.0 — native menu prototype

This is an **OptLevi-branded native ARM64 LeviPack**, built from the pinned ModMenuBE 0.2.1 dependency after applying OptLevi's compatibility patch. The original ModMenuBE code remains in its upstream Git submodule, with AFAlimru's original MIT license.

## Source / packaging
- **Base launcher**: LeviLauncher-Android at the pinned repository revision.
- **Menu implementation**: upstream ModMenuBE 0.2.1.
- **Mod identity**: `OptLevi`, `libOptLevi.so`, `OptLevi-0.1.0-arm64-v8a.levipack`.
- **Icon**: user-provided OptLevi icon, resized to 40×40 for the menu button and package icon.
- **Native button**: `optlevi.open`, registered via the LeviLauncher external-button API; a click toggles the menu.
- **Default position**: near the bottom-right of the display, using 83% of landscape width and a small bottom margin. This approximates the location beneath **Vestiário / Dressing Room** in the provided screenshot. Display scaling can change the exact location. Existing button positions remain editable through the launcher HUD editor.
- **Visibility**: LeviLauncher's `setOverlayShowEverywhere` preference enables the overlay beyond the game HUD, including menus. It is **not** a direct insertion into Minecraft's JSON-UI start screen, so the visibility and layering on the start screen require device validation.

## Building
GitHub Actions runs `.github/workflows/optlevi-android.yml`. It checks out upstream submodules, applies the compatibility patch, runs `python3 scripts/prepare-optlevi.py`, builds the ARM64 native library with the Android NDK, and verifies the resulting LeviPack structure.

For local builds, first initialize the repository's submodules, apply `patches/modmenube/0001-align-current-levilauncher.patch`, and run `python3 scripts/prepare-optlevi.py` before the ARM64 CMake configuration. The script intentionally aborts if upstream source no longer matches the expected version.

## Device validation
1. Install `OptLevi-0.1.0-arm64-v8a.levipack` into a compatible LeviLauncher.
2. Prefer disabling the original ModMenuBE to avoid two independent floating menu buttons during this test.
3. Start Minecraft 1.26.x and check the OptLevi button below the **Vestiário** button in the main menu.
4. Tap the OptLevi button; confirm the native menu opens, closes, and receives touch without moving the game.
5. Enter a world and confirm whether the overlay is visible as intended.
6. If the position differs, use LeviLauncher's HUD editor to adjust its location.

A successful CI build proves only compilation and packaging; it does not prove correct runtime or start-screen layering. Custom shader buffers and `.material.bin` handling are **not implemented** in this prototype.

## Original asset handling
The source archive uploaded by the user is a reference; the full archive should not be committed to GitHub. Only the icon needed for the UI is embedded into the build-preparation script. ModMenuBE's source and original license remain with the submodule.
