#include "GameUiTouchBridge.h"
#include "ModMenuMod.h"
#include "State.h"
#include "platform/Jni.h"

#include <pl/Input.hpp>

#include <atomic>
#include <cmath>

namespace modmenu::android::gameui {
namespace {

// MinecraftActivity dispatches raw MotionEvent coordinates through PreloaderInput
// *before* forwarding them to the game's view. No extra Android View is used.
//
// These fractional bounds are calibrated to the user's 1536x709 screenshot of
// the ORIGINAL Mods button position. They are a temporary fallback until the
// Bedrock StartMenuScreenController JSON-UI event callback can be hooked safely.
// Re-enable the original JSON UI layout whenever changing these values.
constexpr float kMinX = 0.244f;
constexpr float kMaxX = 0.326f;
constexpr float kMinY = 0.700f;
constexpr float kMaxY = 0.795f;
constexpr int kDown = 0;
constexpr int kUp = 1;
constexpr int kMove = 2;
constexpr int kCancel = 3;
constexpr int kPointerUp = 6;

std::atomic<bool> gEnabled{false};
bool gRegistered = false;
int gCapturedPointer = -1;

bool getWindowSize(float& width, float& height) {
    JNIEnv* env = jni::env();
    if (!env) return false;
    if (env->PushLocalFrame(16) != 0) return false;
    bool ok = false;
    jobject activity = jni::currentActivity(env);
    jclass activityClass = activity ? env->GetObjectClass(activity) : nullptr;
    jmethodID getWindow = activityClass
        ? env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;") : nullptr;
    jobject window = getWindow ? env->CallObjectMethod(activity, getWindow) : nullptr;
    jclass windowClass = window ? env->GetObjectClass(window) : nullptr;
    jmethodID getDecorView = windowClass
        ? env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;") : nullptr;
    jobject view = getDecorView ? env->CallObjectMethod(window, getDecorView) : nullptr;
    jclass viewClass = view ? env->GetObjectClass(view) : nullptr;
    jmethodID getWidth = viewClass ? env->GetMethodID(viewClass, "getWidth", "()I") : nullptr;
    jmethodID getHeight = viewClass ? env->GetMethodID(viewClass, "getHeight", "()I") : nullptr;
    if (view && getWidth && getHeight) {
        width = static_cast<float>(env->CallIntMethod(view, getWidth));
        height = static_cast<float>(env->CallIntMethod(view, getHeight));
        ok = width > 0.0f && height > 0.0f;
    }
    if (auto ex = jni::takeException(env); !ex.empty()) {
        log().warn("OptLevi touch bridge: could not read window size: {}", ex);
        ok = false;
    }
    env->PopLocalFrame(nullptr);
    return ok;
}

// Fail closed in the game HUD and pause menu, even if the user taps the same
// position as the start screen's Mods button. For other menu pages, the
// version-specific controller hook remains necessary for perfect gating.
bool isStartMenuCandidate() {
    if (!gEnabled.load() || !state().active.load() || state().menuOpen.load())
        return false;
    JNIEnv* env = jni::env();
    if (!env) return false;
    if (env->PushLocalFrame(8) != 0) return false;
    bool allowed = false;
    jclass cls = jni::appClass(env, "org.levimc.launcher.preloader.PreloaderInput");
    jmethodID hud = cls ? env->GetStaticMethodID(cls, "isHudScreenOpen", "()Z") : nullptr;
    jmethodID pause = cls ? env->GetStaticMethodID(cls, "isPauseMenuOpen", "()Z") : nullptr;
    if (hud && pause) {
        bool hudOpen = env->CallStaticBooleanMethod(cls, hud);
        bool pauseOpen = env->CallStaticBooleanMethod(cls, pause);
        allowed = !hudOpen && !pauseOpen;
    }
    if (auto ex = jni::takeException(env); !ex.empty()) {
        log().warn("OptLevi touch bridge: game state unavailable: {}", ex);
        allowed = false;
    }
    env->PopLocalFrame(nullptr);
    return allowed;
}

bool inModsButton(float x, float y) {
    float w = 0.0f, h = 0.0f;
    if (!getWindowSize(w, h)) return false;
    return x >= w * kMinX && x <= w * kMaxX &&
           y >= h * kMinY && y <= h * kMaxY;
}

bool onTouch(pl::input::TouchEvent const& e) {
    if (!gEnabled.load()) {
        gCapturedPointer = -1;
        return false;
    }
    if (e.action == kDown) {
        gCapturedPointer = -1;
        if (inModsButton(e.x, e.y) && isStartMenuCandidate()) {
            gCapturedPointer = e.pointerId;
            return true;
        }
        return false;
    }
    if (e.pointerId != gCapturedPointer) return false;

    if (e.action == kMove) return true;
    if (e.action == kCancel) {
        gCapturedPointer = -1;
        return true;
    }
    if (e.action == kUp || e.action == kPointerUp) {
        gCapturedPointer = -1;
        if (inModsButton(e.x, e.y) && isStartMenuCandidate()) {
            log().info("OptLevi Mods JSON UI button tapped; opening native mod browser");
            openMenu();
        }
        return true;
    }
    return false;
}

} // namespace

void install() {
    gEnabled.store(true);
    if (gRegistered) return;
    gRegistered = true;
    pl::input::registerTouchCallback(onTouch);
    log().info("OptLevi Android JSON UI position-based touch bridge enabled");
}

void uninstall() {
    // Preloader's input API does not offer callback removal.
    gEnabled.store(false);
    gCapturedPointer = -1;
}

} // namespace modmenu::android::gameui
