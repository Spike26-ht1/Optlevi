#pragma once

#include <string>

namespace modmenu::android::launcher {

// Hides LeviLauncher's floating HUD (every floating button, ours included, the FPS counter, the logcat
// panel) while the menu is open, and shows it again afterwards. Safe from any thread.
void setHudHidden(bool hidden);

// Open the LeviLauncher HUD editor via its public Java overlay manager.
void openModMenu();

// An Android Toast (long). Call on the UI thread.
void toast(std::string const& text);

} // namespace modmenu::android::launcher
