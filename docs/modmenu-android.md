# ModMenuBE Android — build and test

## Source used

OptLevi tracks the upstream [AFAlimru/ModMenuBE](https://github.com/AFAlimru/ModMenuBE) repository as a submodule, pinned to release commit `dcca382ec0a15e3849e9a65dcdf8fe64480d3a71` (version 0.2.1). The uploaded `ModMenuBE-0.2.1.zip` was inspected as the reference source. The upstream source is not silently relicensed or rewritten.

The Android target is a native `.so` mod for LeviLauncher. It uses the Preloader Android input API, an EGL swap hook, ImGui, and OpenGL ES 3. The source also contains a separate Windows GDK build; that is not the target of this test.

## Automated ARM64 build

The workflow `.github/workflows/modmenu-android.yml`:

1. Checks out OptLevi and its submodules.
2. Installs Android NDK 28.2.13676358.
3. Configures `ModMenuBE/android` for `arm64-v8a` and Android API 28.
4. Builds the `levi_package` target.
5. Verifies that the package contains an AArch64 `libModMenuBE.so`.
6. Uploads the `.levipack` as a workflow artifact.

A green workflow confirms that compilation and packaging succeeded. It does not prove that the overlay works inside Minecraft.

## Local build

From the OptLevi repository root on Linux:

```bash
cmake -S ModMenuBE/android -B build/modmenu-arm64-v8a -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_SDK_ROOT/ndk/28.2.13676358/build/cmake/android.toolchain.cmake" \
  -DANDROID_NDK="$ANDROID_SDK_ROOT/ndk/28.2.13676358" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/modmenu-arm64-v8a --target levi_package --parallel 2
```

The output should be:

```
build/modmenu-arm64-v8a/ModMenuBE-0.2.1-arm64-v8a.levipack
```

On Windows, the upstream helper is located at `ModMenuBE/android/scripts/build.ps1` (not at the repository root):

```powershell
powershell -ExecutionPolicy Bypass -File ModMenuBE\android\scripts\build.ps1
```

## Device test checklist

After a successful CI build, import the generated `.levipack` into the matching LeviLauncher installation and check:

- LeviLauncher recognizes the package and lists ModMenuBE in its inbuilt Mod Menu.
- The ModMenuBE module can be enabled.
- The floating **MOD** button appears in the game HUD.
- Tapping **MOD** opens the menu.
- Touches on the open menu do not rotate or move the game camera.
- Android Back navigates back through menu pages and closes the menu.
- The module list appears and shows installed mods.
- Closing the menu restores the launcher's HUD.
- The game remains stable after opening and closing the menu repeatedly.

Record the exact LeviLauncher version, Minecraft version, Android version, device ABI, and logs for any failure. Do not mark runtime support as verified from CI alone.

## Known compatibility points to verify

- The Android CMake file pins Preloader Android to commit `cc08b40a31471d3e2dedb267f5f17b28da2b0604`, chosen by the upstream ModMenuBE project to match LeviLauncher 1.6.1's runtime structures. Confirm compatibility with the exact launcher build used for testing before distributing the package.
- The overlay hooks EGL swap functions and renders with GLES3. Devices or game configurations that render through Vulkan may not display the overlay.
- Enabling/disabling another mod is handled through the launcher's `mods_config.json` and takes effect after restarting the game.
- The upstream Android README documents features unavailable on Android, including opening another mod's menu by hotkey and opening folders.

## License

ModMenuBE is MIT-licensed by AFAlimru. Preserve `ModMenuBE/LICENSE` and upstream notices. The OptLevi root MIT license does not replace the upstream license.
