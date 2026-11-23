# Imported from the tested v3.3 portable SkellyTune CMake graph.
# Enabled only when SKELLY_BUILD_DSP_TESTS=ON.
add_executable(dsp_tests tests/test_engine.cpp)
target_link_libraries(dsp_tests PRIVATE skelly_dsp)
add_test(NAME dsp_regression COMMAND dsp_tests)
add_executable(dsp_smoke tests/sanitizer_smoke.cpp)
target_link_libraries(dsp_smoke PRIVATE skelly_dsp)
add_test(NAME dsp_smoke COMMAND dsp_smoke)
add_executable(skelly_render tools/render.cpp)
target_link_libraries(skelly_render PRIVATE skelly_dsp)
add_executable(transition_render tools/transition_render.cpp)
target_link_libraries(transition_render PRIVATE skelly_dsp)
add_executable(dsp_v06_tests tests/test_v06.cpp)
target_link_libraries(dsp_v06_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v06_tests COMMAND dsp_v06_tests)
add_executable(dsp_benchmark tools/benchmark.cpp)
target_link_libraries(dsp_benchmark PRIVATE skelly_dsp)

add_executable(dsp_v05_tests tests/test_v05.cpp)
target_link_libraries(dsp_v05_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v05_upgrade COMMAND dsp_v05_tests)

add_executable(dsp_v07_tests tests/test_v07.cpp)
target_link_libraries(dsp_v07_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v07_tests COMMAND dsp_v07_tests)

add_executable(dsp_v08_tests tests/test_v08.cpp)
target_link_libraries(dsp_v08_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v08_tests COMMAND dsp_v08_tests)

add_executable(dsp_v09_tests tests/test_v09.cpp)
target_link_libraries(dsp_v09_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v09_cycle_synth COMMAND dsp_v09_tests)

add_executable(cycle_render tools/cycle_render.cpp)
target_link_libraries(cycle_render PRIVATE skelly_dsp)

add_executable(dsp_v10_tests tests/test_v10.cpp)
target_link_libraries(dsp_v10_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v10_integration COMMAND dsp_v10_tests)

add_executable(dsp_benchmark_modes tools/benchmark_modes.cpp)
target_link_libraries(dsp_benchmark_modes PRIVATE skelly_dsp)

# v1.1 experimental synthesis regressions: channel-invariant pitch marks,
# cubic reconstruction, gap diagnostics, and state reset.
add_executable(dsp_v11_tests tests/test_v11.cpp)
target_link_libraries(dsp_v11_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v11_cycle_quality COMMAND dsp_v11_tests)

# v1.2 causal grain release, re-entry gating and fallback tests
add_executable(dsp_v12_tests tests/test_v12.cpp)
target_link_libraries(dsp_v12_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v12_release COMMAND dsp_v12_tests)

# v1.3 sub-sample phase mark regression and stereo invariance
add_executable(dsp_v13_tests tests/test_v13.cpp)
target_link_libraries(dsp_v13_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v13_fractional_marks COMMAND dsp_v13_tests)

# v1.4 experimental vocal-air restoration regression
add_executable(dsp_v14_tests tests/test_v14.cpp)
target_link_libraries(dsp_v14_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v14_air_preservation COMMAND dsp_v14_tests)

# v1.5 scale, user note-mask and bypass routing regressions.
add_executable(dsp_v15_tests tests/test_v15.cpp)
target_link_libraries(dsp_v15_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v15_scale_mask COMMAND dsp_v15_tests)

# v1.6 causal detector re-entry and stale retune intent regression.
add_executable(dsp_v16_tests tests/test_v16.cpp)
target_link_libraries(dsp_v16_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v16_voice_reentry COMMAND dsp_v16_tests)

# v1.7 conservative octave arbitration for weak fundamentals and harmonic stacks.
add_executable(dsp_v17_tests tests/test_v17.cpp)
target_link_libraries(dsp_v17_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v17_octave_arbitration COMMAND dsp_v17_tests)

# v1.8 analysis-only mains-hum rejection, preserving the actual audio signal.
add_executable(dsp_v18_tests tests/test_v18.cpp)
target_link_libraries(dsp_v18_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v18_mains_hum_rejection COMMAND dsp_v18_tests)

