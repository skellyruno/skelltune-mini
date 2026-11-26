# SkellyTune Playground — active five-engine lineup

Active full build: TESTTUNE, META, CLASSIC, CRYSTAL, METATUNE 2.5. Portable build contains the first four. Regular and OG SkellyTune are retired from the Playground, including their comparison routes, rejected experiment patch and OG build dependency. Do not restore them. Shared Mini DSP remains because TESTTUNE and separate Mini products use it.

CLASSIC and CRYSTAL are frozen listening references; do not modify their pinned SnapCore or processing policy. The old host choice range is intentionally unchanged: stable slots 1, 2, 3, 4, 6 select the five engines. Retired slot 0 maps to TESTTUNE; retired slot 5 maps to CLASSIC. The actual editor exposes only five choices. Old saved sessions canonicalize aliases. Offline renderer rejects the retired slots; use 1, 2, 3, 4, 6. Portable build accepts 1–4. No history rewrite or backup branch is created. Old profile 0 is a legacy matched offline comparison; removing unused engines can change its common padding. Interactive profiles 1–5 retain existing engine-specific latency targets.

## Reproducibility and release gates

- Snap source pin: skellyruno/skell-snap at 8a4876259ebf30dd100204be1cf9beae2c3b5f03; recover with tools/recover_snap_core.py. Required SnapCore SHA256: ca46212c500efe397968a2ef9869050f26b0ea015b5f462fd8ac99ffb6b3672b.
- MetaTune 2.5 source pin: skellyruno/MetaTuneInspiredvst at 73d759307ce9536e522b2cf78dc1da943afc8f3d. Full combined research binary and corresponding-source package must retain all license notices and AGPL-3.0 terms. No production release from this draft.
- Portable test targets: playground_bank_test, playground_mix_test, playground_attack_test, playground_recovery_test, playground_formant_test. Full Windows targets and owner research artifact are specified in .github/workflows/pitch-playground.yml.
- Windows test includes all five native engines, actual editor smoke, pluginval strictness 5, VST3 identity, real-vocal renders, hash checks for protected reference pairs, and complete corresponding source. FL Studio validation and real Quick4 UI inspection are separate.
- Retune: 0 ms fastest; 100 ms slower. Latency selector requests 0, 1, 2.5, 5 or 10 ms; actual engine minimum and host delay are reported. Formant audition is opt-in and adds no fixed audio transport or host PDC. Its causal envelope warp approximates formant movement; listening verification is required. Graph editing is supported by TESTTUNE and META; the other three are monitor-only.

## Latest verified portable result (retirement commit 9528d2d)

GitHub run 45821207262, portable-four job 139913980132: five CTest targets passed; sixteen actual singer renders (four engines, 0/10/25/100 ms) completed with no samples above full scale. Maximum peaks: TESTTUNE .896436, META .955656, CLASSIC .877820, CRYSTAL .878492. Callback p99 maxima on this runner: .051656, .037821, .079488, .098392 ms respectively. These are runner-specific measurements, not FL Studio CPU or listening scores. Full Windows validation was still in progress at the time of this record.

## Next improvement priorities

New offline-only candidate uses aligned dry spectral envelopes to correct the three challengers’ wet vowel balance without adding latency; frozen references are excluded. Full CI renders baseline/candidate at four retune speeds. No promotion without measured and listening results.

1. TESTTUNE: reduce note boundary chatter and preserve natural vowel spectrum; compare current boundary-hold behavior with independent vocal performances.
2. META: refine hard-tune onset response and wet level stability, targeting sustained spectral fidelity rather than only pitch accuracy.
3. METATUNE 2.5: verify full native recording path, consonant recovery and stereo/partial amount behavior. Measure real host latency and CPU.
4. Keep Classic and Crystal unchanged; reject contenders that only improve pitch statistics while worsening vocal envelope, transients or listening preference.

Historical results are in Git history, not in the active source tree. Do not reintroduce the retired engines.

## Measured listening promotion

The native editor/processor automatically enables the 2.5ms listening policy. TESTTUNE and META use the new wet dynamics and the measured retune calibration. TESTTUNE motion returns to baseline at 25ms because the earlier trial lost envelope accuracy and coverage. At other profiles, and for MetaTune 2.5, the prior defaults remain until matching results are available. Classic/Crystal stay bit-identical. No new parameter or user experiment selection. Render with SKELLY_PLAYGROUND_LISTENING_PROMOTION=1 to match the real plugin. This is a listening build, not a production release.

MetaTune 2.5's measured 25ms combined improvement is now enabled in the 2.5ms native listening policy, tapered back to baseline outside 15–35ms. 10ms behavior remains baseline. Native synthesis experiments are still compared offline before promotion.
