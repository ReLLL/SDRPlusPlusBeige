# Modulation-aware Baseband NR

Read when: changing automatic NR selection, gain transitions, shared-VFO policy or its receiver tests.

Version 1.3.20 adds the measured Wiener voice candidate without removing the accepted Original LogMMSE method. Baseband NR's existing on/off preference is preserved. The new profile defaults to Automatic (mode). Versions 1.3.18 and 1.3.19 were intermediate QA builds.

| Profile / selected modulation | Effective method |
|---|---|
| Automatic, USB or LSB | Wiener voice with one noise-model update per FFT frame |
| Automatic, AM / CW / DSB | Accepted Original LogMMSE |
| Automatic, NFM / WFM / RAW | Bypass; preprocessor disabled |
| Automatic, no selected radio or unknown mode | Original LogMMSE |
| Original LogMMSE | Accepted method, regardless of modulation |
| Digital / bypass | Bypass, including digital signals carried in USB/LSB |

The mode query uses the existing radio interface, on the UI thread, once per drawn frame. It follows the selected waterfall radio rather than guessing from bandwidth or frequency. There are no new module command IDs, exported object fields or background polling threads.

Baseband processing affects every receiver. Automatic uses Wiener only when the selected radio and all other active radio VFOs are USB/LSB. Mixed SSB/AM/CW receivers use Original; an active FM/raw radio forces bypass. Disabled radios are excluded. This cannot detect digital payloads inside an SSB receiver or third-party decoder activity: choose Digital / bypass for those signals. No protocol recognition or increased decoder success is claimed.

## State and audio continuity

Original and voice use the same startup capture, FFT length/window/hop, noise history, overlap-add and original 4x level. Voice changes only the noise-update clock and spectral gain. It avoids re-estimating noise on input packets without a new transform frame and uses Wiener gain instead of the LogMMSE exponential-integral gain.

Method changes are atomic requests applied by the DSP worker at its next input block. They preserve pending samples, learned noise and overlap. Acceleration changes still replace only FFT plans. Original stays the default for the numerical `ClassicNR` seam and for Audio NR paths; only the Baseband automatic voice choice enables the new flags.

Bypass removes the preprocessor from the frontend chain, as switching Baseband NR off does. It passes the ordinary IQ path, without NR's historical 4x boost, and consumes no NR DSP time. Returning from bypass starts the established 280 ms noise capture. That explicit start differs from switching between Original and voice while NR stays active, which does not recapture noise. Receiver mode changes can independently restart their demodulator/audio chain.

No new strength control or gain smoothing was added. Those experiments did not offer a reliable quality advantage. AM/CW retain the accepted path because the more promising startup/prior variant had limited independent evidence. [Research and qualifications](experimental/NR-RESEARCH.md).

## Settings and visibility

- `BasebandProfile`: integer 0 Automatic, 1 Original, 2 Digital. Missing or invalid values use Automatic. Saved only when the profile is changed.
- Existing `IFNR`, `BasebandAcceleration`, source/tuning, view selection and Audio NR2 preferences retain their meanings.
- The NR panel shows the selected modulation and resolved method, including Bypassed. A tooltip explains the shared receiver rule and SSB digital bypass.
- Debug HTTP adds `profile` (`auto`, `original`, `digital`), read-only `effective` and `selected_mode` under the existing NR procfs path. Invalid profile writes leave the choice unchanged. The HTTP server remains disabled on ordinary launches.

## Verification

The numerical tests exercise the actual `ClassicNR` and `IFNRLogMMSE` wrappers. They require agreement with the independent research candidate, packet independence, Apple/FFTW agreement, finite silence, unchanged sample counts during repeated method/backend changes, Hold/reset behavior and bounded output. The accepted-method comparison remains in the existing regression suite. ASAN covers the new numerical test code; the linked shared core is not wholly instrumented.

`baseband_mode_smoke.py` accepts only four-module isolated File or RTL-SDR profiles. It checks real radio modulation changes, profile choices, invalid writes, backend changes, stop/start, continuing audio and the NullAudioSink nonfinite counter. NullAudioSink now counts invalid stereo samples for this QA purpose; normal audio sinks are unchanged.

