#pragma once
#include <string>
#include <vector>

namespace modmenu::android::shaderworld {
// Ordered fullscreen pass graph. Only buffers actually produced by a pass
// can be read; the initial real frame provides colortex0, not a fake G-buffer.
struct Pass {
    std::string name;
    std::string fragment;
    std::vector<unsigned> targets;
    bool finalPass=false;
    // Translated original fullscreen vertex stage; empty for diagnostic passes.
    std::string vertex;
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
void beforeSwap(int width,int height);
void stop();
} // namespace modmenu::android::shaderworld
