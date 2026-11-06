# SkellyTune v3.1 — real-vocal measurement

The shared 30.57-second, 44.1-kHz stereo take is evaluated at **G major, 0 ms Retune Speed, 100% Amount, 100% Wet, zero Flex/Vibrato**. The outside commercial renders are supplied recordings; their plugin parameter settings were not independently inspected. Commercial curves are references, not target training data.

## Independent results

| Render | Median cents | Within 25¢ | Within 50¢ |
|---|---:|---:|---:|
| Raw | 40.218 | 31.042% | 61.250% |
| XPitch Realtime | 8.743 | 76.458% | 91.875% |
| Waves Tune Realtime | 11.675 | 69.479% | 88.021% |
| Auto-Tune Pro | 7.951 | 76.771% | 91.562% |
| SkellyTune v3.0 | 13.095 | 67.362% | 86.548% |
| SkellyTune v3.1 | 12.736 | 67.987% | 86.340% |

These values use 960 raw-active analysis frames (959 valid Skelly frames) with independent `librosa.yin` estimates. Differences from earlier reports' third decimal are due to re-running and interpolating all six WAV tracks in the same script. Lower cent error can mean more robotic correction; it does not measure formant quality or perceptual preference.

The v3.1 note bridge was invoked on **68 detector update hops** in this complete render. It reduces invalid tracking dropbacks only where the previous frame was trusted, sound energy continues, no new note is pending, noise is low, and there is no onset. Maximum duration is 12 ms. Measured wet waveform transport latency, formant stability and Windows VST3 host behavior remain unverified.

Additional parameter experiments: extending the bridge to 18–35 ms offered no clear benefit. Lowering confidence globally sometimes reduced median cent error but allowed an uncertain F0 to act on a stale retained note. The release leaves tracker threshold at 0.62 and now also requires NoteDecision to mark the candidate voiced.

Output-peak and high-percentile waveform-step checks were essentially unchanged (peak 0.9750, 99.9th-percentile step ~0.11587), which does not replace real-vocal listening tests. Level-matched 20–28-second WAV files are included. For more authoritative conclusions, test additional singers, each with matched scale/retune settings and blind level-matched listening.
