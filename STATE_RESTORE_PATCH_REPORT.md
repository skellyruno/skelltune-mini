# SkellyTune Mini v3.3.1 — guarded saved-state restoration

## Change
Source/PluginProcessor.cpp validates host state size (max 256 KiB), XML root, 16 whitelisted IDs and finite parameter values. It patches only recognized parameters onto a valid APVTS copy. Unknown or corrupted input leaves current state unchanged. dsp/SafePresetValue.hpp defines the schema; tests/test_preset_safety.cpp exercises malformed, legacy and numeric edge cases. JUCE parameter IDs, plugin codes and real-time DSP remain unchanged.

## Validation
Portable DSP tests, including strict preset-schema parsing, can run through the Verify portable DSP workflow. The CMake version is 3.3.1. This change has not yet been verified in Windows FL Studio against the original crash: only GitHub's Windows build and an affected FLP can establish that.
