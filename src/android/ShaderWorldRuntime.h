#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>
#include <array>

namespace modmenu::android::shaderworld {
// Ordered fullscreen pass graph. Only buffers actually produced by a pass
// can be read; the initial real frame provides colortex0, not a fake G-buffer.
struct PackTexture {
    std::string sampler;
    int width=0,height=0;
    bool nearest=false;
    std::vector<std::uint8_t> rgba;
};
struct Pass {
    std::string name;
    std::string fragment;
    std::vector<unsigned> targets;
    bool finalPass=false;
    // Translated original fullscreen vertex stage; empty for diagnostic passes.
    std::string vertex;
    // Resolve the active conditional DRAWBUFFERS mapping from linked outputs.
    bool routedOutputs=false;
    // Shared pack-level composite textures are attached to the first pass only.
    std::vector<PackTexture> packTextures;
    // Optional GLSL preprocessor-only program.enabled probe. Disabled passes
    // must not compile, allocate targets, or satisfy later dependencies.
    std::string enabledProbe;
    // Pack-wide attachment formats, present only on the first graph pass.
    std::vector<std::pair<unsigned,std::string>> colorFormats;
    // Deferred programs run at the opaque/translucent boundary, not at swap.
    bool deferred=false;
    // Clear=false history is reflected via enabledProbe; no history buffer
    // is usable without an actual writer in the graph.
};
struct Status {
    bool active=false;
    bool failed=false;
    std::string message;
};
void requestPipeline(std::vector<Pass> passes,std::string label);
void requestEnable(std::string translatedFragment,std::string sourceLabel);
void requestDisable();
Status status();
// Remains true for a selected pack awaiting native resources, even if its
// first compilation was rejected. False when Off or Disable is selected.
bool captureRequested();
// Called on the game render thread by a phase-aware renderer adapter, with
// a completed, single-sample depth attachment. Never infer phases at swap.
enum class DepthPhase : unsigned { Complete=0, BeforeTranslucent=1, BeforeHand=2 };
bool captureSceneDepth(DepthPhase phase,unsigned framebuffer,unsigned internalFormat,
                       int width,int height,std::string& error);
// Capture a native geometry color attachment with explicitly verified Java
// semantics. The adapter must select the correct phase and attachment;
// an arbitrary native color attachment is not automatically a G-buffer.
bool captureSceneColor(unsigned colortex,unsigned framebuffer,unsigned attachment,
                       std::string const& format,int width,int height,std::string& error);
// Independent light-space resources; the adapter must render actual shadows.
bool captureShadowDepth(unsigned index,unsigned framebuffer,unsigned internalFormat,
                        int width,int height,std::string& error);
bool captureShadowColor(unsigned index,unsigned framebuffer,unsigned attachment,
                        std::string const& format,int width,int height,std::string& error);
// Typed data from a verified world-rendering phase. Matrices are column
// major and must already use the Java shader's coordinate convention.
struct WorldUniform {
    std::string name;
    unsigned glType=0;
    std::array<float,16> floats{};
    std::array<int,4> ints{};
};
struct UniformMapping {
    std::string nativeName;
    std::string javaName;
    unsigned glType=0;
};
// Either submit native world-state values, or read a verified linked native
// GLES program's actual uniform values using explicit semantic mappings.
// All values expire at the next swap; missing data is never defaulted.
bool publishWorldUniforms(std::vector<WorldUniform> const& values,std::string& error);
bool captureProgramUniforms(unsigned program,std::vector<UniformMapping> const& mappings,
                            std::string& error);
// Called after all opaque snapshots and uniforms have been captured, before
// translucent draws. Outputs are borrowed until the next swap; the native
// adapter must consume them using the pack's actual G-buffer semantics.
bool renderDeferred(int width,int height,std::string& error);
unsigned deferredTexture(unsigned colortex);
void beforeSwap(int width,int height);
void stop();
} // namespace modmenu::android::shaderworld
