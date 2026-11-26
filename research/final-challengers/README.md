# Remaining challenger research

Continue only TESTTUNE, META and METATUNE 2.5; CLASSIC and CRYSTAL are frozen references. The rejected mixed challenger patch was deleted at the owner's request. No backup was made.

Prior TESTTUNE cycle synthesis experiment at 10 ms improved median pitch error from 12.007 to 6.314 cents on one singer, but worsened spectral distance from 1.681 to 2.006 dB and p10 level from -0.813 to -1.446 dB, with a regression at 0 ms. Do not promote the cycle path as default based on that result. Cents-nearest note selection was mixed. Repeat candidate testing with independent performances and compare aligned vocal spectrum, envelope, transitions, and stereo preservation, not just cents.

See docs/PITCH_PLAYGROUND.md for the current validation contract.

## Adaptive vowel candidate

Second candidate normalizes relative band energy, smooths entry/exit, protects low fundamentals, full-wet/attack boundaries and stereo, and bounds residual headroom. Both 0.35 and 0.65 strengths are offline-only, compared against the prior envelope candidate and unchanged base at 0/10/25/100 ms and 2.5 ms transport. CI independently scores source-voiced pitch coverage, cents to the requested scale, dry-envelope distance, level deviations, and overload; protected Classic/Crystal WAVs must hash-identically match. Metrics are proxies, not listening approval; the source recording is not held out. No automatic default promotion.

## Targeted third round

Portable run 45838572750 (same 2.5ms transport, 10ms retune): TESTTUNE 10.824 cents / 0.685 dB envelope MAE, META 6.301 / 1.152, Classic 7.698 / 0.456, Crystal 4.210 / 0.577. META old envelope candidate clipped 40–64 samples per render at the four tested speeds (46 at 0ms); adaptive candidates clipped none but did not show compelling overall improvement. Reject default promotion. Fix old candidate headroom and isolate new motion calibration and faster stereo-linked wet dynamics at 0/10/25/100ms; combination is also scored. Original retune endpoints and reference DSP protected. No additional PDC. Candidate internal speed calibration is experimental, not a verified physical timing match. No public release or default change.

## Owner requested audition promotion

Portable run 45839949789: TESTTUNE combined 10ms 10.824 -> 8.280 cents, envelope 0.685 -> 0.690 dB, median level 0.144 -> 0.111 dB; p10 level -0.174 -> -0.297 dB. META 10ms 6.301 -> 5.112 cents, level 0.205 -> 0.173 dB, p10 -0.570 -> -0.425 dB; 25ms 12.685 -> 11.072 cents. All scored new variants had zero overload. Promote TESTTUNE and META dynamics and validated motion into the real 2.5ms plugin path. TESTTUNE 25ms motion stays old due to 0.083dB envelope regression and coverage loss. Scope to tested 2.5ms profile and no changes to MetaTune 2.5 pending Windows scores. CI hash-compares promoted audio with exact corresponding trial, and all reference outputs. The measured gain is single-take evidence, not proof of superiority or release readiness.
