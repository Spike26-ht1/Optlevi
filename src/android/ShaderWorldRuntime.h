#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <utility>

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
// Called on the game render thread by a phase-aware renderer adapter, with
// a completed, single-sample depth attachment. Never infer phases at swap.
enum class DepthPhase : unsigned { Complete=0, BeforeTranslucent=1, BeforeHand=2 };
bool captureSceneDepth(DepthPhase phase,unsigned framebuffer,unsigned internalFormat,
                       int width,int height,std::string& error);
void beforeSwap(int width,int height);
void stop();
} // namespace modmenu::android::shaderworld
