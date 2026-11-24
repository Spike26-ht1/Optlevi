# OptLevi 0.12.0 — Android Documents import crash investigation

## Owner's 2026-11-23 device report

The owner reported that Android's file picker opens, imports a shader ZIP,
then Minecraft/LeviLauncher may close while returning to the shader menu.
A separate xCrash **Java** tombstone from a Samsung SM-A175F (Android 16,
LeviLauncher 1.6.1) records this exception:

```
java.lang.UnsatisfiedLinkError:
No implementation found for long
com.google.androidgamesdk.GameActivity.initializeNativeCode(...)
```

That tombstone occurs just 193 ms after process start, in
`GameActivity.onCreate -> MainActivity.onCreate -> MinecraftActivity.onCreate`.
It also reports `libpreloader.so` loaded, but the GameActivity native entry
point is missing. It has **no document picker call**, shader ZIP frames, or
OptLevi return-from-import Java frames. It is therefore **not evidence** that
OptLevi's import bridge caused that specific startup crash.

This Java crash needs separate investigation with the exact launcher APK,
game version and `libpreloader.so` symbols; it cannot be fixed confidently by
changing a resource pack or renaming shader files.

## Verified problematic 0.11 import design

The 0.11 bridge performs synchronous
`std::filesystem::copy_file(input,destination)` inside the
`PreloaderInput.nativeOnDocumentResult` JNI callback, which is called on
Android's main thread by `MinecraftActivity.onActivityResult`.
For large shader ZIPs this blocks resuming the Activity and increases the risk
of UI freezes or ANRs on mid-range hardware.

The launcher also **deletes the temporary selection immediately after**
the callback returns, so launching an asynchronous copy directly from the
temporary path would corrupt imports.

## 0.12 native bridge changes

- Validate the cached file and file extension without copying the whole pack.
- Rename the temporary file into a unique staging name in the **same private
  `preloader_documents` directory** before returning to Android. This makes
  the staged file survive the launcher's immediate original-path deletion.
- Complete the cross-directory copy in a managed native worker, not in
  `onActivityResult`; return results to the menu through the existing mutex.
- Reject arbitrary JNI paths and symlinks; preserve the 64-MiB limit and
  duplicate-safe imports, including the exact `.material.bin` suffix.
- Handle C++ exceptions inside the JNI callback; join the worker during
  native module uninstall to avoid executing unloaded code.
- Display shaderworld compiler errors ahead of obsolete import messages.

## What the change does *not* establish

- The uploader's specific crash is **not confirmed fixed** until an Android
  phone successfully imports a BSL ZIP and returns to the OptLevi menu.
- The uploaded startup tombstone remains unexplained on LeviLauncher 1.6.1.
- OptLevi 0.12 still has a **single final/composite GLES3 pass**, not a
  compatible Iris multi-pass deferred renderer. Full BSL terrain lighting,
  shadows, transparency and water require a RenderDragon scene-buffer backend.
- A successful GitHub Actions ARM64 build validates compilation and packaging,
  not the in-game Android graphics pipeline.

## Minimal reproducible check on device

1. Start Minecraft from LeviLauncher; verify this works even with OptLevi
   temporarily disabled. If not, investigate the GameActivity native loader.
2. In OptLevi 0.12, select **Import Shader Pack**, choose one ZIP and return.
   Verify that the import completes and the shader list refreshes.
3. Select BSL and use the compiler log to identify missing uniforms or passes.
   Only report a world shader as active when the Minecraft renderer actually
   displays the intended effect.

Please provide the **native** crash tombstone at the moment of import if the
process still dies: a Java startup tombstone cannot locate a C++ GPU crash.
