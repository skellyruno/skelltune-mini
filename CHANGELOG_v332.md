# SkellyTune Mini v3.3.2

Files changed: `dsp/RealtimeTuner.hpp`, `dsp/NoteDecision.hpp`, `dsp/SafePresetValue.hpp`, `dsp/PeriodTable.hpp` (new),
`Source/PluginProcessor.cpp`, `Source/PluginEditor.h/.cpp`, `tools/render.cpp`, `cmake/SkellyDSPTests.cmake`, plus four NEW tests.
**One existing test changed:** `tests/test_preset_safety.cpp` (saved-parameter count 16 -> 17, plus two checks for the new `testTune`
parameter). No other existing test was modified. Existing parameter IDs, plugin IDs and the latency contract are unchanged; one
parameter (`testTune`, default off) and one header button were added.

## DSP
1. **Tracker octave-down lock.** The v1.7 octave rule compared integer-lag CMND values. At the 12 kHz analysis rate a period near
   N+0.5 samples is rounded badly at 1x but well at 2x, faking 0.035-0.05 of "evidence". A *clean steady tone* at ~282, 247, 220, 198
   or 180 Hz was reported an octave low on 450/450 frames. Fix: compare interpolated valley depths (`valleyDepth`), raise the evidence
   needed from 0.035 to 0.06 (`kOctaveEvidence`; test v17 fails at 0.08), and require 0.15 (`kOctaveContinuityEvidence`) to switch
   octave while already locked at the shorter period within ~60 ms.
2. **Hold the current note while a new note is pending** (`setHoldActiveNoteWhilePending`, `kHoldActiveMaxCents` = 150). Previously the
   engine faded to raw dry audio at every note change.
3. **Vowel noise dead zone** (`kVowelNoiseDeadZone` = 0.25). Broadband noise attenuation bled 10-33% unshifted dry audio under clean
   vowels (a ghost at the original pitch, ~8% level loss).
4. **Boundary hysteresis** in `NoteDecision` (`kBoundaryHysteresisCents` = 25). At max retune any excursion past a note boundary
   committed after 8 ms, so a voice hovering between two scale notes flipped targets constantly. The fast commit now needs the pitch 25
   cents past the boundary, otherwise 120 ms of sustained evidence. Onsets and decisive changes are unchanged.
5. **Stay wet through zero correction** (`kKeepWetThroughZeroCorrection`, `kZeroHoldSeconds` = 0.12). When the needed correction passed
   through ~0 cents (vibrato, a slide through a note centre) the engine faded to dry, crossfading two copies of the voice at DIFFERENT
   delays (the wet read head is typically a fraction of a period from the dry tap) and mangling a cycle or two. A slow 50 cent/s sweep
   through zero dropped the wet mix to 0.011 in v3.3.1. Now the wet path is kept until the correction has stayed near zero for 120 ms
   (genuinely in tune), then it hands off to exact delayed dry as before, so v0.5's "in-tune converges to exact delayed dry" contract
   still holds. An explicit Amount of ~0 still goes dry.

## UI
6. Tooltips never showed: controls set tooltips but no `TooltipWindow` existed. Added `tooltipWindow_` to `PluginEditor.h`. Not compiled
   here (no JUCE); the Windows CI GUI smoke test should confirm.

## NEW: TestTune switch (A/B algorithm)
7. **`testTune` (default OFF)**: an alternate target-note selection you can switch to for A/B listening. It builds a table of in-scale
   note PERIODS from the scale mask (`dsp/PeriodTable.hpp`, standard equal-temperament math: `period = sampleRate / (A4 * 2^((n-69)/12))`
   for MIDI notes 0..127) and snaps each pitch estimate to the nearest period, with NO hysteresis, pending state or note-hold logic.
   The tracker, voicing gates and shifter are shared with the standard algorithm. The period-domain boundary between two notes is the
   arithmetic mean of their periods, ~0.6 cents below the geometric midpoint the standard algorithm uses. Falls back to the standard
   algorithm if the table is empty (no notes enabled, or sample rate/A4 out of range).
   Where: `Parameters::testTune` (engine), APVTS parameter `testTune` (also added to the saved-state allow-list in
   `SafePresetValue.hpp`, otherwise it would reset to off on every project reload), a TESTTUNE toggle button in the editor header row,
   and `SKELLY_TESTTUNE=1` for `tools/render`. With the switch off the standard algorithm is bit-identical to before (verified on three
   real clips).
   Provenance: written from general equal-temperament math. It contains no code, constants or structure taken from any third-party
   binary or decompilation.
   Measured (same three clips, same scorer): pitch score G/E/B standard 72.2/75.7/54.2 vs testtune 69.4/72.5/54.6; frames on a different
   note than the Auto-Tune Pro/XPitch/Waves consensus: standard 7.7/9.1/5.1% vs testtune 0.5/0.7/0.5%; roughness E/B 7.1/5.9% vs 5.9/4.8%.
   i.e. testtune switches notes like the commercial plugins do (stateless snapping) but scores ~3 points lower on two of three clips
   because it flips more at note boundaries. Not compiled in JUCE here (editor/processor changes are unbuilt and untested).

