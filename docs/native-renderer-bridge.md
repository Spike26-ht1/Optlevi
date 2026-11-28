# Native renderer bridge contract (Android GLES)

This bridge is installed through the existing game EGL overlay. It observes
native draw entrypoints only on the selected game's EGL context while a
shader pack requests world resources. Presentation and the OptLevi UI
are excluded from capture. A native terrain signature can be reported, but
is **not** used to infer Java matrices or full-scene phase boundaries.

A version-specific material/renderer adapter must emit explicit, active
uniforms on a dedicated draw at the correct phase, with the source FBO bound:

- OptLeviDepth0, OptLeviDepth1, OptLeviDepth2: ivec3(width,height,GL internal
  format), respectively complete scene, before translucent geometry, and
  opaque scene without hand.
- OptLeviColor0 through OptLeviColor15: ivec4(color attachment index,width,
  height,GL internal format), already encoded according to the chosen Java
  pack's geometry buffer contract.
- OptLeviShadowDepth0/1: same ivec3 contract, light-space depth snapshots.
- OptLeviShadowColor0/1: same ivec4 contract, light-space color.
- OptLeviWorld_<Java uniform name>: active, explicitly mapped scalar,
  vector, or matrix. Values must be in the Java pack's coordinate convention.

The marker must be a dedicated, non-mutating phase draw, with one marker
program per phase and metadata initialized by the renderer. Captures occur
before the marked draw. The runtime does not synthesize missing resources.
No marker material for Bedrock 26.62 is supplied by this commit. The existing
Custom Buffers 26.52 example exposes a combined view-projection and terrain
camera/fog values, not this phase contract. The Windows RenderDragon 26.51
material IO catalog is not an Android ABI or a Java G-buffer encoding.

Compiler log now includes observed native program count, marker count, and
capture errors. This is not proof of stock Bedrock or BSL gameplay support.

## Native terrain projection recovery

When the observed program exposes the RenderChunk uniform signature, the bridge can recover a centered finite OpenGL perspective and its inverse from the actual `u_viewProj` matrix and independently cross-check `ViewPositionAndTime`. It rejects unsupported projections, ill-conditioned matrices, and inconsistent eyes. It publishes only `gbufferProjection` and `gbufferProjectionInverse`; no Java model-view matrix, absolute camera position, or draw-phase boundary is inferred. This path has not been verified on Bedrock 26.62 hardware.

## Deferred phase

A dedicated `OptLeviDeferred` active `ivec2(width,height)` marker executes the enabled deferred programs after opaque resource captures, before translucent geometry. The native adapter can borrow `deferredTexture(colortex)` for the current frame. A later explicit capture of an updated attachment replaces the opaque/deferred texture before composite. No deferred pass is run at swap. This interface does not itself supply a Bedrock material which writes or consumes those buffers.

Native translucent materials can declare `s_OptLeviDeferred0` through `s_OptLeviDeferred15` as active `sampler2D` uniforms. The draw bridge binds the matching live deferred output on the material's reserved sampler unit for each draw, then restores the original binding. A material must reserve distinct units, use actual pack-specific encoding, and submit updated geometry attachments explicitly. Stock materials do not declare these samplers.

The runtime derives `gbufferProjectionInverse` and `gbufferModelViewInverse` only from valid supplied current matrices, and preserves the last successfully presented camera/projection/model-view for previous-frame uniforms. Explicitly published values override derived values; first-frame history is seeded from the actual current values, and interrupted frames reset history. The adapter still must provide an actual Java-convention model-view and absolute camera position.

## Java geometry program ABI

The loader resolves original Overworld `gbuffers_*` and `shadow*` pairs. A separate GLES geometry compiler maps position, color, normal, UV0, UV1, entity ID, mid-UV, tangent and mid-block attributes to locations 0–8 and reflects actual MRT target routing. `acquireGeometryProgram` returns a borrowed linked program for a native adapter; it does not replace an arbitrary RenderDragon program or assume its vertex layout matches. The adapter must construct the VAO, bind actual per-draw matrices/material inputs, and render the geometry into the pack's attachments. Geometry source is not persisted outside the selected pack and is not included in releases. No Arke source was available; this is an independent implementation.

`drawGeometry` submits mapped native VAOs and optional index buffers to selected geometry or shadow programs, binds reflected uniforms/textures, rejects missing inputs and framebuffer feedback, and restores GL bindings. The adapter supplies an independent color/depth FBO with attachments in reflected route order and sets the correct depth/blend/cull state before each draw. It must provide the actual Bedrock meshes and light-space matrices, then capture completed attachments using the existing scene/shadow phase APIs. This does not yet install a stock-material mesh extractor.

