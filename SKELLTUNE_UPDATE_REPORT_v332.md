# SkellyTune Mini v3.3.2 — update verification snapshot

This report records validation performed against the uploaded `skelltune-mini-v332-META-TEST-ONLY.zip` source snapshot. It is not a claim that the full uploaded archive has been imported into this repository branch.

## Release-safety change in this branch

- Added the opt-in `SKELLY_META_TEST` CMake option with a default of `OFF`.
- When explicitly enabled, CMake emits a warning that META is test-only and must not be released.
- No existing plugin IDs, parameter IDs, preset/state formats, automation IDs, or latency declarations were intentionally changed by this guard.

## Validation recorded for the uploaded source snapshot

- Portable DSP suite: **43/43 CTest tests passed** with `SKELLY_META_TEST=OFF`.
- META characterization test: **1/1 passed** when built separately with explicit `SKELLY_META_TEST=ON`.
- Real-vocal G-major comparison: **92.469%** of voiced frames within 25 cents; peak **0.893306**; no samples above full scale.
- +10% input-level stability check: **1.284%** 20 ms envelope mismatch after gain normalization.
- Isolated 48 kHz engine benchmark: **0.136588 CPU seconds** over the 10-second simulation (about **1.37% of one core**; not a plugin-wide host measurement).
- Declared nominal DSP delay: **144 samples / 3.0 ms at 48 kHz**; not a host-measured plugin latency result.

## Still unverified

- JUCE 8.0.12 and pinned SkellCore could not be fetched in the validation environment because `github.com` DNS resolution failed. Quick4 was not built and no UI captures are claimed.
- No Windows x64 VST3 build, FL Studio load/save test, Fruity Wrapper crash reproduction, or owner-PC host acceptance was completed.
- The uploaded source archive contains additional source/test changes not included in this small repository guard commit. Those need a separate source-sync review before merging.

## META rule

Normal builds must leave `SKELLY_META_TEST` unset. Only isolated A/B or characterization builds may set `-DSKELLY_META_TEST=ON`. Never distribute a META-enabled build.
