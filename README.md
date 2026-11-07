# SkellTune Mini — SkellyTune v3.3.1
JUCE 8 / CMake C++17 VST3 and Standalone project with SkellyTune's real pitch-correction engine and compact dark/cyan GUI (not the original gain template).

The FL Studio saved-state crash mitigation in v3.3.1 rejects malformed, oversized, or unsupported saved parameter values without overwriting existing valid settings. **It has not yet been verified to resolve the exact Fruity Wrapper crash**; see [FL_STUDIO_RECOVERY.md](FL_STUDIO_RECOVERY.md).

## Build and download
On pushes to `main`, **Build Windows VST3** uses GitHub Actions to build three plugin profiles: Realtime (3 ms nominal dry/transient delay), Studio Fast (40 ms) and Studio (50 ms). From the workflow's Artifacts section download `SkellyTune-Windows-x64`.

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
