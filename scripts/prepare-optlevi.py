#!/usr/bin/env python3
"""Prepare OptLevi from pinned ModMenuBE; preserve original third-party source/attribution."""
from pathlib import Path
import base64
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ModMenuBE"
PNG_B64 = "iVBORw0KGgoAAAANSUhEUgAAACgAAAAoCAYAAACM/rhtAAAE0UlEQVR42u2Ya0xTZxjHf71xabUUqNpaFK/ILhrD8AIfAEHCNHObH0YmgzCdbjMxZCwjJjKVbc5tupC5zYQ5lRATNUznZdlwqxeYmx9EN1gUA7N2gEKt2NJCafHAYR/OysY+LEUb7BKfj+e8583vff7P/3nP+8r6euxDhHDICfF4BPi/AfQ2t9DndocW4IAgSHAN5wh7ZgX9P58NLUClSoXH1olq3TYARHN9aAH2ud2Er8hHntRBZKWPfrMZb3NL6AB6yz5CqbMS9mw/okvBpIQ+Bo6dDQ3Au2+WovvmCGFbB7l3Mpz+/WqUWT4ij3718AGdNcfpN5tRFg8CoJri5XaLGtU8AWu3DWfN8YcH6LF10lVUiiG1B5VGAAuIMUo8Po8EO9s1KrMEHbD97S3DWRtslyF4VMPvHG2rmZmqpN9sfjiA3uYW7p74nhnzZCidESidEcgdAwBoIjQAKKYMcbX3Fh5b59gB+hvy4OWDAPRr9fT9ocTbK4H26PL5TalFiJ6PqFdImW68OnaASpUk4/ljLUzUjSeiTQRgqEuB02FAiJ4PQIQxCnmXZJxBa/uIxY2JxILDhSZCg+hWIeuOQeHuQ8zOoKPVyb2YcAmsXQaAzXojsMUHMuja11JbUBgMAExRexENiWgMxuEs+LOoS1bDFRDdKmw+PVGZr2F5aS3qzHQGm+qG5xR7XMED/H33l7ReHNkahvQxzE5OYmZxEQmpi6XsjFNT1xtFjvYmNruP8OxsZGoNrRfryX9rHfLqT7DclkwjM8U9eA3660OdmU6HQkHD5EnY9bF0KBR0Ol1cP3WayuUrObNxMwDjFy6gzdlNy/Q5eHweYsvf58JnFYQtSgZg35EmfJOXcY3xxCUnjajf+wL0f+yfDCCrbBPKaB0Adn0sxugoTn+xl0uHqkl5pYCbFisTtBMxvvMCAE1793PP5WLHqvUkFryIfd5cABJzlgbPxVNTFmKMjsJru8PpH86w5thhVNOmYnE4uYNU9Cc2bUVjMDJRH8OvsVGMyyujtuoAnU6p1nJ27SA+P4+aLR+Quv3dgBwcEOCAIKDWankiV8pIzcnvMO+vYuOp42QU5mFxOKVxzm6sDY089vxzWM7VAnCmbDtZJcWsv1CL3OWkcvlKskqKSV6VG5C8ALJAj50eWycfpmRicThpFkWyExNYs+tjxJ5e6naWU1f/C5srK5BpxnFwYymZxRvYU1TCrOnxDLncCPpYXt5WFrC0owL0t5FLh6opf70IgGZRZI5cTmZeLsve2MC3uytISlmMMWk+ny9KRxmtI/7pbKbFxTF1Sdqw00cbstEe3GurDrCnqGTEs3iZjJxdO8goLMDa0Ehpeg4zY6J5z3pt7E91GYUF7Lz4IxmFecyaHg9A69AQe4pKuNXcQlhkJADpa1cHbIQHbtT/DtOcBF79tJwBQaDLbsd7p4vICXoMJhPWhkbJ+UvSAjZC0AH/2ScNJhOYTH9vi02SrHFPPh5aB3e/lB3nLxCWMAu1VhtagH4pr9/qYMZfu8WD1l/Qf7cGBIG2up9IS0sNzbuZG/WXaRZFEpdmBsUgQf/lP7yvirmJCZJxQimDSpWKPrebE9VHyS3eELT6u6+d5L/uYW5eaWLGgqeCIm3QAR/dsD4CvM/4ExrS43LC4HJkAAAAAElFTkSuQmCC"

def replace_once(file: Path, old: str, new: str):
    value = file.read_text(encoding="utf-8")
    count = value.count(old)
    if count != 1:
        raise RuntimeError(f"{file}: expected exactly one occurrence of {old!r}, got {count}")
    file.write_text(value.replace(old, new), encoding="utf-8")

