#pragma once
#include <string>

namespace modmenu::android::shaderworld {
// Deliberately restricted GL ES3 POSTPROCESS pass, not a full Iris pipeline.
// State is copied across threads; all GL calls happen on the EGL swap thread.
struct Status {
    bool active=false;
    bool failed=false;
    std::string message;
};
void requestEnable(std::string translatedFragment, std::string sourceLabel);
void requestDisable();
Status status();
void beforeSwap(int width,int height);
void stop();
} // namespace modmenu::android::shaderworld