# v1.9 causal wet path measurement and phase/formant A/B probe.
add_executable(wet_path_probe tools/wet_path_probe.cpp)
target_link_libraries(wet_path_probe PRIVATE skelly_dsp)

add_executable(dsp_v19_tests tests/test_v19.cpp)
target_link_libraries(dsp_v19_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v19_wet_latency_formant_probe COMMAND dsp_v19_tests)
add_executable(benchmark_lowvoice tools/benchmark_lowvoice.cpp)
target_link_libraries(benchmark_lowvoice PRIVATE skelly_dsp)

# v2.0: verify PDC in both synthesis modes with fully engaged wet tuning.
add_executable(dsp_v20_tests tests/test_v20.cpp)
target_link_libraries(dsp_v20_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v20_transient_host_delay COMMAND dsp_v20_tests)
add_executable(measure_output_delay tools/measure_output_delay.cpp)
target_link_libraries(measure_output_delay PRIVATE skelly_dsp)

# v2.1 clock-locked studio timebase validation; original plugin remains 3 ms.
add_executable(dsp_v21_tests tests/test_v21.cpp)
target_link_libraries(dsp_v21_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v21_clock_locked_timeline COMMAND dsp_v21_tests)
add_executable(dsp_v22_tests tests/test_v22.cpp)
target_link_libraries(dsp_v22_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v22_fast_studio_latency COMMAND dsp_v22_tests)
add_executable(timeline_compare tools/timeline_compare.cpp)
target_link_libraries(timeline_compare PRIVATE skelly_dsp)

add_executable(fast_studio_compare tools/fast_studio_compare.cpp)
target_link_libraries(fast_studio_compare PRIVATE skelly_dsp)

add_executable(dsp_v22_safety tests/test_v22_safety.cpp)
target_link_libraries(dsp_v22_safety PRIVATE skelly_dsp)
add_test(NAME dsp_v22_safety_smoke COMMAND dsp_v22_safety)

# v2.3: phase-decorrelated wet/dry level stability and strict bypass latency.
add_executable(dsp_v23_tests tests/test_v23.cpp)
target_link_libraries(dsp_v23_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v23_stereo_mix_stability COMMAND dsp_v23_tests)
add_executable(profile_callbacks tools/profile_callbacks.cpp)
target_link_libraries(profile_callbacks PRIVATE skelly_dsp)

# v2.4: causal envelope matching, wet-only signal paths, antiphase stereo,
# exact realtime/bypass preservation and block-size invariance.
add_executable(dsp_v24_tests tests/test_v24.cpp)
target_link_libraries(dsp_v24_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v24_formant_envelope COMMAND dsp_v24_tests)

# v2.5: reproducible phase/formant and genuine envelope-delay measurements.
add_executable(v25_compare tools/v25_compare.cpp)
target_link_libraries(v25_compare PRIVATE skelly_dsp)
add_executable(v25_voiced_delay tools/v25_voiced_delay.cpp)
target_link_libraries(v25_voiced_delay PRIVATE skelly_dsp)
add_executable(dsp_v25_tests tests/test_v25.cpp)
target_link_libraries(dsp_v25_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v25_phase_timeline COMMAND dsp_v25_tests)

add_executable(dsp_v26_tests tests/test_v26.cpp)
target_link_libraries(dsp_v26_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v26_phrase_reentry COMMAND dsp_v26_tests)

# v2.7: preserve conservative tuning, accelerate max-speed G-major retune.
add_executable(dsp_v27_tests tests/test_v27.cpp)
target_link_libraries(dsp_v27_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v27_hard_tune COMMAND dsp_v27_tests)

# v2.8 true 0ms correction slew, adaptive multi-resolution pitch analysis,
# safe centered-note hard locking and low-fundamental regressions.
add_executable(dsp_v28_tests tests/test_v28.cpp)
target_link_libraries(dsp_v28_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v28_zero_ms_adaptive COMMAND dsp_v28_tests)

# v2.9: harmonic-versus-noise wet gate regression and A/B verification.
add_executable(dsp_v29_tests tests/test_v29.cpp)
target_link_libraries(dsp_v29_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v29_harmonic_wet_gate COMMAND dsp_v29_tests)

