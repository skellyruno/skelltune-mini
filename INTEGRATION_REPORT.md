# Template integration verification

- **Source baseline:** SkellyTune v3.3 Onset-Aware Max Lock, the latest complete source bundle available to this conversation. A completed v3.4 source archive was not available for inspection.
- **Template:** `skell-vst-template-main(1).zip`, preserving its JUCE 8 / CMake / Windows GitHub Actions development workflow.
- **Imported:** original SkellyTune JUCE processor/editor, DSP headers, full regression source, benchmarking and WAV references, license/third-party notices, three independent VST3 profiles.
- **Updated:** top-level CMake to default to the Realtime plugin and optionally add Studio/Studio Fast; Windows CI builds all three; separate Linux CI compiles/tests portable DSP.
- **Preserved:** VST3 plugin codes `SkRT`, `SkST`, `SkSF`, manufacturer `Skly`, and original v3.3 parameter IDs.
- **Local configure:** portable DSP CMake configured successfully with JUCE disabled.
- **Local compiled tests:** core regression, smoke, and v28/v31/v32/v33 selected tests passed.
- **Not verified:** JUCE VST3 / Standalone compilation, Windows GitHub Actions workflow execution, FL Studio hosting or wet audio transport latency. JUCE fetch is unavailable locally because this runtime cannot resolve github.com.
- **Full test graph:** originally 32 suites; the optional full portable build was interrupted by the environment before all targets finished. Existing archived v3.3 test report records its prior validation; that is not a test of this Windows plugin build.

Use `GITHUB_SETUP.md` and `README.md` to upload and build the project.
