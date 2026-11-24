#pragma once
#include <string>

namespace modmenu::android::shaderbridge {
// Install native draw entry hooks, independent of the EGL presentation hook.
// Only the selected game's EGL context is inspected, and only while a pack
// has requested world resources. No unverified render-phase guesses.
bool install();
struct PresentGuard {
    PresentGuard();
    ~PresentGuard();
    PresentGuard(PresentGuard const&)=delete;
    PresentGuard& operator=(PresentGuard const&)=delete;
};
std::string report();
}
