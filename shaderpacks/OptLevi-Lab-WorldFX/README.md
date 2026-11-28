# OptLevi Lab WorldFX 0.2

Original fullscreen Java GLSL pack for real-time Minecraft Bedrock framebuffer
experimentation on Android via OptLevi. Includes three fully linked stages:
highlight extraction, 7-tap blur and tone-mapped final composition.

- PERFORMANCE compiles only the final pass.
- BALANCED uses three active passes at full screen resolution.
- QUALITY increases saturation, vignette, exposure and bloom.
- All modes operate on actual rendered framebuffer pixels, not on synthetic
  G-buffer resources. The current swap-stage hook also contains Bedrock HUD.
- This is not a replacement for Java shadow, geometry, water or BSL rendering.
- No material.bin or Custom Buffers are modified.

Use the "Play with Lab World FX" menu control in the embedded OptLevi package,
or import the zip manually and select a profile.
