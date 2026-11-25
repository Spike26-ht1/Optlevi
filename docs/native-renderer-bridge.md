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
