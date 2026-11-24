# SkellyTune Playground — research only

Purpose: one test host for every distinct Skelly pitch correction engine. Keep Mini Round 2D, Snap, OG and MetaTune Inspired standalone products intact. Never silently substitute a different engine. Stable mode numbers 0–6.

| ID | Engine | Exact source baseline | Native profile | Graph note editing |
|---|---|---|---|---|
| 0 | REGULAR | Mini 397ef8566e7f78fad34415fd8868fbc17ea0c617 | 1.5 ms | yes |
| 1 | TESTTUNE | same Mini baseline | 1.5 ms | yes |
| 2 | META | same Mini experimental MetaTest | 1.5 ms | yes |
| 3 | CLASSIC | skell-snap 7c5ff11b68a8e0767a6f64378a037b5adf21d50a | native 2.5 ms, 80 Hz floor | monitor only |
| 4 | CRYSTAL | same pinned Snap source, mode 1 | native 2.5 ms, 80 Hz floor | monitor only |
| 5 | OG SKELLYTUNE | skell-autotune-vst 99e419a164e736e50e9cc4b1e0d751338f6f5019 | Standard, 80 Hz, formant preservation | monitor only |
| 6 | METATUNE 2.5 | MetaTuneInspiredvst 73d759307ce9536e522b2cf78dc1da943afc8f3d | actual SkellyFusion/Lyra automatic 2.5 LowVoice path | monitor only |

Source baselines are pinned. Import DSP only, no third-party plugin resources. Snap header is recovered from checksum-verified overlay. Optional full source adapter includes AGPL-3.0 MetaTune Inspired code at build time; **no redistribution of combined VST3 until license and corresponding-source obligations have been reviewed**. Do not change production licensing or package source from unrelated repositories. Local full research build is explicitly opt-in. Five-engine preview has a distinct test VST3 code (SkP5), seven-engine preview SkP7; existing Mini/Meta Lab IDs untouched.

Common controls: absolute note mask, A4 (400–480Hz), retune, amount, wet mix. Default profiles differ by design; OG's normalized nonlinear native retune mapping is approximated from its documented time constant (0–100ms supported, saturates above 100ms). Do not infer equal response just because the knob value matches. Native tuning/formant/voice floors remain unchanged for baseline comparisons. Pad outputs to largest **reported native dry delay**, do not pretend this proves equal wet source age or native DAW latency. Only one engine runs, fade through dry and prime new history when switching. First 40ms+common delay kept dry during acquisition; compare clips with pre-roll. No allocation in adapter process, except source-engine regressions must be separately checked. Graph estimates, not measured output F0; imported engine graph edits are disabled rather than approximated using a pitch-class mask. Saved edits are retained when switching.

## Build
```
python tools/recover_snap_core.py <pinned-snap-checkout> <output>/SnapCore.h
cmake -S . -B build -DSKELLY_META_TEST=ON -DSKELLY_BUILD_PITCH_PLAYGROUND=ON -DSKELLY_SNAP_HEADER=<output>/SnapCore.h -DSKELLY_BUILD_PLUGIN=OFF -DSKELLY_BUILD_DSP_TESTS=ON
cmake --build build --target playground_bank_test playground_render
ctest --test-dir build -R playground_bank_test --output-on-failure
```
With JUCE enabled, add `-DSKELLY_BUILD_PLUGIN=ON` and build `SkellyTunePlayground_VST3`, `SkellyPlaygroundSmoke`. For the complete seven-engine research build add `-DSKELLY_PLAYGROUND_FULL=ON -DSKELLY_OG_SOURCE=<pinned-OG> -DSKELLY_META_SOURCE=<pinned-MetaTune>`; build `SkellyPlaygroundFullBankTest` and `SkellyPlaygroundFullRender` as well. All imports must pass exact-source compilation/tests before claiming full support. `SKELLY_PLAYGROUND_MODE=0..6` in the appropriate offline runner selects the native engine; invalid/unavailable IDs fail rather than silently default. Never publish unverified or unlicensed full artifacts.

Next test gates: exact build, actual Windows GUI+pluginval, real vocal blind A/B at 0/10/25/100ms, low-note, transition/attack, vibrato, partial amount, silence/hum, stereo and CPU/latency. Preserve every contender until user listening votes; later promote proven DSP improvements to SkellCore without copying restricted code or breaking old projects.

## Integration checkpoint — sample-exact switching

The first full Windows attempt failed because the OG SIMD renderer uses explicit templated lambdas and the Playground was compiling under C++17. Full targets now require C++20, as in OG itself; portable five-engine tests remain C++17. Added integer-sample fade progression and split rendering precisely at the engine handoff boundary. The old five-engine test fixture produces 0.0560701 full-scale maximum difference for 1 vs 127 frames at 44.1 kHz; updated regression produces exactly zero difference at 44.1/48/96 kHz. All 15 pitch + dry PDC cases and ASan/UBSan passed locally. Full test now visits all seven modes rather than stopping at mode 5. GUI smoke must find the real graph toggle, expand/collapse, verify editing buttons and saved note persistence; these Windows checks remain unverified until CI completes. Repository display-name target: SkellyTune Playground, desired slug skellytune-playground. Preserve the existing repository and draft history during any owner-side rename.
