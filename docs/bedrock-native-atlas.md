# OptLevi 0.6 — real Bedrock 26.60 texture atlas

## Purpose

Earlier OptLevi prototypes *only imitated* Minecraft buttons with ImGui-colored rectangles. The owner specifically requested the **actual UI textures from their uploaded `levi_packs.zip`**. This update embeds a small, lossless **128×128 RGBA PNG atlas** assembled from 16 of those source PNGs. At runtime, the native UI decodes the image via `stb_image`, uploads it as a **GLES3/GL_NEAREST texture**, then renders sprite rectangles through ImGui's `AddImage`.

The **Fabric Mod Menu information hierarchy** (search and filters, compact mod list, selected-mod details, two action buttons) is preserved. This is not a return to AFAlimru's old visual frontend.

## Verified source texture mapping

| Sprite role | Path in owner's 26.60 archive | Pixel dimensions |
| --- | --- | --- |
| Normal button | `gui/newgui/buttons/border/base.png` | 7×7 |
| Hover button | `gui/newgui/buttons/border/hover.png` | 7×7 |
| Pressed button | `gui/newgui/buttons/border/basePress.png` | 7×7 |
| Hovered/pressed button | `gui/newgui/buttons/border/hoverPress.png` | 7×7 |
| Disabled button | `ui/disabledButton.png` | 3×3 |
| Inset / dark panel | `ui/edit_box_indent.png` | 6×6 |
| Light title panel | `ui/background_panel.png` | 16×16 |
| Panel outline | `ui/panel_outline.png` | 4×4 |
| Focused input | `ui/edit_box_indent_hover.png` | 6×6 |
| Scrollbar handle and rail | `ui/ScrollHandle.png`, `ui/ScrollRail.png` | 3×5, 1×1 |
| Checkbox off/on | `gui/newgui/buttons/checkbox/checkbox_unchecked.png`, `checkbox_checked.png` | 16×13 each |
| Checkbox hover variants | `gui/newgui/buttons/checkbox/checkbox_unchecked_hover.png`, `checkbox_checked_hover.png` | 16×13 each |

The atlas deliberately duplicates the inset sprite as both a panel and input source for distinct render roles. All original pixels are copied exactly into isolated atlas cells; there is **no resampling**. The exact atlas SHA-256 is `33133186c6f6be28acf54ce427d9e63f34a7b268adf3852bcde59fcb295304ab` and its PNG length is **1062 bytes**.

## Runtime implementation

- `src/ui/OptLeviBedrockAtlas.inc` contains the actual PNG bytes (base64) and sprite UV coordinates. It is compiled into `libOptLevi.so`.
- `src/ui/OptLeviBedrockTexture.inc` decodes the PNG once per EGL context using `stbi_load_from_memory`; it uploads the RGBA pixels through GLES3 with `GL_NEAREST` sampling.
- `ofNineSprite` preserves source-image corners when stretching buttons and panels, avoiding the stretched-pixel distortion of a simple scaled PNG.
- `src/ui/OptLeviFabricScreen.inc` calls these texture functions from the real button state handler, mod list rows, title and details panels, search field, checkboxes, and scrollbars.
- If a GL context is not available, the button renderer uses a basic fallback rather than leaving the control invisible. A new context gets its own texture; the renderer never tries to delete stale texture names from an unrelated context.
- `scripts/package-optlevi.py` includes the same PNG in the combined LeviPack at `OptLevi/assets/ui/bedrock26_atlas.png` for inspection. The runtime uses its compiled-in copy and doesn't depend on device filesystem paths.
- `scripts/generate-bedrock-atlas.py` regenerates or checks the exact atlas from the uploaded ZIP. The full source ZIP is **not stored in the repo**.

## Regeneration

```sh
python -m pip install Pillow
python scripts/generate-bedrock-atlas.py /path/to/levi_packs.zip --check
# To deliberately regenerate after a version update:
python scripts/generate-bedrock-atlas.py /path/to/levi_packs.zip --write --png /tmp/OptLeviAtlas.png
```

## Tests and limits

The CI test `scripts/test-bedrock-atlas.py` validates the PNG's exact hash and dimensions, 16 source sprite coordinates, the image decoder/GPU upload path, and real image use in the native UI. Android ARM64 builds also validate embedded PNG inclusion in the final LeviPack.

**A successful build does not prove in-game texture display.** The main remaining device test is to confirm that the buttons visibly show Minecraft pixel borders and pressed/hover states, that list rows and inputs use the new texture, and that tap, scroll, and mod-opening behavior still work on the user's Minecraft 26.62 device.

These assets originate from the user's game UI archive. The repository does not distribute that full archive or any Minecraft font files. Use and redistribution of selected Mojang UI assets remain subject to the game's applicable terms and permissions.
