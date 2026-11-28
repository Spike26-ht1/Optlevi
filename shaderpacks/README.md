# OptLevi Lab 0.1

Original Java GLSL shader packs for OptLevi development, not BSL ports.

- **Lite**: one final pass, no world G-buffer/depth/shadow requirements. Neutral is a passthrough, Lite and Vivid apply progressively stronger saturation/exposure. On the current OptLevi swap hook this operates on the actual presented frame (which may include game UI). It is not proof of a geometry shader integration.
- **World**: separate terrain, shadow, deferred, water and final programs. Default/LOW has a 512² shadow map and small, inexpensive effects; MINIMUM disables shadows and water tint; HIGH increases the map to 1024². The experimental world pack requires real mapped meshes, atlas, lightmap coordinates, camera and phase callbacks. It cannot render stock Bedrock without the native provider. Geometry input positions for the shadow lookup must be camera-relative and use matching transforms. This pack is a small reference, not a general-purpose finished Iris shader.

Use the ZIP with `shaders/` at its root. No material.bin changes. The original pack source is in this directory.
