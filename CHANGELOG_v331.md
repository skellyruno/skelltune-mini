# SkellyTune Mini v3.3.1 — FL Studio preset restore hardening

- Bounds saved-state data and rejects incompatible XML before restoring.
- Restores only 16 stable parameter IDs with strict numeric parsing and range validation.
- Preserves existing plugin identity, GUI, automation, true 0 ms Retune Speed and DSP behavior.
- Adds portable preset-safety tests and Windows plus Linux GitHub Actions workflows.
- Documents safe FL Studio crash reproduction and recovery.

## Windows preview packaging hardening — November 17, 2026

- Stage the VST3 bundle directly as the Actions artifact payload; do not wrap a ZIP inside the artifact ZIP.
- Normalize JUCE-generated trailing commas in `moduleinfo.json`, then validate it with strict JSON parsing and the CMake project version.
- Require one Windows x64 VST3 module, unique component/controller class IDs, PE32+ AMD64 headers, build provenance, SHA-256 manifest and included license notices.
- Pin and hash-check pluginval v2.0.0-beta3 and run strictness level 5 before uploading the preview.
- Fix the VST3 default program label to `Default`; add a real editor-smoke assertion after the Steinberg validator found an empty program name.
- No DSP, editor UI, preset data, VST3 identity, parameter IDs or automation IDs changed.

**No claim of resolving the reported FL Studio crash until Windows VST3 and affected project are tested.**
