# SKELLY JUCE UI SOURCE-OF-TRUTH — Quick4

**When asked “current UI,” “show UI,” or “screenshot”:** inspect THIS repository's newest actual `Source/PluginEditor` C++ implementation. Build the genuine JUCE editor with the native Linux development UI target and capture **4 screenshots locally**. Do not use ImageGen, a painted mockup, an audio WAV renderer, stale captures, or GitHub Actions screenshot jobs. If local source compilation is unavailable, say what is missing rather than substituting an invented screenshot.

- Target: `SkellyTuneQuick4`; CMake: `-DSKELLY_BUILD_UI_QUICK4=ON`; `--output <folder>` captures four real scenes by default, `--full` intentionally runs the deep visual audit.
- Status: **verified**. "Wired" is not tested; never claim a successful local compile without a real build and 4/4 PNG result.
- Note: one neon theme; use a real alternate note state, not fabricated colour.
- Shared router: `skellyruno/skellcore-vst-template/tools/quick4_projects.py`.
- Shared policy and rollout: `skellyruno/skellcore-vst-template/docs/PLUGIN_QUICK4_ROLLOUT.md`.
- Use original plugin source, pinned JUCE/SkellCore libraries, Xvfb, and a writable local build directory; use `--jobs 1` in low-RAM containers. Record commit SHA, output screenshots and build status.
- UI screenshot verification is **separate** from Windows x64 VST3 compile, DSP/latency/CPU tests, actual FL Studio host validation, and release packaging. Do not remove release validation or infer VST3 success from screenshots.
- Default is **four** frames: main, real controls/preset, real active state, real alternate supported skin/state. A selected ComboBox is not an open menu. Don't invent a new skin.
