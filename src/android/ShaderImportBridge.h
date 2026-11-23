#pragma once
#include <string>
namespace modmenu::android::shaderimport {
// Bridges the launcher's EXISTING Android ACTION_OPEN_DOCUMENT flow to OptLevi.
// The launcher supplies a temporary app-private file and deletes it immediately
// after the callback; the callback must copy the file before returning.
struct Result { bool success = false; std::string message; };
bool install();
void uninstall();
bool openPicker();
bool consumeResult(Result& result);
bool available();
} // namespace modmenu::android::shaderimport
