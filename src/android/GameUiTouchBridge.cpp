#include "GameUiTouchBridge.h"
#include "ModMenuMod.h"
#include "State.h"
#include "platform/Jni.h"

#include <pl/Input.hpp>

#include <atomic>
#include <cmath>
#include <chrono>
#include <thread>

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
// The second Mods button is inserted after the pause menu's open-book control.
// These normalized bounds are experimental and will be verified on device.
constexpr float kPauseMinX = 0.197f;
constexpr float kPauseMaxX = 0.250f;
constexpr float kPauseShadersMinX = 0.250f;
constexpr float kPauseShadersMaxX = 0.305f;
constexpr float kPauseMinY = 0.794f;
constexpr float kPauseMaxY = 0.938f;
constexpr int kDown = 0;
constexpr int kUp = 1;
constexpr int kMove = 2;
constexpr int kCancel = 3;
constexpr int kPointerUp = 6;

std::atomic<bool> gEnabled{false};
bool gRegistered = false;
std::atomic<int> gCapturedPointer{-1};
enum class Target : int { None, Start, Pause, Shaders };
std::atomic<Target> gCapturedTarget{Target::None};
// Development-only screen-state study counter (no coordinates or player data).
std::atomic<bool> gPauseStateWarningLogged{false};
std::atomic<unsigned> gPressRevision{0};

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
bool isScreenCandidate(Target target) {
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
        allowed = (target == Target::Pause || target == Target::Shaders) ? pauseOpen : (!hudOpen && !pauseOpen);
    }
    if (auto ex = jni::takeException(env); !ex.empty()) {
        log().warn("OptLevi touch bridge: game state unavailable: {}", ex);
        allowed = false;
    }
    env->PopLocalFrame(nullptr);
    return allowed;
}

bool inModsButton(Target target, float x, float y) {
    float w = 0.0f, h = 0.0f;
    if (!getWindowSize(w, h)) return false;
    if(target == Target::Shaders)
        return x >= w * kPauseShadersMinX && x <= w * kPauseShadersMaxX &&
               y >= h * kPauseMinY && y <= h * kPauseMaxY;
    if (target == Target::Pause)
        return x >= w * kPauseMinX && x <= w * kPauseMaxX &&
               y >= h * kPauseMinY && y <= h * kPauseMaxY;
    return x >= w * kMinX && x <= w * kMaxX &&
           y >= h * kMinY && y <= h * kMaxY;
}

bool onTouch(pl::input::TouchEvent const& e) {
    if (!gEnabled.load()) {
        gCapturedPointer.store(-1);
        gCapturedTarget.store(Target::None);
        return false;
    }
    if (e.action == kDown) {
        gCapturedPointer.store(-1);
        gCapturedTarget.store(Target::None);
        Target target = Target::None;
        if(inModsButton(Target::Shaders, e.x, e.y) &&
           isScreenCandidate(Target::Shaders)) {
            target = Target::Shaders;
        } else if (inModsButton(Target::Pause, e.x, e.y)) {
            if (isScreenCandidate(Target::Pause))
                target = Target::Pause;
            else if (!gPauseStateWarningLogged.exchange(true))
                log().info("OptLevi UI study: pause button region touched while pause-menu state unavailable");
        } else if (inModsButton(Target::Start, e.x, e.y) && isScreenCandidate(Target::Start)) {
            target = Target::Start;
        }
        if (target != Target::None) {
            gCapturedTarget.store(target);
            gCapturedPointer.store(e.pointerId);
            // Let Minecraft process ACTION_DOWN so the vanilla button draws
            // its pressed face, rather than stealing the animation.
            return false;
        }
        return false;
    }
    if (e.pointerId != gCapturedPointer.load()) return false;

    if (e.action == kMove) return false;
    if (e.action == kCancel) {
        gCapturedPointer.store(-1);
        gCapturedTarget.store(Target::None);
        ++gPressRevision;
        return false;
    }
    if (e.action == kUp || e.action == kPointerUp) {
        gCapturedPointer.store(-1);
        const Target target = gCapturedTarget.exchange(Target::None);
        if (target != Target::None && inModsButton(target, e.x, e.y) && isScreenCandidate(target)) {
            // ACTION_UP also goes to Minecraft to finish the vanilla press
            // animation. Open after a short nonblocking release interval.
            const unsigned revision = ++gPressRevision;
            log().info("OptLevi UI study: {} Mods control released; scheduling native browser",
                target == Target::Shaders ? "pause Shaders" :
                (target == Target::Pause ? "pause" : "start"));
            std::thread([revision,target] {
                std::this_thread::sleep_for(std::chrono::milliseconds(95));
                if (gEnabled.load() && state().active.load() &&
                    !state().menuOpen.load() && gPressRevision.load() == revision)
                {
                    if(target == Target::Shaders)
                        state().shaderOpenRequested.store(true);
                    openMenu();
                }
            }).detach();
        }
        return false;
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
    gCapturedPointer.store(-1);
    gCapturedTarget.store(Target::None);
    ++gPressRevision;
}

} // namespace modmenu::android::gameui