# v3.0 opt-in glottal-event pitch mark, provisional transition and LPC
# source-filter research. Production defaults intentionally unchanged.
add_executable(dsp_v30_tests tests/test_v30.cpp)
target_link_libraries(dsp_v30_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v30_research_paths COMMAND dsp_v30_tests)
add_executable(v30_compare tools/v30_compare.cpp)
target_link_libraries(v30_compare PRIVATE skelly_dsp)

# v3.1 low-confidence dropouts: bounded, voiced-only bridging; no PDC changes.
add_executable(dsp_v31_tests tests/test_v31.cpp)
target_link_libraries(dsp_v31_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v31_short_voiced_bridge COMMAND dsp_v31_tests)

# v3.2: explicitly verify acquisition timing without sacrificing low notes.
add_executable(dsp_v32_tests tests/test_v32.cpp)
target_link_libraries(dsp_v32_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v32_acquisition_integrity COMMAND dsp_v32_tests)

# v3.3: remembered unvoiced-attack evidence at reliable hard-tune pitch hops.
add_executable(dsp_v33_tests tests/test_v33.cpp)
target_link_libraries(dsp_v33_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v33_onset_memory COMMAND dsp_v33_tests)

# Validate saved parameters before replacing APVTS state in a VST3 host.
add_executable(dsp_preset_safety tests/test_preset_safety.cpp)
target_link_libraries(dsp_preset_safety PRIVATE skelly_dsp)
add_test(NAME dsp_preset_safety COMMAND dsp_preset_safety)

# Max-retune cycle overlap math: replace expensive audio-thread cosine with LUT.
add_executable(dsp_phase_crossfade_tests tests/test_phase_crossfade.cpp)
target_link_libraries(dsp_phase_crossfade_tests PRIVATE skelly_dsp)
add_test(NAME dsp_phase_crossfade COMMAND dsp_phase_crossfade_tests)

# v3.4: detector ring-index optimisation and antiphase stereo integrity.
add_executable(dsp_v34_tests tests/test_v34_tracker.cpp)
target_link_libraries(dsp_v34_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v34_pitch_tracker_performance_regression COMMAND dsp_v34_tests)

# v3.5: bounded candidate excursion / faster confident adjacent-note locking;
# protects expressive vibrato and verifies sample-loop pitch-ratio equivalence.
add_executable(dsp_v35_tests tests/test_v35_note_lock.cpp)
target_link_libraries(dsp_v35_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v35_stable_note_lock COMMAND dsp_v35_tests)

# v3.6: production-engine audio-path safety, dry PDC, sample-block invariance,
# nonfinite/subnormal host input recovery, basic output headroom and CPU.
add_executable(dsp_v36_audio_integrity tests/test_v36_audio_integrity.cpp)
target_link_libraries(dsp_v36_audio_integrity PRIVATE skelly_dsp)
add_test(NAME dsp_v36_audio_integrity COMMAND dsp_v36_audio_integrity)

# v3.7: subsonic wet-only filter response and complete-engine DC/PDC checks.
add_executable(dsp_v37_wet_rumble tests/test_v37_wet_rumble.cpp)
target_link_libraries(dsp_v37_wet_rumble PRIVATE skelly_dsp)
add_test(NAME dsp_v37_wet_rumble COMMAND dsp_v37_wet_rumble)

# v3.8: loud vowel amplitude scaling, headroom and accidental transient bypass.
add_executable(dsp_v38_loud_voice tests/test_v38_loud_voice.cpp)
target_link_libraries(dsp_v38_loud_voice PRIVATE skelly_dsp)
add_test(NAME dsp_v38_loud_voice COMMAND dsp_v38_loud_voice)

add_executable(dsp_v39_tests tests/test_v39_note_change_hold.cpp)
target_link_libraries(dsp_v39_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v39_note_change_hold COMMAND dsp_v39_tests)

add_executable(dsp_v40_tests tests/test_v40_octave_and_hysteresis.cpp)
target_link_libraries(dsp_v40_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v40_octave_and_hysteresis COMMAND dsp_v40_tests)

