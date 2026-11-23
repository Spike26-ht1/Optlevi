#pragma once
namespace modmenu::android::gameui {
// Registers a native Android touch bridge. No floating Android overlay is created.
// The displayed control continues to come from the Bedrock Levi API JSON-UI pack.
void install();
void uninstall();
} // namespace modmenu::android::gameui
