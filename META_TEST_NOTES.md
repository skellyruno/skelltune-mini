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