add_executable(dsp_v41_tests tests/test_v41_keep_wet_through_zero.cpp)
target_link_libraries(dsp_v41_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v41_keep_wet_through_zero COMMAND dsp_v41_tests)

add_executable(dsp_v42_tests tests/test_v42_testtune.cpp)
target_link_libraries(dsp_v42_tests PRIVATE skelly_dsp)
add_test(NAME dsp_v42_testtune COMMAND dsp_v42_tests)

# v3.3.3: causal prefade before protected input transients at exact
# original host PDC, including different callback sizes/sample rates.
add_executable(dsp_v45_transient_prefade tests/test_v45_transient_prefade.cpp)
target_link_libraries(dsp_v45_transient_prefade PRIVATE skelly_dsp)
add_test(NAME dsp_v45_transient_prefade COMMAND dsp_v45_transient_prefade)

# v3.3.4: note-switch anti-zipper protection in both regular and
# TESTTUNE hard-retune synthesis paths.
add_executable(dsp_v47_hard_ratio_guard tests/test_v47_hard_ratio_guard.cpp)
target_link_libraries(dsp_v47_hard_ratio_guard PRIVATE skelly_dsp)
add_test(NAME dsp_v47_hard_ratio_guard COMMAND dsp_v47_hard_ratio_guard)

# TESTTUNE's held period-domain note must not fall back to unrelated
# REGULAR musical hysteresis during brief low-confidence voiced frames.
add_executable(dsp_v51_testtune_hold tests/test_v51_testtune_hold.cpp)
target_link_libraries(dsp_v51_testtune_hold PRIVATE skelly_dsp)
add_test(NAME dsp_v51_testtune_hold COMMAND dsp_v51_testtune_hold)

# Stabilize detector-only stereo routing when channels approach opposite
# polarity: no one-sample mid/side jumps, preserved anti-phase F0 signal.
add_executable(dsp_v52_stereo_route tests/test_v52_stereo_router.cpp)
target_link_libraries(dsp_v52_stereo_route PRIVATE skelly_dsp)
add_test(NAME dsp_v52_stereo_route COMMAND dsp_v52_stereo_route)

# TEST ONLY: Meta engine tests (built only when -DSKELLY_META_TEST=ON).
if(SKELLY_META_TEST)
    add_executable(dsp_v43_meta tests/test_v43_meta.cpp)
    target_link_libraries(dsp_v43_meta PRIVATE skelly_dsp)
    add_test(NAME dsp_v43_meta COMMAND dsp_v43_meta)

    # Regress the actual Meta read-head jump bug; research engine only.
    add_executable(dsp_v44_meta_grain tests/test_v44_meta_grain.cpp)
    target_link_libraries(dsp_v44_meta_grain PRIVATE skelly_dsp)
    add_test(NAME dsp_v44_meta_grain COMMAND dsp_v44_meta_grain)

    # Do not grain-resample unvoiced consonants/air or break dry PDC in META.
    add_executable(dsp_v46_meta_dry_guard tests/test_v46_meta_dry_guard.cpp)
    target_link_libraries(dsp_v46_meta_dry_guard PRIVATE skelly_dsp)
    add_test(NAME dsp_v46_meta_dry_guard COMMAND dsp_v46_meta_dry_guard)

    # META only: prove cubic fractional resampling is materially more
    # accurate than the original grain's two-tap linear interpolation.
    add_executable(dsp_v48_meta_interpolation tests/test_v48_meta_interpolation.cpp)
    target_link_libraries(dsp_v48_meta_interpolation PRIVATE skelly_dsp)
    add_test(NAME dsp_v48_meta_interpolation COMMAND dsp_v48_meta_interpolation)

    # META Lab only: bounded phase-aligned grain resets, stereo and high
    # frequency/high-sample-rate causal read-head safety.
    add_executable(dsp_v49_meta_phase tests/test_v49_meta_phase.cpp)
    target_link_libraries(dsp_v49_meta_phase PRIVATE skelly_dsp)
    add_test(NAME dsp_v49_meta_phase COMMAND dsp_v49_meta_phase)

    # META only: physical millisecond speed response, four sample rates
    # and a real processed harmonic note-transition fixture.
    add_executable(dsp_v50_meta_speed tests/test_v50_meta_speed.cpp)
    target_link_libraries(dsp_v50_meta_speed PRIVATE skelly_dsp)
    add_test(NAME dsp_v50_meta_speed COMMAND dsp_v50_meta_speed)

    # Full three-way deterministic synthetic vocal rendering and metrics.
    # Never runs inside the host audio thread and never distributes Meta DSP.
    add_executable(three_mode_ab tools/three_mode_ab.cpp)
    target_link_libraries(three_mode_ab PRIVATE skelly_dsp)
