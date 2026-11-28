# Native Java shader integration contract

Target: Bedrock Android ARM64, RenderDragon GLES, LeviLauncher. The renderer adapter must supply actual per-frame resources, not inferred phases from draw counts.

| Requirement | Runtime entry point | Remaining device integration |
|---|---|---|
| Actual camera/world state in Java coordinate conventions | publishWorldUniforms / captureProgramUniforms | Verify native camera origin, matrices, previous frame, time, weather and biome values |
| Block identity and geometry | translateTerrainQuad / drawTerrainQuads / drawGeometry | Obtain verified chunk mesh, material IDs, UV/lightmap, normals and tangents; map block.properties |
| World batches | renderJavaWorldScene | Supply opaque, hand and translucent geometry, including entities, sky, particles and water as needed |
| Light-space geometry | getShadowCamera / renderShadowScene | Provide real shadow caster batches, including off-camera casters |
| Depth and MRT | captureSceneDepth / captureSceneColor or owned world targets | Verify attachments, formats, dimensions and semantic producer |
| Deferred | renderDeferred | Invoke at verified opaque-to-translucent boundary if using native phase adapter |
| Final | presentJavaWorldScene | Call after mapped world is complete, before native HUD; verify destination and viewport |
| Lifetime | beforeSwap | Expire current-frame inputs; never present the mapped scene twice |

Do not treat GLES symbol resolution, a program name or an FBO bind as proof of a Minecraft phase. Use version-specific, source-backed adapter evidence. A missing mandatory input must preserve native rendering rather than fabricate a G-buffer. GPU work submitted by OptLevi itself is excluded from native observation via OwnedGpuScope.

References:
- https://shaders.properties/current/reference/programs/overview/
- https://shaders.properties/current/reference/programs/deferred/
- https://shaders.properties/current/reference/buffers/depthtex/
- https://shaders.properties/current/reference/shadersproperties/rendering/

Acceptance: actual BSL archive imports and links; all mandatory inputs produced for the current world frame; mapped terrain, entities, shadows and water visible during camera movement; native HUD survives final composition; frame failures restore native rendering; test on physical device. Host GLES fixtures are not proof of Bedrock compatibility.
