# SkellyTune Mini v3.3.1 — FL Studio preset restore hardening

- Bounds saved-state data and rejects incompatible XML before restoring.
- Restores only 16 stable parameter IDs with strict numeric parsing and range validation.
- Preserves existing plugin identity, GUI, automation, true 0 ms Retune Speed and DSP behavior.
- Adds portable preset-safety tests and Windows plus Linux GitHub Actions workflows.
- Documents safe FL Studio crash reproduction and recovery.

**No claim of resolving the reported FL Studio crash until Windows VST3 and affected project are tested.**
