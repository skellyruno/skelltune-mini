# SkellTune Mini — SkellyTune v3.3.1
JUCE 8 / CMake C++17 VST3 project with SkellyTune's real pitch-correction engine and compact, resizable dark/neon-green GUI (not the original gain template).

The FL Studio saved-state crash mitigation in v3.3.1 rejects malformed, oversized, or unsupported saved parameter values without overwriting existing valid settings. **It has not yet been verified to resolve the exact Fruity Wrapper crash**; see [FL_STUDIO_RECOVERY.md](FL_STUDIO_RECOVERY.md).

## November 7, 2026 — compact UI / low-overhead DSP refinements
- The editor opens at **432 × 414**, with aspect-locked resizing up to **1008 × 966**. Selector widths, scalable text buttons, retune hierarchy, note-state labels, and pitch confidence have been refined.
- Pitch readout and active note indicators use the automated **A4 tuning reference** rather than assuming 440 Hz.
- In the detector, the 33-tap FIR circular indexing no longer uses modulo in its hot loop. Antiphase stereo selection uses an equivalent squared-correlation check rather than a per-sample square root.
- An unprepared 64-bit host audio callback safely returns without an infinite loop.
- New `dsp_v34_pitch_tracker_performance_regression` and extended `gui_startup_smoke.cpp` cover regression cases. The native JUCE/Windows CI build **has not been verified** as part of this update.
- Original VST3 IDs, parameter IDs and host latency contract remain unchanged.

## DSP v3.5 — high-confidence semitone lock and audio-thread efficiency
- **Stable adjacent-note acquisition:** in 0 ms/full-wet aggressive tuning, a new semitone within 30 cents of its center may commit after at least 35 ms of consistently high-confidence (>= 0.92) pitch evidence. A candidate must remain inside a 16-cent observed excursion window. Older broad vibrato excursions still use conservative confirmation.
- **Centered-note vibrato safety:** the existing 15 ms near-center lock also requires the candidate's accumulated pitch evidence to stay within that 16-cent window, reducing false semitone switches near wide vibrato extrema.
- **Hot-loop math:** pitch ratios now use `std::exp2(cents/1200.0)` instead of `std::pow(2.0,cents/1200.0)`; the transient detector now compares squared threshold magnitudes rather than taking a square root for every sample.
- **Regression coverage:** `dsp_v35_stable_note_lock` checks fast/normal note decisions, 70–100-cent vibrato at 4–6 Hz, onset/phrase recovery, note-mask edits, and pitch-ratio / transient-threshold math.
- **Local validation:** the extracted NoteDecision implementation passed C++17 GCC -Werror tests at 44.1/48/96 kHz; AddressSanitizer+UndefinedBehaviorSanitizer passed. An isolated CMake/CTest run passed 1/1. An isolated pitch-ratio microbenchmark was ~3x faster for that math operation, **not** the full plugin. A deterministic 2,000,000-sample transient stimulus yielded identical threshold decisions.
- **Still unverified:** full in-repository regression suite, Windows VST3 build, FL Studio hosts, real-vocal A/B benchmarks, true wet-path latency, and plugin-wide CPU reduction. No manufacturer/plugin IDs, parameter IDs, or host delay contract were changed.

## Build and download
On pushes to `main`, **Build SkellyTune Realtime VST3** runs on GitHub Actions and builds only the **SkellyTune Realtime** Windows VST3 (3 ms nominal dry/transient delay). Download the `SkellyTune-Realtime-Windows-VST3` artifact from the successful workflow. No Studio, Studio Fast, or Standalone files are produced.

To run portable C++ DSP tests locally (without JUCE):
```sh
cmake -S . -B build-dsp -DSKELLY_BUILD_PLUGIN=OFF -DSKELLY_BUILD_DSP_TESTS=ON
cmake --build build-dsp --parallel 4
ctest --test-dir build-dsp --output-on-failure
```

The DSP engine includes 0 ms *Retune Speed* (no correction smoothing), which does not mean zero detection or audio transport latency. Actual fixed wet delay and FL Studio compatibility are not fully validated.

### Compatibility
Do not change manufacturer/plugin codes or parameter IDs without a migration plan; existing FL Studio projects rely on them. Back up projects before changing installed VST3 versions. Third-party license notices are in `third_party`.

[Patch notes](CHANGELOG_v331.md) · [FL Studio recovery](FL_STUDIO_RECOVERY.md) · [State validation](STATE_RESTORE_PATCH_REPORT.md)

## SkellWorld / SkellKey link (opt-in preview)

SkellyTune can follow SkellKey's confident major/minor analysis without any MIDI routing. First start the local SkellWorld hub (from the SkellCore template checkout), join both plugins, and in SkellyTune's **MORE** panel select **JOIN WORLD** then **FOLLOW SKELLKEY**. In SkellKey click **JOIN WORLD** in the footer. SkellKey sends its detected key when confidence is high and the result is stable; updates run on background/UI threads. No audio or plugin state is sent over the network.

**Follow is off by default, never persisted or enabled at project load.** While active, SkellyTune uses an atomic, temporary effective-scale override. Its saved key, scale, note mask, latency and automation remain intact. On disconnect or leave it reverts to the selected manual key/scale. Custom note masks have priority; reset those before following. The older SkellKey MIDI scale transmission remains a separate option.

Click the skull-key logo in SkellyTune to cycle SkellCore's four shared skins (Ossuary, Neon, Ivory, Obsidian); the existing default Neon skin is preserved. Skin changes affect UI only and persist as an optional safe saved-state property. SkellKey uses the same mark and colors.

This is a developer pilot. It requires both plugins on matching shared SkellCore versions and the opt-in local hub; physical FL Studio sessions and UI click/reopen tests are still required for release.
