#pragma once
#include <string>
namespace modmenu::android::shaderdiagnostics {
// Explicit user action only; writes a text report into Android Downloads.
// No storage permission or upload is requested.
bool exportDownloads(std::string const& contents,std::string& result);
}