Performance comparisons use the same 65,536-point display FFT, 3D history horizon and source settings. File input is unthrottled and can consume more total CPU when processing more samples; its CPU percentage is a stress measurement. RTL-SDR provides the real-time comparison. Warmup exceeds the history horizon; retained row counts are recorded because RF cadence does not always fill all 64 slots. Do not infer whole-app savings or FPS from the isolated NR benchmark.

## October 22 qualification

The final package passes all nine CTests, public Brown object-layout/command-ID comparison and full signature verification. This includes the original-path regression and new normal/ASAN mode tests. Recorded-IQ receiver tests pass 24 modulation changes at 800 kS/s and another 24 at 204.8 kS/s, including actual Apple/FFTW backend changes and continuing finite audio. Eight final-build mode changes also pass on the connected RTL-SDR. Bypass is verified by stopped NR processing plus continuing audio. Invalid profile writes, oversized saved values, Digital profile persistence, unchanged master state and clean exits pass.

Live RF comparison, four-module RTL-SDR/Radio/NR/NullAudioSink profile, 3.2 MS/s input with fourfold decimation to 800 kS/s:

| Method / view | Whole-process CPU, one core | UI FPS median | NR processing-load display | History rows median |
|---|---:|---:|---:|---:|
| Original / 3D, first interval | 41.31% | 73.40 | 20.0% | 51 |
| Automatic / 3D | 34.66% | 74.03 | 13.0% | 50 |
| Automatic / 2D | 31.45% | 72.32 | 13.0% | 49 |
| Original / 3D, last interval | 38.97% | 74.35 | 19.5% | 49 |

The measured 3D CPU saving is about 5.5 percentage points of one core against the average of the bracketing Original intervals. The NR panel uses processing wall time, which includes scheduling delays; it is not a per-thread CPU-time counter. Whole-process CPU uses `ps` CPU-time deltas. The private decoder/plugin workload is not included. Display refresh, RF contents, temperature and background load limit generalization. No new geometry or resolution reduction was made.

An eight-second Time Profiler capture of the final build uses the verified QA PID, binary/module paths and a live `vmmap` load address. Symbolication finds the expected IFNR worker, ClassicNR, LogMMSE frame and noise-update stacks. This confirms the exercised path; sample counts alone do not quantify a speedup. Raw traces and private installation receipts remain outside Git.

Native inspection confirms Automatic, Original and Digital controls, selected-mode/status text and aligned controls during bypass. The first File-source trial used float IQ with Float32 Mode off; it was discarded. The repeated receiver measurements and mode tests use PCM16 IQ, and the harness rejects the wrong WAV width. The direct numerical research and RTL measurements do not depend on that File-format setting.

Durable evidence: [`validation/baseband-1.3.20`](validation/baseband-1.3.20/). Build mapping is explicit because 1.3.18/1.3.19 were QA-only intermediates; 1.3.20 adds the final bypass/off row alignment. Its DSP is unchanged from the qualified 1.3.19 implementation.

Installed 1.3.20 preserves 106 private-profile JSON files and ten private plugin binaries during replacement. The installed 1.3.17 backup is `build-beige/installed-before-1.3.20.app.backup-20261022T084421909356`. Normal reception resumes at the saved 7.092 MHz LSB tune and selected 2D view. Oz compared Automatic with Original on voice and reported clearer audio with no distortion. This is acceptance for that listening comparison, not a general decoder or performance result.

```bash
cmake --build build-beige --target beige_baseband_mode_test beige_baseband_mode_asan
ctest --test-dir build-beige --output-on-failure
python3 beige/check_plugin_abi.py
python3 beige/tests/baseband_mode_smoke.py --url http://127.0.0.1:18870 \
  --fixture /tmp/beige-mode-qa/baseband_7100000Hz_800000.wav \
  --pid <verified-QA-pid> --output /tmp/beige-mode-qa/receiver-after.json
python3 beige/tests/baseband_mode_smoke.py --url http://127.0.0.1:18871 \
  --source RTL-SDR --measure-only --pid <verified-QA-pid> \
  --output /tmp/beige-mode-qa/receiver-rf-after.json
```

The existing profile combobox switches between Automatic (improved SSB voice), Original LogMMSE (standard) and Digital / bypass during reception. No restart is required. Automatic acceleration is independent; disable it with Original to compare the full original FFTW path. Oz confirmed the combobox is sufficient and requested no separate toggle. Listening acceptance remains specific to the tested voice signal; check other signals for consonants, fading and musical/watery artifacts.