def main():
    if not (SRC / "android/src/Entry.cpp").exists():
        raise SystemExit("Initialize the ModMenuBE submodule before running this script")
    png = base64.b64decode(PNG_B64, validate=True)
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError("Invalid embedded OptLevi PNG")
    icon = SRC / "android/src/OptLeviIcon.inc"
    icon.write_text(
        "#pragma once\n// Icon supplied by OptLevi project owner.\n"
        "static constexpr unsigned char kOptLeviIconPng[] = {\n" +
        "".join("  " + ", ".join(f"0x{b:02x}" for b in png[i:i+12]) + ",\n"
                for i in range(0, len(png), 12)) + "};\n", encoding="utf-8")
    (SRC / "assets/icon.png").write_bytes(png)

    cm = SRC / "android/CMakeLists.txt"
    for old, new in [
        ("project(ModMenuBE_Android LANGUAGES C CXX)", "project(OptLevi_Android LANGUAGES C CXX)"),
        ('set(MOD_ID "ModMenuBE")', 'set(MOD_ID "OptLevi")'),
        ('set(MOD_NAME "ModMenuBE")', 'set(MOD_NAME "OptLevi")'),
        ('set(MOD_AUTHOR "AFAlimru")', 'set(MOD_AUTHOR "BRSpike26, AFAlimru")'),
        ('set(MOD_VERSION "0.2.1")', 'set(MOD_VERSION "0.1.0")'),
        ('set(MOD_LIBRARY_NAME "ModMenuBE")', 'set(MOD_LIBRARY_NAME "OptLevi")'),
    ]:
        replace_once(cm, old, new)

    entry = SRC / "android/src/Entry.cpp"
    for old, new in [
        ('#include "ButtonIcons.inc"', '#include "OptLeviIcon.inc"'),
        ('constexpr const char* kModuleId = "modmenube";', 'constexpr const char* kModuleId = "optlevi";'),
        ('constexpr const char* kButtonId = "modmenube.open";', 'constexpr const char* kButtonId = "optlevi.open";'),
        ('constexpr int   kLayoutVersion   = 2;', 'constexpr int   kLayoutVersion   = 3;'),
        ('constexpr float kIconDp          = 24.5f;', 'constexpr float kIconDp          = 31.0f;'),
        ('constexpr float kGapDp           = 2.5f;', 'constexpr float kGapDp           = 2.0f;'),
        ('constexpr float kVanillaHalfSpan = 39.25f;', 'constexpr float kRightFraction   = 0.83f;'),
        ('jmethodID setSize = e->GetMethodID(mgrCls, "setOverlayButtonSize", "(Ljava/lang/String;I)V");',
         'jmethodID setSize = e->GetMethodID(mgrCls, "setOverlayButtonSize", "(Ljava/lang/String;I)V");\n'
         '        jmethodID setEverywhere = e->GetMethodID(mgrCls, "setOverlayShowEverywhere", "(Ljava/lang/String;Z)V");'),
        ('int       x      = int(std::lround(width / 2.0f + (kVanillaHalfSpan + kGapDp - kViewPaddingDp) * d));',
         'int       height = std::min(w, h);\n'
         '                int       x      = int(std::lround(width * kRightFraction - viewPx / 2.0f));\n'
         '                int       y      = std::max(0, height - viewPx - int(std::lround(kGapDp * d)));'),
        ('e->CallVoidMethod(mgr, setPos, jkey, x, 0);',
         'e->CallVoidMethod(mgr, setPos, jkey, x, y);\n'
         '                if (setEverywhere) e->CallVoidMethod(mgr, setEverywhere, jkey, JNI_TRUE);'),
        ('log().info("MOD button placed at its default position ({}, 0), {} px", x, viewPx);',
         'log().info("OptLevi button default at ({}, {}), {} px", x, y, viewPx);'),
        ('ButtonBuilder(kButtonId, "ModMenuBE")', 'ButtonBuilder(kButtonId, "OptLevi")'),
        ('.label("MOD")', '.label("OptLevi")'),
        ('kButtonIconPng, sizeof(kButtonIconPng)', 'kOptLeviIconPng, sizeof(kOptLeviIconPng)'),
        ('ModuleBuilder(kModuleId, "ModMenuBE")', 'ModuleBuilder(kModuleId, "OptLevi")'),
        ('模組選單：已安裝模組、說明與設定。開啟後畫面上會出現 MOD 按鈕。',
         'OptLevi: menu nativo para mods, baseado no ModMenuBE de AFAlimru.'),
    ]:
        replace_once(entry, old, new)

    mod = SRC / "android/src/ModMenuMod.cpp"
    replace_once(mod, 'modmenu::log::info("ModMenuBE loaded from {} ({} mods found)"',
                 'modmenu::log::info("OptLevi menu loaded from {} ({} mods found)"')
    print("Prepared OptLevi 0.1.0 with the supplied icon and a lower-right menu button")

if __name__ == "__main__":
    main()