endif()

# TESTTUNE transient note-owner and bounded 0ms sample-to-sample correction.
add_executable(dsp_v53_testtune_continuity tests/test_v53_testtune_continuity.cpp)
target_link_libraries(dsp_v53_testtune_continuity PRIVATE skelly_dsp)
add_test(NAME dsp_v53_testtune_continuity COMMAND dsp_v53_testtune_continuity)

# Causal note-editor host transport, lock-free edits, and audible target route.
find_package(Threads REQUIRED)
add_executable(dsp_v54_pitch_edit_timeline tests/test_v54_pitch_edit_timeline.cpp)
target_link_libraries(dsp_v54_pitch_edit_timeline PRIVATE skelly_dsp Threads::Threads)
add_test(NAME dsp_v54_pitch_edit_timeline COMMAND dsp_v54_pitch_edit_timeline)
add_executable(dsp_v55_manual_pitch_modes tests/test_v55_manual_pitch_modes.cpp)
target_link_libraries(dsp_v55_manual_pitch_modes PRIVATE skelly_dsp)
add_test(NAME dsp_v55_manual_pitch_modes COMMAND dsp_v55_manual_pitch_modes)

if(SKELLY_META_TEST)
    add_executable(dsp_v56_meta_stereo tests/test_v56_meta_stereo.cpp)
    target_link_libraries(dsp_v56_meta_stereo PRIVATE skelly_dsp)
    add_test(NAME dsp_v56_meta_stereo COMMAND dsp_v56_meta_stereo)
endif()

# Fast path for the vast majority of audio with no recorded manual edits.
add_executable(dsp_v58_edit_fastpath tests/test_v58_edit_fastpath.cpp)
target_link_libraries(dsp_v58_edit_fastpath PRIVATE skelly_dsp)
add_test(NAME dsp_v58_edit_fastpath COMMAND dsp_v58_edit_fastpath)
if(SKELLY_META_TEST)
    # Research-only: original Hann grain fade vs prepared lookup curve.
    add_executable(dsp_v57_meta_fade_lut tests/test_v57_meta_fade_lut.cpp)
    target_link_libraries(dsp_v57_meta_fade_lut PRIVATE skelly_dsp)
    add_test(NAME dsp_v57_meta_fade_lut COMMAND dsp_v57_meta_fade_lut)
endif()

# Real 1.5ms transport contract, sample-exact dry at three sample rates,
# full-wet finite/peak guard, META dry parity and reset robustness.
# Historical portable callers retain the previous 3ms prepare default.
add_executable(dsp_v59_low_pdc tests/test_v59_low_pdc.cpp)
target_link_libraries(dsp_v59_low_pdc PRIVATE skelly_dsp)
add_test(NAME dsp_v59_low_pdc COMMAND dsp_v59_low_pdc)

if(SKELLY_META_TEST)
    # Invalid host input, timeline, bypass and zero-allocation edit guards.
    add_executable(dsp_v60_meta_host_guard tests/test_v60_meta_host_guard.cpp)
    target_link_libraries(dsp_v60_meta_host_guard PRIVATE skelly_dsp)
    add_test(NAME dsp_v60_meta_host_guard COMMAND dsp_v60_meta_host_guard)
endif()

if(SKELLY_META_TEST)
    add_executable(dsp_v62_meta_tuning_cache tests/test_v62_meta_tuning_cache.cpp)
    target_link_libraries(dsp_v62_meta_tuning_cache PRIVATE skelly_dsp)
    add_test(NAME dsp_v62_meta_tuning_cache COMMAND dsp_v62_meta_tuning_cache)
endif()