## Tried and REMOVED
- **Glide lead compensation** (extrapolating the pitch trajectory by the ~10 ms analysis-window delay). It looked good on synthetic
  glides (output error p90 53.7 -> 23.5 cents) but on three real clips it was neutral to slightly harmful (B major moving-frame
  roughness 3.6% -> 4.2%; 18 ms clearly worse), needed a loosened engine-test threshold and produced an onset overshoot. Removed
  entirely; `test_engine.cpp` is back to its original 0.15 threshold.

## Measured (G major clip = benchmark/v33; E minor and B major = your new low-male clips; 0 ms retune, 100% wet; own YIN scorer,
## no-pitch frames count as misses; % of raw-voiced frames within 25 cents of a scale note)
| clip | v3.3.1 | v3.3.2 | Auto-Tune Pro | XPitch | Waves |
|---|---|---|---|---|---|
| G major (8 s) | 62.3% | 72.2% | 73.2% | 72.8% | 70.2% |
| E minor (11 s, low male) | 65.0% | 75.7% | 77.7% | 77.2% | 76.2% |
| B major (15 s, low male) | 49.3% | 54.2% | 56.3% | 56.9% | 56.9% |

Roughness = share of frames where the INPUT is clean and strongly periodic but the output is badly aperiodic (CMND > 0.2):
E minor 14.0% -> 7.1% (references 3.6-3.8%); B major 8.9% -> 5.9% (references 2.6-2.8%). Steady-note roughness reached parity with the
references (E minor 1.1% vs 1.6-2.7%, B major 0.0% vs 0.0%) in the always-wet build; the shipped 120 ms-hold build scores identically
overall. Output peak is unchanged (0.893 on G major; no clipping on any clip). Sibilants/breath unchanged (in-band energy within 0.1 dB).
Independent synthetic checks: clean-voice sweep 99.9% -> 100.0% octave-correct; legato melody rapid reversals 3 -> 0.

## Tests
43 portable DSP tests pass (39 existing + 4 new). The first three new tests FAIL on v3.3.1 and pass on v3.3.2:
- `test_v39_note_change_hold` (smoke check only; weak by design)
- `test_v40_octave_and_hysteresis` (450/450 octave-down frames; 40 target switches while hovering)
- `test_v41_keep_wet_through_zero` (wet mix 0.011 vs 1.0 sweeping through zero; Amount=0 still dry)
- `test_v42_testtune` (period-table math, validation ranges, period-domain nearest-note rule, switch defaults off, no hysteresis when on)

## Examined and deliberately NOT changed
- Octave errors at note onsets: sweep already 100% correct; remaining onset events are low-confidence frames the engine treats as unvoiced;
  strictly alternating cycles are genuinely ambiguous between T and 2T. Most "octave jumps" on the low-male clips are vocal-fry note tails.
- Note-change latency tail (p90 74 -> ~124 ms): three attempts either did nothing or cost accuracy. It is a trade-off of holding the old
  note. A/B with `setHoldActiveNoteWhilePending(false)`.
- Formant preservation: formants move with pitch (F1 +29% on a +400 cent shift). On the G major vocal corrections are mostly small
  (median 42 cents, p99 152), so a full formant-correction stage was judged not worth it. It would matter for large corrections.

## Known gaps / not verified
- Remaining roughness is concentrated at note changes and while the input is moving (moving-frame roughness ~1.5x the references);
  still ~2-4 points behind the commercial plugins on pitch score.
- Three real clips (one voice type per scale) plus synthetic voices; scored with my own tools. No listening test.
- No JUCE / VST3 / Windows / FL Studio build was available here.


## META (TEST ONLY) — v3.3.2 research branch

An experimental port of the user's reconstructed `metatune.cpp` pipeline is included solely for isolated A/B characterization. It is guarded by the opt-in CMake option `SKELLY_META_TEST` (default OFF); the guarded source deliberately refuses to compile without the option. A test-build-only third algorithm segment appears next to REGULAR and TESTTUNE, and the offline render tool accepts `SKELLY_META`, `SKELLY_META_FILE_DETECTOR`, `SKELLY_META_AS_WRITTEN`, and `SKELLY_META_SPEED` switches.

The file reconstruction's own detector was measured to find almost no pitch on the tested voiced tones, and its as-written smoother feed produces no correction. The test UI defaults instead to the standard causal pitch tracker and the detected-period smoother, solely to make the reconstruction's remaining signal path comparable. META is not saved into preset state, is not latency-compensated, can shift consonants during unvoiced gaps, and must never be shipped. The working experimental mode also measured below the standard engine and commercial references on the three sample clips. See `META_TEST_NOTES.md` for measurements and characterization-test details.

The normal build remains unchanged when `SKELLY_META_TEST` is OFF. Original VST3 IDs, host parameter IDs, preset formats and the standard DSP engine remain intact.

