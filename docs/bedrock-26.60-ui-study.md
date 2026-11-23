# OptLevi 0.5.0 — Bedrock 26.60 UI texture study

## Intent preserved

OptLevi's Mods menu must retain the **Fabric Mod Menu information layout** requested by the project owner: left-hand searchable mod list, selected-mod details on the right, working link/config actions and minimal bottom buttons. The UI **appearance and interaction feedback** should resemble the Minecraft Bedrock UI where practical, using the owner's 26.60 UI texture archive as a source of styling measurements. This is not a return to the former AFAlimru framed interface.

## Source inspected

The owner uploaded `levi_packs.zip`, containing **2,264 entries** including `ui/` and `gui/` images. Verified relevant files and source texture properties:

| Bedrock 26.60 texture | Size | Observed center pixel | Design inference |
| --- | --- | --- | --- |
| `ui/NormalButtonStroke.png` | 12×12 PNG | RGBA(198,198,198,255) | Vanilla-style grey button face `#C6C6C6` |
| `ui/NormalButtonThinStroke.png` | 7×7 PNG | RGBA(198,198,198,255) | Thin pixel bevel |
| `ui/background_panel.png` | 16×16 PNG | RGBA(198,198,198,255) | Pixel-outlined panel construction |
| `ui/ScrollHandle.png` | 3×5 PNG | RGBA(198,198,198,255) | Contrasting light scrollbar |
| `ui/edit_box_indent_hover.png` | 6×6 PNG | RGBA(94,94,94,255) | Dark inset/input border family |
| `ui/classic-button-pressed.png` | 5×5 PNG | RGBA(114,101,103,255) | Pressed-state darkening |

These observations are taken directly from the supplied texture ZIP, not guesses about the game. Minecraft UI also relies on **nine-slice stretching and focus states**, so sampling only the center color is not a complete reproduction of vanilla texture behavior.

## Applied to OptLevi 0.5.0

- Pixel-style button faces modeled from the observed Bedrock grey/white/black palette, including pressed, hover and disabled states.
- A thin Bedrock-grey title strip inside the independently rendered Fabric list/details screen.
- More opaque charcoal panels. The 0.4.0 screenshot showed Minecraft menu controls bleeding through the mod list, making text hard to read; panels now block those controls.
- Bedrock-like grey scroll handles and black tracks, a black inset search control, and matching 1-pixel outlines.
- Preserved actual **Fabric screen hierarchy**, touch-controlled list, mod metadata, links, the optional collapsed mod settings, and simple Refresh/Done footer.
- Kept previously tested main-menu and pause-menu Mods buttons and the Shaders placeholder unchanged.
- Original OptLevi project icon remains full-quality 200×200.

### Asset handling

The **full 39 MB texture archive is not committed**. Its proprietary Minecraft art is used as a design reference; the mod reproduces neutral colors and UI primitives in its own C++ code rather than redistributing Mojang's texture files. The archive remains a local reference only.

## Next device validation

On Minecraft Bedrock v26.62 (with the supplied textures from v26.60 as styling reference), compare a real 1536×709 screenshot of the new 0.5.0 screen. Verify list items no longer overlap the game background, each row remains readable, selection works, UI controls react to touch, and the settings popup/link/config actions work. Confirm no differences in the main and pause-screen Mods button positions.

A GitHub Actions success verifies C++ compilation and `.levipack` structure only; it does not establish screenshot similarity or complete device runtime correctness.
