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

## November 10, 2026 — Mini: fewer controls, quieter wet audio
- The **only visible controls** are Retune Speed, the editable **A4 reference**, **Key**, **Scale**, and the existing interactive 12-note piano. The old Advanced window, extra tuning sliders, Mix, Amount, Bypass, and crowded status row are no longer in Mini's editor.
- Click a piano note to enable/disable it. **Double-click a note to restore Key/Scale notes**. "Custom Notes" is indicated above the piano. The window remains 432 × 414 by default and resizes proportionally to 1008 × 966.
- The editor always uses the shared SkellCore neon-green skull branding. The old skin state is still accepted for saved-session migration, but the skull is now a static brand mark, not another button.
- **All legacy automatable parameter IDs remain in the processor**, including hidden Mix/Amount/Bypass and synthesis options, so older host sessions don't lose their saved settings. New instances retain the existing default full correction/full wet. A project with intentionally overridden legacy automation will continue using those values.
- The former advanced UI for experimental PSOLA and manual SkellWorld/SkellKey opt-in is removed from Mini. Their backing processing/state code was deliberately not deleted to avoid breaking existing projects, but new users cannot enable World follow from this minimal editor.
- The wet **reconstruction** (not input, delayed dry, or sample-accurate transient path) now passes through a causal, stereo-symmetric, two-stage 12 Hz DC blocker. Measured response: ~-16.6 dB at 5 Hz, -7.7 dB at 10 Hz, -2.7 dB at 20 Hz, and -0.29 dB at 65 Hz. It is not a replacement for diagnosis of octave tracking errors or microphone/room rumble.
- CI checks 39 portable DSP regressions, long-silence denormal safety, raw-vocal peak/RMS integrity, loud/quiet tone gain equivalence, real-singer gain-scaled A/B, and Windows JUCE rendering / A4-key-scale-piano attachments at three sizes. The Windows job uploads PNG UI previews and a VST3 artifact (7-day retention).
- **Limits:** Real FL Studio loading/closing, microphone loudness behavior, subjective artifact elimination, exact wet-waveform delay, and listening tests on multiple singers still need real-host evaluation. Zero ms Retune Speed is not zero host latency.

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