## Packed terrain face translation

`ShaderMeshMapping.h` loads the selected pack's version-conditional `block.properties` rules and translates verified planar RenderChunk quads using the supplied Custom Buffers 1.1.0 packed UV formulas. It computes face normals, UV tangents, texture center and the BSL mid-block offset. The lightmap conversion uses an assumed 16-level quantization of the decoded native normalized values; verify it on the target renderer before claiming lighting parity. The adapter must identify the actual block and state, normalize its Java name, and supply positions in the same coordinate space as its block center. Arbitrary meshes, unmapped/ambiguous states and degenerate faces are not silently converted. No hook for Bedrock's block tessellator is provided here.

`drawTerrainQuads` converts a bounded batch of verified native faces to the nine-attribute Java ABI, uploads vertex/index buffers, submits the selected geometry or shadow program, and restores native buffer bindings. It implements the ordinary unlisted-block ID zero convention without conflating an absent block identity with an unlisted canonical block. This path still requires the real tessellator/chunk-mesh provider and validated lightmap conversion on the target version.

## Java calculated uniforms

The loader parses scalar float `variable`/`uniform` declarations from `shaders.properties`, including MC_VERSION branches and continuations. The bounded expression evaluator supports the BSL default arithmetic, comparison, conditional, membership and smoothing expressions. Derived values are calculated from real published scalar/vector data and supplied to both geometry and fullscreen passes. `smooth(id,value,up,down)` uses the documented tick half-life and successful-frame history (https://shaders.properties/current/reference/shadersproperties/custom_uniforms/). Missing inputs and cyclic dependencies produce no value. The native provider still supplies `sunAngle`, `blindness`, `biome`, the Java biome constants and actual camera data. Other uniform types and option-dependent custom-uniform declarations are not yet supported. No material.bin or Custom Buffers changes.

## Java shader pack quality profiles

The shader menu discovers `profile.*` definitions in `shaders.properties`, resolves inherited settings and applies the selected profile to original expanded vertex/fragment source and pass-option probes. Numeric options must match the pack's declared allowed values. Cycles, unsupported options and missing declarations fail explicitly. The default profile preserves the original sources, and the selected profile is session-local (no file edits). The single-pass preview still uses the default source; Apply uses the chosen profile. This is independent of Custom Buffers and material binaries.

## Independent pack resource lifecycle

Pack-owned PNG textures are initialized before compiling the fullscreen graph and remain available across missing-world-input retries. Geometry programs can draw while composite is waiting, avoiding a circular dependency on G-buffer generation. Geometry sampler reflection also resolves current-frame depth/shadow snapshots and deferred output or native colortex snapshots where available. Material atlas aliases are never treated as scene color in geometry. Pack resources are released when selection changes, on permanent failure, or when disabled; an EGL context change invalidates old names. No Custom Buffers or material binaries are modified.

## Phase-specific readiness

Enabled deferred graphs link without requiring future-frame native inputs to already exist at link time. External color targets, shadow/depth attachments and typed world uniforms are validated at the phase that actually consumes them. Missing late composite data preserves the vanilla presentation and resets potentially partially written GPU history without discarding the linked deferred graph. The existing standalone fullscreen diagnostic graphs still validate initial input availability during compilation. This change does not provide native world inputs by itself.

Java geometry and fullscreen programs share engine-owned frame counter, modulo, viewport, aspect ratio and monotonic frame timing values. Timing initializes once for an EGL frame; ordinary geometry supplies its real viewport, while a shadow-stage adapter may call `beginJavaFrame` with the world viewport before rendering a differently sized shadow map. In-frame viewport changes fail closed. These built-ins are available to the CPU custom-uniform evaluator as well. No native material modifications.

## Geometry program controls

Original `program.<name>.enabled` and dimension-specific overrides now gate geometry and shadow compilation through the same GLES preprocessor probe used for fullscreen programs. `alphaTest.<program>` comparisons are applied to the first fragment output before any routed color or depth write, implementing the explicit OptiFine/Iris property on GLES. Only declared alpha overrides are applied; implicit per-render-type defaults still depend on the native adapter. No material binaries are changed.

## Java orthographic shadow camera

The engine reads literal shadow camera settings from the expanded/profile-adjusted shadow vertex source. It computes a camera-relative orthographic shadow view/projection and validated inverses from actual Java-convention `sunAngle` and `cameraPosition`, matching the rotation, signed grid remainder and OpenGL clip-space convention documented by Iris 1.21.1 (https://github.com/IrisShaders/Iris/blob/1.21.1/common/src/main/java/net/irisshaders/iris/shadows/ShadowMatrices.java). Unsupported perspective FOV or conflicting/nonliteral settings do not enable automatic camera calculation. Explicit supplied shadow matrices take precedence as a complete set. `getShadowCamera` exposes the computed transform and requested map size to the future renderer adapter; this does not create shadow geometry or claim real world shadow rendering. No material binaries are modified.

## Engine-owned shadow rendering

`renderShadowScene` receives explicit camera-relative mapped world VAOs and per-draw texture/uniform bindings for opaque and translucent casters. It creates/reuses its own light-space depth and RGBA8 color textures, applies the selected Java shadow camera, draws actual geometry with the original translated shadow programs, copies the opaque-only depth before translucent casters, and publishes both depth maps and written color buffers only on success. GPU allocation is included in the existing budget. This is an orthographic, two-color, depth-writing path; more shadow attachment formats, per-program blending and culling directives need additional support. No stock Bedrock mesh provider or material binary is installed by this change. The GLES fixture tests the entire path with known indexed/array mesh data, not live Bedrock gameplay.

## Engine-owned world phase coordinator

`renderJavaWorldScene` consumes actual mapped world batches and owns the main geometry framebuffer attachments. It executes optional supplied shadow caster batches, then opaque world geometry, saves pre-hand depth, draws the hand, saves pre-translucent depth, executes deferred, seeds updated geometry destinations from deferred outputs, and draws translucent geometry. It publishes only current-frame outputs. Default color clears follow Iris's fog/white/black convention; geometry blend mode is the standard source-alpha mode when explicitly requested. Custom per-target blend/clear directives, history-backed geometry outputs, and read/write feedback are not supported by this coordinator. The native provider must provide every required mesh, state and material texture; this commit does not install a provider for stock Bedrock. Composite/final still run at the existing beforeSwap hook. The GPU regression uses controlled geometry, not a live Minecraft world.

## End-to-end first-party world effects

The complete LeviPack embeds `OptLevi-Lab-WorldFX-0.2.zip` directly under `OptLevi/shaderpacks/`, the same directory used by the native shader browser. The one-click **Play with Lab World FX (Balanced)** control submits its three-pass shader graph through the real Java shader-pack loader and world runtime. PERFORMANCE disables the bloom passes entirely, BALANCED enables extraction/blur/final, and QUALITY increases the color effect. At the current swap hook this processes the composed game backbuffer (including Bedrock HUD); it is a verifiable first-stage world postprocess, **not** full Java G-buffer rendering or BSL. No manual SAF import or custom material.bin is required for this first-party effect.

The host integration runner now compiles the generated WorldFX ZIP with PERFORMANCE, BALANCED and QUALITY profiles and reads the actual GLES framebuffer pixels. It additionally tests the standalone geometry/deferred/shadow Lab World pack. The complete native package includes the exact WorldFX ZIP, so first-party deployment no longer requires a separate file manager import.

## Live frame metrics

The shader screen reports successful frame presentations, waiting-for-opaque-stage frames, failed pipeline submissions and a smoothed CPU time spent issuing composite/final GLES commands. These numbers are reset on shader selection changes. CPU submission time is not GPU render time, end-to-end frame latency or game FPS; no blocking `glFinish` or synchronous GPU timer query is introduced. Use the game FPS counter to judge actual performance.

## Native geometry layout discovery (read-only)

The draw hook now records the active vertex attributes, element buffer, VAO identity, component sizes/types, normalization, stride, VBO and instance divisors for the first linked GLES program that actually declares `u_viewProj` plus `s_MatTexture`. The report persists across frames for that EGL context and can be read in the Shader screen's Renderer log even after successful compilation. It is **diagnostic only**: it neither replays arbitrary pointers nor assumes an attribute name implies Java semantics, and it makes no guesses about a missing shader material. This is the evidence needed to implement the remaining native Bedrock geometry provider safely on the user's tested version.

## Scaled Java intermediate color buffers

The engine supports bounded `size.buffer.colortexN=widthScale heightScale` properties (0.125..1.0, colortex 1..15). It allocates each intermediate ping-pong texture at its actual size, adjusts stage viewports, and preserves Iris's full-screen viewWidth/viewHeight uniforms. MRT passes with different dimensions and scaled external geometry are rejected rather than silently distorted. WorldFX bloom extraction and blur run at half resolution; the final pass still presents the full-sized scene. This reduces intermediate color storage and shaded pixels without reducing Minecraft's render resolution. Memory estimates reflect the actual scaled allocations. No material binary changes.

## Native terrain framebuffer effect

The new `OptLevi-Lab-Terrain-0.1.zip` is embedded in the complete LeviPack. Its explicit `optlevi.nativeTerrainPhase=true` property selects a separate native phase. Recognized `u_viewProj`/`s_MatTexture` depth-writing nonblended terrain draws record their actual framebuffer and viewport; before leaving that framebuffer (or at swap when it remains bound), the hook captures real normalized 8-bit color and single-sample depth, using a distinct `optNativeTerrainDepth` sampler. The Java final pass reconstructs view-space positions from the validated projection inverse and calculates contact shading. The output is blitted back to the same framebuffer, not a fake G-buffer. Unsupported signatures, formats or multiple draw targets are rejected; no stock world phase or BSL light-space shadow is claimed. Default framebuffer processing may include HUD. Device testing remains required.

The renderer log additionally records a bounded sample of linked native uniform names when the terrain signature is not recognized, exposes the recognized terrain-program count, and offers a copy action. A frame without a successful native terrain phase no longer reports an active shader just because a previous frame succeeded. The existing WorldFX action is preserved; select the separately embedded Lab Terrain ZIP in the normal list and Apply for depth-based contact shading.

## Phase 1 — fail closed and recover the native-depth regression

The original Lab Terrain host fixture drew positive clip-space Z and incorrectly used an identity inverse projection. Its occlusion predicate expects conventional negative-Z view space; the fixture now draws real perspective-projected surfaces at -2.5 and -2.0, inverts that same projection, prints both sampled pixels, and retains the original dark-edge assertion. It checks separate read/draw framebuffer bindings, program, VAO, active texture and depth-test state. This is a fixture, not a Bedrock camera claim.

Uniform matches, VAO layout and framebuffer departure are observations, not phase ownership. Automatic Lab Terrain writes on guessed framebuffer departure/swap are disabled until a verified phase provider exists; the explicit runtime API remains tested. The log labels candidates as unverified and captures attribute integer mode and VBO offsets, and candidate viewport/sample/draw buffer values. No pointers are dereferenced. Geometry-bearing non-deferred graphs now require verified colortex0 rather than falling back to the composited backbuffer. No shader catalog, MBL2, material.bin, UI, registry, or third-party shader source changes.

## Exportable diagnostic journal

Shaders > Log > Save log to Downloads creates a timestamped text file in Android 10+ Downloads/OptLevi through MediaStore, without requesting broad storage access. Copy renderer log is the fallback on older Android or export failure. The bounded journal retains sampled world candidates and runtime status changes even when the UI moves to a menu. No upload is performed. This does not establish that the shader renderer is connected to real world geometry. The host checks journal bounds/deduplication/concurrent access; actual MediaStore publication requires a device.

The diagnostic observer now records the first actual draw per linked program (including previously unclassified programs): draw mode/count/instances, EBO and index offset only for buffer-backed indices, VAO attribute formats, FBO object/component/bit metadata, viewport, samples and depth/blend state. It uses only GLES 3.0 attachment queries, not guessed framebuffer dimensions. A separate retained last-world-candidate snapshot survives menu-only draws; unknown signatures are sampled as well. These are observations, not phase or material classification, and the observer never replays geometry.

## Automatic external media logs

OptLevi discovers the Android app-specific media directory using `Context.getExternalMediaDirs()` and creates `OptLevi/logs/latest.log` automatically on native enable, with a retry every three seconds on accepted EGL swaps. Disable flushes the bounded journal; a new process preserves the prior `latest.log` as `previous.log`. Atomic replacement prevents partial reports from replacing a complete file. No button, broad storage permission or network upload is required. Shader requests/rejections, hook installation results, including zero-observed-draw cases, and recent renderer diagnostics are included. Host tests check persistence/rotation and preservation after a write failure; the Android media location still requires device verification.

## Native GLES hook coverage

Uses the pinned Preloader `pl::memory::hook` API, with individual installation results for exported and EGL-resolved entrypoints. Adds core GLES range-indexed drawing, clears, framebuffer blits, and context-supported GLES 3.2/OES/EXT base-vertex indexed and instanced draws. Same-address aliases are skipped. The bounded GL event sequence records draw program, FBO changes, clears and copies in actual order, and counters distinguish all hooked draws from draws outside the accepted render context. These are observational hooks: no guessed world phase, raw pointer dereference, or replacement of material.bin. The SDK does not provide verified native Minecraft world callbacks, so this is not the missing world provider itself. Host test covers sequence order, coalescing and bounded storage; actual detour coverage requires device evidence.
