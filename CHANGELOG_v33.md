# SkellyTune v3.3 — Causal onset-memory hard-tune update

## What changed

- Added a strictly causal **32 ms remembered-onset cue** to the maximum hard-tune note-decision input. A consonant/attack can precede the first high-confidence pitch estimate. The original single-sample onset score can expire before the estimator updates.
- The memory **only starts when the engine is not currently processing a trusted voiced note**, and is only forwarded on an accepted pitch-estimator hop with **confidence >= 0.85**. This is designed to avoid aggressive switching during sustained vibrato.
- Continued to use the existing NoteDecision musical constraints and its attack decision; did not add provisional pitch correction or an unconditional octave switch.
- Added `setHardTuneOnsetMemoryEnabled(bool)` as an internal A/B testing switch; the GUI and plugin parameter IDs are unchanged.
- Added counters for remembered-onset detector hops and genuine note changes assisted by the remembered onset, plus a debugging getter for the live onset score.
- Added `test_v33.cpp` regression suite for sample rates 44.1/48/96 kHz, callback sizes 1/257, stereo inversion, and ordinary-preset bit identity when memory is toggled off/on.
- Added level-matched WAV comparisons and the complete same-take commercial pitch-center benchmark. All pitch accuracy figures are observations from the supplied recording, **not proof of sound-quality or commercial parity**.

## What did not change

- Retune Speed minimum remains **0 ms**; this removes correction-parameter smoothing but not acoustic pitch-recognition time.
- The original realtime waveform shifter, pitch estimator, optional PSOLA engine, plugin UI, automation IDs, VST3 identities, and declared 3/40/50 ms dry/attack timing contracts are unchanged.
- No new future-looking buffer or allocation was added to the audio callback.
- Full wet-waveform latency, Windows VST3 compilation and FL Studio host operation remain unverified.

## Rejected experiments

- Letting a remembered onset start during any currently voiced note produced a slightly better single-recording pitch-center score, but increased the risk of treating a vibrato peak or bright harmonic attack as a new note; not shipped.
- Remembering for 40/48/64 ms did not consistently outperform the shorter release setting and risked using stale onset evidence.
- New synthesis and octave heuristics were not accepted without an independent gain in real-vocal accuracy and artifact tests.
