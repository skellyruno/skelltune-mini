# SKELLYTUNE MINI — REAL LOCAL JUCE UI SCREENSHOTS 🩻

**Source of truth:** `Source/PluginEditor.cpp` and `Source/PluginEditor.h`. UI is not an image-generation task, and `tools/render.cpp` renders **WAV audio**, not screenshots. Never show a concept image or outdated UI as the current editor.

## Verified local native Quick4 (November 13, 2026)

Opt-in CMake target: `SkellyTuneQuick4` (`SKELLY_BUILD_UI_QUICK4=ON`); code: `tests/ui_quick4.cpp`. This executable constructs the **real** `SkellyTuneRealtimeProcessor` and its live JUCE `createEditor()`. It does not ship with the Windows VST3 and does not modify DSP/automation IDs.

Captured and verified locally on Linux/Xvfb: **4 real PNGs, zero errors, ~1.3 seconds** from an existing executable. Full optional **8 real PNGs, zero errors, ~2.5 seconds**. Initial JUCE compilation is much slower; incremental recompilation is separate from capture time.

Default four states:
1. `main_default_432.png` — original 432×414 Mini window, C Chromatic/A4 440
2. `key_scale_selected_432.png` — G Minor/A4 442, **selected fields**, not an open native dropdown popup
3. `retune_active_720.png` — real 720×690 editor with 0 ms Retune, E Major
4. `custom_notes_432.png` — true custom 12-note mask, 7 ms Retune

Also produces `contact-sheet.png` and machine-readable `manifest.json`. `--full` captures all eight state/size checks. The renderer checks dimensions, nonblank/colour-rich output, control bounds, and real component IDs.

**One colour skin only.** Mini deliberately uses neon green; the fourth scene is a genuine alternate note state. Do not invent an ivory/black theme until actually implemented.

## Build/rebuild *locally*, not via GitHub Actions

Linux, CMake, Ninja, g++, Xvfb and JUCE/SkellCore development dependencies are required. Use the shared **SkellCore Quick4 project runner** with plugin `skelltune-mini` (or manually):

```bash
cmake -S . -B build-ui -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DJUCE_PATH=/path/to/JUCE-8.0.12 \
  -DFETCHCONTENT_SOURCE_DIR_SKELLCORESHARED=/path/to/pinned/SkellCore-repo \
  -DSKELLY_BUILD_PLUGIN=ON -DSKELLY_BUILD_UI_QUICK4=ON
cmake --build build-ui --target SkellyTuneQuick4 --parallel 1
xvfb-run -a -s '-screen 0 1600x1200x24' \
  "$(find build-ui -type f -name SkellyTuneQuick4 -executable | head -n 1)" \
  --output ui-quick4
```

On low-memory Linux containers, the Quick4 development target **omits JUCE's release -O3 config flags**. This does not change the Windows VST3 target's release optimization.

Windows VST3 and GUI startup/host tests are a *separate validation gate*. Passing headless screenshots never certifies FL Studio. GitHub screenshots have been removed from automatic artifacts; one-time offline source bootstrap is now manual-only.

When asked **"current UI"**: check out latest source, locally build renderer, capture these four, display actual main PNG and optionally contact sheet, state exactly which commit was built. If rebuilding is impossible, explicitly disclose the blocker; do not substitute an old screenshot or invented GUI.
