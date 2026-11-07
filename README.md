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
