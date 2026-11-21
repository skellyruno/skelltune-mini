# SkellyTune Mini v3.3.2 — repository update

## Source sync
The v3.3.2 META test archive has been synced into this branch for review:
- Adds the isolated \`dsp/MetaTest.hpp\` experimental engine and \`tests/test_v43_meta.cpp\` characterization suite.
- Adds a third REGULAR / TESTTUNE / META algorithm selector in the editor and routes audio to META only in explicitly META-enabled builds.
- Adds optional offline render switches for META A/B testing.
- Documents known detector/smoother limitations and benchmark observations in \`META_TEST_NOTES.md\`.
- Keeps \`SKELLY_META_TEST\` disabled by default and emits a CMake warning when explicitly enabled.
- Includes the validation snapshot in this report.

The repository-only Windows build workflow, installer/rollback code, third-party notices, artifact-access policy helper, and associated tests were preserved from GitHub main. The truncated archive copy of \`CHANGELOG_v331.md\` was deliberately not used.

## Validation from the uploaded source snapshot
- Standard portable DSP suite with META disabled: **43/43 tests passed**.
- Separate META characterization build: **1/1 test passed** with explicit \`SKELLY_META_TEST=ON\`.
- Real-vocal G-major test render: **92.469%** voiced frames within 25 cents; peak **0.893306**; no clipped samples.
- +10% input-level stability check: **1.284%** 20 ms output-envelope mismatch after gain normalization.
- 48 kHz isolated engine benchmark: approximately **1.37% of one core** (not a plugin-wide host measurement).
- Nominal declared DSP delay: 144 samples / 3.0 ms at 48 kHz (not host-measured latency).

## Release / test boundary
META is research-only. Its own documentation reports weaker pitch scores than the standard engine on the three comparison clips, a few milliseconds of uncompensated latency, and no explicit unvoiced handling. Never distribute a build configured with \`SKELLY_META_TEST=ON\`. The standard engine remains the default path.

## CI and remaining host validation
The portable DSP GitHub Actions checks on this branch have passed. The Windows JUCE/VST3 build, actual editor startup, screenshot capture, package verification, and pluginval are still in progress at the time of this report. FL Studio load/save and Fruity Wrapper crash reproduction still require owner-host testing. No Windows VST3 release is being claimed by this PR.