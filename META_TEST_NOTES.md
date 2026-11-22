# META (TEST ONLY) - notes

**This build contains an experimental engine. Do not release it.** The release-safe package is \`skelltune-mini-v332-patched.zip\`
(no Meta code at all). The default CMake configuration excludes META. Build the experimental A/B variant explicitly with \`-DSKELLY_META_TEST=ON\`; CMake prints a warning when it is enabled.

## What META is
A port of your own \`metatune.cpp\` reconstruction (\`dsp/MetaTest.hpp\`) so it can be A/B tested beside the standard algorithm. It keeps the
pipeline: pre-filter -> period detector -> in-scale period table -> log-domain snap -> smoother -> two-tap grain resampler (a delay-line
pitch shifter that jumps by one period, with a cosine crossfade). It is wired into the editor as the third segment of the algorithm
switch (REGULAR / TESTTUNE / META) and into the processor behind \`#if SKELLY_META_TEST\`. The audio is processed in place; the INPUT/TARGET
readouts and glow work in META too.

What I dropped from the file: the binary-layout scaffolding (object offsets, padded structs, \`static_assert\` sizes, the 49 hashed
parameter IDs, builder memset/memcpy). It did not compile as pasted (\`O_FLAG0/O_FLAG1\` undefined, struct sizes inconsistent) and is not
needed for audio. Per-note tone offsets are omitted (always 0).

## Two things in the reconstruction do not work as written (measured, with a corrected test harness)
1. **Its detector finds (almost) no pitch.** On clean sines and voiced tones from 110 to 440 Hz it returned a period in at most 2 of 562
   hops, and a wrong one when it did. (My first probe of this used a harness that fed the engine the same 128 samples repeatedly; I caught
   that, fixed it and redid every measurement. The conclusion held, the earlier numbers did not.)
2. **The smoother is fed the snapped target period**, so the resample ratio \`period/target\` is ~1 and the pitch is never corrected:
   +30 cents in -> +30 cents out.
Both are selectable so you can see it yourself; the META switch uses the working options:
   detector: the engine's causal pitch tracker instead of the file's detector (\`setFileDetector(true)\` restores the file's).
   smoother feed: the DETECTED period instead of the snapped target (\`setSmoothDetected(false)\` restores the file's). This one is an
   assumption about what the original did; it is the only wiring I found that makes the rest of the pipeline correct.
Render-tool switches (offline A/B): \`SKELLY_META=1\`, \`SKELLY_META_FILE_DETECTOR=1\`, \`SKELLY_META_AS_WRITTEN=1\`, \`SKELLY_META_SPEED=<x>\`.

## Results on your three clips (same scorer as the other numbers; % of voiced frames within 25 cents of a scale note)
| mode | G major | E minor | B major | frames on a different note than the commercial consensus |
|---|---|---|---|---|
| regular | 72.2 | 75.7 | 54.2 | 7.7 / 9.1 / 5.1% |
| testtune | 69.4 | 72.5 | 54.6 | 0.5 / 0.7 / 0.5% |
| **meta (working)** | **70.2** | **72.4** | **51.9** | 0.3 / 1.3 / 0.9% |
| meta, as written | 34.3 | 32.5 | 25.7 | (= untuned) |
| meta, file's detector | 33.8 | 32.5 | 26.6 | (= untuned) |
| Auto-Tune Pro / XPitch / Waves | 73.2 / 72.8 / 70.2 | 77.7 / 77.2 / 76.2 | 56.3 / 56.9 / 56.9 | |
With a working detector and the detected-period smoother, your table + log snap + smoother + grain resampler is a viable tuner: within
~2-3 points of the standard algorithm on two clips, and it picks the same notes as the commercial plugins (stateless snapping).
CPU: 0.7% of one core (about half the standard engine).

## Behaviour to expect / limits (it is a test build)
- Latency is NOT compensated: META's inherent delay (base >= 120 samples plus the grain read-head) differs from the reported latency, so
  expect a few ms of offset when A/B-ing in a host.
- When nothing is detected the last target period is held and the smoother drifts the ratio back to 1; there is no explicit unvoiced
  handling, so consonants can be shifted briefly. That is the reconstruction's design, left as is.
- The smoother "speed" uses the reconstruction's own unit; the plugin maps retune time as speedMs / 8 (arbitrary, for testing).
- The \`meta\` parameter is not persisted in saved sessions (not on the saved-state allow-list), so it resets to off on reload.
- Tests: \`tests/test_v43_meta.cpp\` (only built with the option): tuning to the nearest note within 0.6 cents, scale mask respected, bit-exact
  bypass, finite output on silence/full-scale, plus two clearly labelled CHARACTERIZATION checks that document the as-written behaviour
  (delete or invert them if you fix the reconstruction).
- Provenance: your reconstruction is the source for the logic; nothing here was taken from the original binary. Keep this build out of
  anything you distribute.

## Knob correction and META Lab side-by-side test (November 22, 2026)

The retune dial now shows the **actual** \`speed\` parameter in milliseconds.
\`0 ms\` is the fastest / hardest correction and \`200 ms\` is the slowest;
the progress arc and marker follow JUCE's skewed parameter mapping.
It no longer displays an invented 0–100 strength value (e.g. 56 when the
saved value was 25 ms). This does **not** change the automated \`speed\`
parameter ID, range, default, stored values, or the underlying engine.

**Normal Mini VST3 remains unchanged in identity and has no META option.**
With \`SKELLY_META_TEST=ON\`, the experimental build becomes a separately
identified **SkellyTune META Lab** VST3 (\`SkML\`,
\`com.skelly.realtimetune.metalab\`) rather than replacing Mini's normal
\`SkRT\` VST3. The Lab editor offers REGULAR / TESTTUNE / META; all three use
the same visible speed, key, scale, A4 and note controls.

From an x64 Visual Studio development shell with CMake and JUCE dependencies:

\`\`\`powershell
cmake -S . -B build-meta -A x64 -DSKELLY_META_TEST=ON -DSKELLY_BUILD_GUI_SMOKE=ON
cmake --build build-meta --config Release --target SkellyTuneMetaLab_VST3 SkellyTuneGuiSmoke --parallel 2
# The testing-only VST3 lives under:
# build-meta/SkellyTuneMetaLab_artefacts/Release/VST3/SkellyTune META Lab.vst3
\`\`\`

For the portable META characterization suite (no plugin dependencies):

\`\`\`bash
cmake -S . -B build-meta-dsp -DSKELLY_BUILD_PLUGIN=OFF -DSKELLY_BUILD_DSP_TESTS=ON -DSKELLY_META_TEST=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-meta-dsp --parallel 4
ctest --test-dir build-meta-dsp --output-on-failure
\`\`\`

**TEST ONLY, never distribute the Lab binary.** Its META resynthesis latency
is not fully PDC-compensated, unvoiced consonants may be shifted, and the
reconstruction has known weaker measured pitch scores than REGULAR.
Avoid using Lab instances in production FL Studio projects: their plugin ID is
deliberately different, and switching back to normal Mini will not replace a Lab
instance automatically. Back up test projects. The META state is not intended
to transfer to release Mini. Real-vocal A/B listening and host round-trip testing
remain required. GitHub CI may verify Lab compilation and screenshots, but
it intentionally does not upload the installable experimental VST3.


## Reverse Auto-Tune-style 100 → 0 dial correction (November 22, 2026)

The **large dial number is now 100 at slowest, 0 at fastest hard tune**.
The rotary control itself is physically reversed relative to the engine's
millisecond parameter: drag up / clockwise increases tuning intensity and
decreases the displayed speed number toward 0. The ring fills clockwise as
correction becomes harder. The small readout underneath shows real retune
milliseconds, to avoid confusing the 0..100 user-facing scale with time.

The saved and automatable JUCE `speed` parameter is unchanged at 0..200 ms
(default 25 ms, same ID and version). The GUI mapping is:
`clockwise_position = 100 - host_speed_ms/2`; large display is
`100 - clockwise_position`; only the editor changes orientation.
Consequently 0 on the dial = 0 ms (hardest), 50 = 100 ms, and
100 = 200 ms (slowest). Old FL Studio projects and automation lanes still
use their actual stored milliseconds, not the visible 0..100 markings.
The thumb position and host automation stay linked through JUCE
`ParameterAttachment`, with correct drag gestures and no audio-thread UI.

The sound engine and META Lab's separate test-only identity remain unchanged.
Verification includes a real Windows JUCE editor smoke that checks 0 / 50 / 100
dial positions, the old 25ms default, 7ms adjustment, and host automation
feeding back to the dial. A/B listening/FL Studio project round-trip and a
separate real UI review are still release gates.

## November 22 retune correction / owner META testing

The previous 100-to-0 dial incorrectly mapped 100 to **200ms**. Corrected:
100 on the dial **is 100ms**; 50 is 50ms; 0 is 0ms (fastest hard tune).
Clockwise/upward is faster; the face number decreases. Real plugin APVTS
`speed` ID, version, default and 0..200ms storage remain unchanged for
older DAW projects. If a project restores 175ms, Mini displays `100+`
with `175ms / LEGACY` beneath until the user touches the knob. No old
automation or preset value is rewritten on load.

The actual META selector (REGULAR/TESTTUNE/META) lives in the opt-in
`SkellyTune META Lab.vst3`, **not** normal Mini. This uses a separate
plugin identity and cannot overwrite a normal Mini installation.
The owner explicitly requested to test META. For the owned PR #17 branch
only, the experimental Windows CI exposes an explicitly watermarked,
one-day-retention, test-only artifact (no release, no installer); the user
should extract it to their personal VST3 test directory, rescan in FL Studio,
then select META in the upper-right algorithm selector. Do not commit test
projects or ship this as a product. META latency/PDC and audio quality
still require separate verification.

## November 22 — Three-algorithm real-code comparison + responsive piano

A new deterministic `three_mode_ab` *offline* test renders identical eight-second
synthetic harmonic-voice, vibrato, glide, silence, and unvoiced-noise patterns
through REGULAR, TESTTUNE, and experimental META at **0/10/25ms**. Windows
META DSP CI uploads nine actual render WAVs, dry input, measured pitch/peak/
RMS/step/CPU metrics and a small HTML listening index. These do **not** replace
an A/B with a real recorded human vocal. CPU timing on GitHub Actions is
machine-specific, and these metrics are not a perceptual score.

REGULAR and TESTTUNE now reset *only* their fixed-size note-selection history
when the selected algorithm changes, avoiding pending stale note decisions
from the previous algorithm. TESTTUNE snaps only on sufficiently reliable
periodic estimates (confidence >= .72); it retains its no-hysteresis
musical-boundary behavior.

META is still research-only. It now seeds its smoother with the **first
detected fundamental** instead of a fixed arbitrary startup pitch, rearms
after a genuine 90ms unvoiced interval and sanitizes nonfinite/invalid
test parameters. It still has unverified variable source age/latency/PDC.

The 12 authentic JUCE note pads now give independently smoothed **white
INPUT pitch** and **green TARGET** feedback from processor atomic meters,
on the editor message thread. Active-note frames are captured in the
Windows GUI smoke for all three modes, with programmatic proof both
live-note feedback channels are nonzero. Click/edit/double-click behavior,
key masks, original UI identity and saved parameter IDs remain unchanged.

### Existing real-singer recording A/B

The opt-in META DSP workflow now runs the repository's tracked real-singer
benchmark `benchmark/v33/raw_levelmatched_20-28.wav` at G major, A4=440
and 100% correction/wet. It renders REGULAR, TESTTUNE and META separately
at **0, 10 and 25ms**, all in 64-sample offline blocks, with identical
source audio, and bundles the existing Auto-Tune Pro benchmark reference.
`validate_real_vocal.py` asserts finite samples, same duration/channels,
bounded level and waveform steps. The result is a real-singer A/B, but not
a verified perceptual ranking or an FL Studio latency/PDC test.

### META grain-click root-cause fix

The real-vocal three-way comparison at 10ms found a large instantaneous
same-channel step near 7.515s. The reconstruction only set its grain fade
once on initial startup: subsequent one-period read-head jumps switched
directly to the new head at full gain. META now starts a bounded 16–512
sample raised-cosine crossfade at **every** such jump. No allocations,
locks or host latency changes were introduced. A test-only A/B switch
and `dsp_v44_meta_grain` verify jump/fade accounting and safe output.
Real-vocal float32 renders must be compared again after this change;
reduced audible clicks and corrected META PDC are not assumed.

## New real-singer bug fix: anticipatory transient dry/wet handoff

A/B analysis of the tracked singer recording found sudden ~0.50–0.58
full-scale single-sample steps in REGULAR and TESTTUNE within the first
80ms, vs ~0.234 in dry/reference and ~0.226 in crossfaded META.
At the jump samples, the output **exactly equaled the 3ms delayed dry**:
this pinpointed the instantaneous `protectedNow ? 1.0` transient guard.
Its existing 3ms latency ring already knows which input sample becomes a
protected dry attack ~132 samples later. Regular and TESTTUNE now pre-fade
wet into dry for ~0.75ms *within that existing delay*, then preserve the
protected attack EXACTLY at the original host-reported PDC sample. No
additional DAW latency, heap allocation, lock, new parameter, project-state
change, or separate plugin identity.

A fixed-size engine-only A/B flag `setAnticipatoryTransientFadeEnabled(false)`
and offline `SKELLY_TRANSIENT_PREFADE_OFF=1` reproduce the old sound;
these are not exposed to host automation. New `dsp_v45_transient_prefade`
checks exact sample timing at 44.1/48/96kHz, finite output and block-size
invariance. New recorded-singer regression in `verify-dsp.yml` measures
the known first-phrase jump against an unfixed render, failing if the real
waveform discontinuity does not decrease by at least 20%.

## Owner's live FL Studio feedback — Nov 22: all three glitch, META worst

The user has auditioned all three, reports META most glitch-prone and
TESTTUNE least. Do not confuse earlier objective pitch-error metrics with
a passing subjective audio result.

- META research: unvoiced noise formerly passed through stale pitched
  source-head grains. A 3ms dry tap reuses the existing fixed-size ring,
  with 8ms wet entry and 4ms unvoiced wet exit. Amount=0 returns
  sample-exact host-aligned dry, bypass remains bit-identical. **Fixed
  wet waveform PDC remains unverified** and the META Lab is TEST-ONLY.
- REGULAR: bound the zero-ms per-sample pitch-ratio slew to prevent large
  one-sample resampler jumps, without changing actual note targets.
- TESTTUNE: retain its fast period-domain snap but require a second hop
  for uncertain low-confidence adjacent-note chatter; use a faster
  ratio-slew limit than REGULAR.
- Two new portable DSP regressions: META consonant/noise continuity,
  bypass, Amount=0, and both algorithms' per-sample cents bound.

No existing preset state/automation ID, default retune time, plugin
identifier, or released UI layout is changed.

### Additional META voiced-path fix: cubic fractional grains + Mix

The lab still sounded grainy during *voiced* parts, beyond the unvoiced
noise/dry transition. Every fractional source-head read used only two
linear samples, which blurs high-frequency harmonic detail when the grain
speed differs from unity. The default in **META Lab only** now uses
four-point cubic Lagrange interpolation. Old two-tap mode remains
selectable by a test-only engine method for objective A/B. New
`dsp_v48_meta_interpolation` measures analytic 0.31-sample fractional
sine interpolation error on 700/2500/5600/8000Hz and rejects any change
that fails to cut error substantially or causes obvious overshoot.

The processor's META route also inadvertently ignored the existing
host `mix` parameter. It now passes `amount * mix` to META, so
Mix=0 uses its tested sample-exact delayed-dry path. Old host IDs,
preset schema and explicit plugin bypass are unchanged.

Do not interpret reduced fractional sample error as verified subjective
better vocal tone. META's variable wet source age and DAW PDC remain
research gaps; keep it in the isolated, test-only Lab VST3.

### META robustness: nonfinite/denormal sample containment

In the lab resampler a single NaN/Inf host input previously entered
the per-channel ring and could poison future grain interpolations.
Unlike production REGULAR, it lacked sample sanitization. Now internal
history and detection input are constrained to finite, normal floats,
without changing bit-exact *bypass output*. Grain wet level snaps
below 1e-9 to exactly zero to prevent silence-related denormals.
The expanded `dsp_v46_meta_dry_guard` injects NaN, Inf and subnormal
samples and asserts finite, bounded output after processing.
