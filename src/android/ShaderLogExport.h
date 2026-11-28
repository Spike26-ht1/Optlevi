#pragma once
#include <string>
namespace modmenu::android::shaderdiagnostics {
// Explicit user action only; writes a text report into Android Downloads.
// No storage permission or upload is requested.
bool exportDownloads(std::string const& contents,std::string& result);
// Automatic app-owned external media log. No UI, broad storage permission,
// or external upload. Retry when the activity/media volume is not ready.
void persistMediaLog(bool force=false);
}
