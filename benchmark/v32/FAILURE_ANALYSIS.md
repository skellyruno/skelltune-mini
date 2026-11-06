# Frame-level failure audit

From the supplied 30.57-second raw vocal, XPitch Realtime and Auto-Tune Pro both score within ±25 cents on many frames where v3.2 does not. The ten longest such stretches include:

- 10.12–10.31 s: approximately 9 analysis frames; the input contains a voiced onset mixed with less-periodic syllable material; the tracker takes time to reacquire.
- 19.48–19.62 s: approximately 7 frames; the independent measured output frequency falls to ~79 Hz while the raw recording's estimate is around ~117 Hz. This requires a real-vocal octave/periodicity investigation, not a blind octave multiplier.
- 7.13–7.24 s: approximately 6 frames; intermittent target-note pending states let uncorrected pitch reappear.
- 23.99–24.10 s: approximately 6 frames; the input glides through several candidate notes while the synthesis ratio and note selector update.

See `frame_level_errors_v32.csv` and `frame_level_errors_v31.csv`. The gaps do not prove that the commercial products made musically correct note selections, because original authorized note annotations and commercial preset data weren't provided. The reported pitch-error evaluation is also sensitive to independent pitch-estimator errors on sibilants.

## Immediate next research requirements

1. Obtain independently annotated note targets and beat-aligned audio for multiple singers; separate detection, note-choice and shifter errors.
2. Test a causal multi-hypothesis low-F0 estimator specifically on the ~19.5 s passage without sacrificing the verified weak-fundamental/octave regressions.
3. Improve target transitions on real connected syllables using a hysteretic state model; preserve vibrato-onset controls and do not abruptly substitute delayed dry audio on confirmed voiced material.
4. Record wet-output pitch, pitch-mark phase, voiced envelope and formant drift at every update; compare all modes with level-matched commercial renders.
5. Confirm Windows VST3 loading, callback timing, CPU worst-case and DAW PDC before advertising commercial-grade live latency.
