# Audio noise reduction

Read when: changing Audio NR2, neural voice NR, audio CPU/scaling labels, model packaging or audio-rate conversion.

## Audio chain recovery (1.3.30)

Oz reported that Audio NR2 Radio made no audible difference. A four-module File receiver reproduced a checked but disconnected reducer: USB initially processed with a positive NR2 CPU reading; RAW followed by USB left CPU idle and Scaling frozen. Toggling the checkbox did not restore processing. Radio disables every AF block in RAW, but its return path only restores built-in blocks. This reproduces the symptom without establishing Oz's exact preceding mode sequence.

The NR module now reconciles actual AF-chain membership with the current radio mode from its existing UI-frame callback and checkbox setters. RAW retains the reducer preference and bypasses audio effects; returning to an audio mode restores processing. Neural NR has the same recovery. Unchanged membership is an idempotent operation. NR2 suppression equations, Scaling/CPU labels, neural model, acceleration and accepted default values are unchanged.

An initial QA build exposed a concurrent chain mutation: HTTP module commands ran on a connection thread while UI callbacks restored AF blocks. The supplied 1.3.30 crash report is this uninstalled QA process, aborting in stream-mutex locking during Radio mode teardown. Generic module commands now use the existing UI request queue, alongside procfs setters and UI callbacks. No public plugin object layout changed.

`audio_nr_recovery_smoke.py` asserts actual worker activity, preserved selections in RAW and continuing finite audio. It fails on 1.3.29 at the first RAW-to-USB return. Checkbox state and finite dry audio alone did not detect the disconnection in earlier tests. Run against an isolated File/Radio/NR/NullAudioSink profile:

```sh
python3 beige/tests/audio_nr_recovery_smoke.py --url http://127.0.0.1:18873 \
  --fixture /path/to/baseband.wav --cycles 4 --stress 100
```

The fixed receiver passes 56 recovery checks across NR2's audio modes and neural voice modes, then 100 concurrent mode/reducer request pairs with continuing valid audio. These checks verify processing and chain stability, not listening quality.

Oz's subsequent listening check on installed 1.3.30 confirms: "NR2 reduces noise again." This accepts recovery of the reported audible effect; it does not establish long-run crash stability or a change in suppression quality.

## Controls in 1.3.22

Audio NR2 keeps its existing OM-LSA/MCRA suppression method and checkbox. Its changing number is now labeled `Scaling: Nx`: the inverse of the float-to-int16 amplitude scale, relative to 32767. It is not a CPU counter. A separate `% cpu` line measures DSP-thread CPU time per second of input audio, averaged over at least half a second. An idle reducer has no current CPU reading. The Baseband NR meter remains its existing processing-wall-time estimate; the two counters have different definitions.

Neural Voice NR is a separate checkbox for each radio, off by default. It selects the local GTCRN speech model. Selecting it turns Audio NR2 off for that radio, and selecting Audio NR2 turns the neural reducer off. Baseband NR remains independent and retains its existing combobox. Start comparisons with Baseband NR off so the audio methods can be heard separately.

The neural option processes USB, LSB, AM, DSB and NFM audio. CW, RAW and WFM bypass it while retaining the checkbox preference. Digital payloads in USB/LSB cannot be identified by modulation alone: leave speech reduction off for those signals. It is a speech reducer, not a general music, CW or digital decoder filter.

The model uses mono 16 kHz audio and 256-sample inference hops. A bounded polyphase FIR converts the actual radio audio rate to/from 16 kHz, supporting ordinary rates from 8 to 192 kHz. Left/right speech is averaged and restored as dual mono. The FIR, STFT overlap and fixed FIFO preserve exactly one output sample per input sample. Startup uses aligned dry/wet audio and a 100 ms fade into reduction. The native 48 kHz fixtures measure 1,862 samples, or 38.8 ms, of processing delay before device buffers. Enabling/disabling changes audio latency; this is not a sample-synchronous blind switch against unprocessed audio.

## Method selection and measured quality

The offline comparison uses 24 controlled cases: seven noise conditions for three voice excerpts, plus three clean cases. The voice material is two excerpts from one recorded Open Speech Repository speaker and one macOS synthesized male voice. Audio is limited to 300-2700 Hz. Conditions include white noise at 0/-5 dB, colored/stepped noise, impulses, a wandering tone and fading speech. All algorithms receive the same input. Integer delay is compensated before scoring; no spectral equalization or per-case parameter tuning is applied.

Means over the 21 noisy cases, actual native wrappers on this M2 Max:

| Method | eSTOI | STOI | SI-SDR, dB | Median NR CPU, one core |
|---|---:|---:|---:|---:|
| Off | 0.3684 | 0.6271 | -1.80 | - |
| Audio NR2 | 0.3554 | 0.5762 | 4.64 | 0.26% |
| Stock RNNoise | 0.2980 | 0.6028 | 1.40 | 1.09% |
| Native GTCRN | 0.3937 | 0.6942 | 7.17 | 3.04% |

GTCRN improves the average speech metrics here, at more CPU cost than Audio NR2. Stock RNNoise attenuates too much wanted speech in this narrowband set and is not included in the receiver. GTCRN is not a universal winner: individual cases regress, and its clean-speech eSTOI ranges from 0.866 to 0.943. It also reduces projected wanted-speech level by about 5.1 dB on average in the noisy cases. It remains an optional listening choice. These metrics are proxies, not QSO word accuracy, human listening approval or measured off-air performance.

Raw results, provenance and qualification limits: [`experimental/results/2026-10-22-audio`](experimental/results/2026-10-22-audio/). Local level-matched listening clips remain under `/tmp/beige-audio-nr-20261022/native/`; corpus audio is not bundled or committed.

## Acceleration and fallback

The native engine uses ONNX Runtime's ARM CPU kernels, VOLK SIMD for the resamplers and the existing Apple Accelerate FFT backend for its 512-point STFT. If Apple FFT setup is unavailable, the same transform uses FFTW. Tests compare the two outputs and packet sizes. The model/session and all recurring tensor/FFT/FIR/FIFO storage are prepared outside steady-state frame processing. Inference uses one thread without a spinning thread pool. Session creation is lazy on the audio worker, so it does not block the UI thread; the first enable has a short warmup.

A separate Python ONNX Runtime 1.19.2 experiment tested Core ML on the same small streaming graph. It fragmented into 65 supported partitions and used about 21% of one core, with median inference time 3.20 ms, versus about 3.1% for CPU execution in that prototype. This does not establish every Core ML version's performance. Shipped native inference uses ONNX Runtime 1.31.0 with the CPU provider. No claim of GPU or Neural Engine execution is made. The acceleration choice follows measured cost and audio parity rather than hardware availability alone.

The private builder pins ONNX Runtime 1.31.0's official ARM64 archive and the upstream GTCRN commit/model SHA-256. Packaging includes the runtime dependency closure, model, provenance and both projects' licenses. No model download or cloud audio transfer occurs during reception. Missing/invalid models or processing errors bypass the neural reducer and show `Unavailable; bypassed`. Builds without the optional runtime retain the existing noise reducers.

## Audio NR2 findings and practical next work

The wrapper currently batches 4,608 samples at its assumed 48 kHz rate, or 96 ms, uses int16 conversion and allocates temporary vectors on processed blocks. These are implementation costs, not requirements of OM-LSA/MCRA. A float implementation with verified gain/noise-estimator equations, smaller streaming buffers and the actual audio rate is a realistic next experiment. Retune suppression floors and speech-presence smoothing against weak consonants and fading, rather than judging success by a quieter background alone. The current lookup implementation includes a strong speech-absence floor; changing that also requires qualifying or regenerating its gain lookup.

Restart testing reproduced an ownership bug: the four numerical classes only freed pointers when they were null, and two arrays were omitted. One denoiser destruction leaked exactly 3,044,838 array bytes. Twenty resets grew resident memory by about 61 MB. Version 1.3.22 initializes owned pointers and frees all arrays. A regression fails before the fix and requires zero outstanding array bytes after each of 32 construction/process/destruction cycles. The retained speech output is byte-identical before/after the ownership correction. The existing 8,000-block AddressSanitizer test also passes. The numerical suppression parameters are unchanged.

DeepFilterNet3 is a credible higher-cost comparison candidate, with an upstream Rust/C API and pretrained weights. Its published full-band speech results are not radio qualification. The initial native research build ran out of disk space while resolving optional training dependencies; generated research-only download/cache data was removed. No DeepFilterNet quality/CPU result is claimed here. A later comparison should use a minimal inference build and independent radio speech, including multiple languages and genuine RFI/fading captures.

## Receiver qualification

Version 1.3.21 was an isolated QA build for the neural engine and CPU/scaling controls. It passed 16 File-source modulation transitions, reducer mutual exclusion, finite-audio checks, restart and native UI inspection. Version 1.3.22 adds Audio NR2 ownership cleanup and its regression; the neural DSP is unchanged. The final package passes all twelve CTests, the Brown ABI comparison and complete app signature verification.

The final four-module RTL-SDR/Radio/NR/NullAudioSink receiver also passes 16 modulation transitions, reducer mutual exclusion, finite audio and stop/start. Input is 3.2 MS/s with fourfold decimation to 800 kS/s; audio is 48 kHz. Matched full-history 3D intervals measure 17.5-17.7% of one core with audio NR off, 17.6% with Audio NR2, and 23.5% with Neural Voice NR. Median UI rates are 109-114 FPS across those intervals, with 46-47 history rows. Neural worker CPU reads 4.08%; Audio NR2 reads 0.25%. These short local results exclude the normal private decoder/plugin workload and depend on RF content, display refresh and background activity. They establish ongoing audio and responsive rendering in this setup, not live listening quality.

An eight-second Time Profiler capture targets the final 1.3.22 RF process with verified binary/module paths and a live `vmmap`. Symbolication finds the expected neural worker, frame processing, ONNX Runtime Run and FFT calls. Frame counts confirm the exercised path; they are not CPU percentages. Raw traces and private startup/installation receipts remain outside Git. Both isolated receiver processes exited cleanly after testing.

Installed 1.3.22 preserves all 106 private-profile JSON files and 17 private binary files, including retained backups, byte-for-byte during replacement. The prior 1.3.20 app is preserved under `build-beige/installed-before-1.3.22.app.backup-20261022T063244128341Z`. Normal startup reaches Ready with HTTP disabled, retains the latest saved 17.185 MHz AM tune and 2D view, and leaves Neural Voice NR off. The normal profile was not used for synthetic audio. Operator comparison of the new audio option is pending; earlier Baseband acceptance does not qualify this separate neural method.

## Custom resource folder correction (1.3.23)

Oz's first normal-profile comparison showed `Unavailable; bypassed`. The imported band-plan resources live outside the app. Version 1.3.22 searched that folder for the newly bundled model, which was absent there. Earlier isolated receiver tests used the bundle resources and missed this layout. Changing only the resource location restored Ready in the same receiver, confirming a model-discovery failure rather than an inference or audio-rate problem.

Version 1.3.23 locates the current macOS executable and prefers its own `Contents/Resources/models/gtcrn.onnx`. Custom band plans, palettes and the configured resource directory remain untouched. Unbundled/other-platform builds and bundles without that file retain the configured resource path. This lookup runs when attaching a radio; the audio processing algorithm and acceleration are unchanged.

`neural_resources_smoke.py` runs the real packaged module with a separate four-module File-source profile and a custom overlay without a model. Before the fix it reproduced the exact unavailable status and missing-file error. The standalone path test covers moved bundles, spaces, bundle preference and configured-path fallback. Private listening acceptance remains pending.

The final 1.3.23 package passes all thirteen CTests, public ABI, full signature, Python/Ruff and whitespace checks. The overlay receiver reaches Ready with zero invalid samples; all sixteen modulation transitions, mutual exclusion and restart checks pass again with that overlay. Installed live RTL-SDR reception also reaches Ready in the full normal profile with positive neural worker CPU. These checks verify processing availability, not listening quality. Both test receivers exited cleanly; no Beige instance was left running.

Installation preserved 106 JSON files, fourteen detected private Mach-O files and 23 custom resource files byte-for-byte. Private receipts, the old app backup path and logs are recorded under `/tmp/beige-neural-path-20261022/`. The model remains inside the app; no model was copied into the band-plan overlay.

## Neural level recovery and radio input range (1.3.24)

Oz rejected the first live neural comparison: quieter voice and remaining noise, worse than Audio NR2. Version 1.3.23 only fixed model discovery; it did not qualify the model's listening quality. Oz then explicitly requested automatic level recovery inside neural processing.

The native 1.3.24 engine limits the model's input RMS envelope to 0.1, then reverses that attenuation before synthesis. This keeps high radio AGC levels within a more useful model range without boosting quiet input. A regression through the actual engine originally measured relative output error 2.70 when scaling the same input by four; the normalized path now passes the 0.02 bound. Frame timing, resamplers, inference provider and FFT backends remain unchanged.

Post-reduction gain estimates attenuation in the 125-4000 Hz bins retained by the model. It holds through rejected noise/silence, smooths recovery and is bounded to 1-4x. A second 20 ms smoothing stage applies gain to the reconstructed audio. This restores wanted-speech level rather than forcing the total noisy input RMS back up. There is no extra checkbox or volume slider. The estimate is a spectral heuristic, not an independent speech detector, and cannot recover speech already removed by the model.

Sixty-four controlled cases use the three previous excerpts plus a separately held-out synthesized voice, eight conditions and two input levels. The higher level is 32x the earlier fixture amplitude, representing the range radio AGC can produce. Means exclude clean cases; each level contains 28 noisy cases:

| Input / metric | Before | After |
|---|---:|---:|
| Ordinary: wanted-speech level, dB relative to input speech | -5.38 | -0.25 |
| Higher: wanted-speech level, dB relative to input speech | -7.07 | -1.32 |
| Ordinary: eSTOI | 0.4190 | 0.4159 |
| Higher: eSTOI | 0.3118 | 0.3830 |
| Ordinary: SI-SDR, dB | 6.66 | 5.49 |
| Higher: SI-SDR, dB | 1.73 | 4.76 |
| Higher: pause noise after matching speech level, dB | -35.24 | -45.29 |
| Higher: median worker CPU, percent of one core | 3.05 | 3.07 |

The level goal improves substantially; quality gains are strongest at high input. Ordinary-level SI-SDR decreases with the time-varying recovery gain, and some individual cases regress. A 1.5x/2x sharpening of the neural spectral mask further reduced speech scores and was not shipped. These are controlled narrowband speech proxies from one human speaker and synthetic voices, not proof of better live audio than NR2. NR2 remains available and unchanged. Human listening acceptance is still required.

Raw results: `experimental/results/2026-10-22-audio/neural-level-recovery.json`. Reproduce with `neural_level_evaluate.py` and before/after native lab binaries; the broader rejected-mask study is `neural_quality_probe.py`. Private fixture audio and logs remain under `/tmp/beige-neural-quality-20261022/`.

The final package passes all fourteen CTests, ABI/signature and Python/Ruff/whitespace checks. The custom-resource receiver reaches Ready with finite audio. An isolated live RTL-SDR receiver passes sixteen mode transitions, restart and mutual exclusion; the neural interval measures 4.06% worker CPU and 104 FPS in 3D. Those brief figures exclude private plugins. Installation preserves 106 JSON files, fourteen detected private Mach-O files and 23 custom resource files; the old app and private receipt are retained.

Installed 1.3.24 also reaches Ready with active neural processing during normal-profile RTL-SDR reception. The previous audio-reducer selections were restored after that check. All Beige test instances exited cleanly and were left closed. Operator listening comparison of this correction remains pending.

Operator checkpoint, October 22: Oz confirmed restored volume, though slightly more voice level would help. He prefers the neural background-noise suppression to NR2, but hears missing letters/voice fragments and sometimes clearer audio with NR2. This partially accepts level recovery and suppression, not speech preservation. Source baseline is `6dc2bafd`; preserve 1.3.24 as a fallback before trying clarity changes. Test quieter consonants and syllable onsets at matched speech level, and measure any background-noise tradeoff explicitly.

## Optional clarity tests (1.3.25)

Before experiments, the listening checkpoint was committed in Beige `a37675ac` and electro `8d52bbe`. Oz explicitly requested separate switches and preservation of the current base settings/values as the default. Version 1.3.25 retains the 1.3.24 model, RMS envelope target, FFT/resampler geometry, automatic level estimator and gain bounds. Missing `NeuralClarity_<radio>` preferences default to zero. Each explicit selection is saved per radio.

Three independent switches appear below the enabled Neural Voice NR checkbox:

- **Speech detail:** blends up to 15% of the delay-aligned original audio around model-recognized speech. It may recover weak fragments, with some original noise during speech.
- **Syllable smoothing:** briefly retains earlier spectral gains around speech. Its original-signal contribution is bounded to 35% and fades with the speech envelope. It may reduce choppiness, with short noise tails.
- **Voice lift (+1 dB):** adds a small, smoothly applied boost around recognized speech. It does not reconstruct speech erased by the model.

All off preserves the accepted base. Changes fade without restarting inference or resetting its recurrent state. The experimental speech heuristic uses model output energy relative to input in 125-4000 Hz, with a short hold/release; it is not an independent VAD. It can miss weak onsets or mistake interference for speech. The options are listening tests, not guaranteed quality upgrades. Pauses return toward full neural suppression.

The numerical comparison uses the saved 1.3.24 native lab executable and identical inputs. Repeating that unchanged executable itself varies around 1e-6 relative RMS, so byte identity is not a valid native guarantee. All-off parity is checked against a 1e-5 relative RMS bound and 2e-5 relative peak bound; no baseline setting is changed to achieve it. Earlier byte-exact attempts and incomplete builds were discarded. The new helper test covers the exact all-off operation, bounded smoothing, speech hold/release and all eight combinations; normal/ASAN engine tests cover packet sizes, FFT parity and live option changes.

Use `neural_clarity_evaluate.py` for matched-level speech/noise metrics and `neural_clarity_smoke.py` for independent runtime toggles. The quiet-frame correlation score is a controlled reference proxy, not recognized letters or measured QSO accuracy. Private fixtures and logs: `/tmp/beige-neural-clarity-20261022/`.

All 64 comparisons pass all-off parity; maximum relative RMS difference from the saved base is 1.64e-6. The same four excerpts/eight conditions/two levels yield these mean eSTOI values (eight clean and 56 noisy cases):

| Switches | Clean | Noisy |
|---|---:|---:|
| All off / 1.3.24 base | 0.9117 | 0.3994 |
| Speech detail | 0.9427 | 0.3802 |
| Syllable smoothing | 0.9335 | 0.3779 |
| Both detail options | 0.9534 | 0.3752 |
| Voice lift only | 0.9117 | 0.3993 |

The detail options preserve more clean speech but worsen the average noisy-speech score; individual noisy cases improve. They remain optional experiments, not recommended replacements for the accepted base. Voice lift adds about 0.94 dB to the average wanted speech here. Mean offline CPU remains approximately 3.12-3.15% of one core across choices. Full measurements: `experimental/results/2026-10-22-audio/neural-clarity-options.json`.

The final package passes fifteen CTests, public ABI/signature, Python/Ruff and whitespace checks. Isolated live RF tests pass all eight switch combinations, return to baseline, finite audio and sixteen modulation transitions/restart. Native clicks verify each checkbox and the saved mask survives reopening. A subsequent RF reopen encounters RTL-SDR register USB timeouts; that additional RF restart is not counted as passing. File-source tests verify the restored mask, baseline return and all combinations independently of hardware. Listening approval of the new switches remains pending.

Installed 1.3.25 reaches Ready in the normal profile and reports all three new switches false. Its bundled model is byte-identical to 1.3.24. Installation preserves 106 JSON files, fourteen detected private Mach-O files and 23 resources. The normal-profile device endpoint subsequently reports zero RTL-SDR devices; no new normal-profile RF reception is claimed. Reconnection/Refresh and listening are manual follow-up checks. All Beige instances were closed after verification.

## Listening feedback and voice lift (1.3.27)

Oz reports that Speech detail helps but adds a little noise; Syllable smoothing initially gave no obvious difference, followed by a selection of both options in the listening response. Keep both controls available and preserve explicit saved choices. Missing preferences still leave every clarity experiment off; the accepted base model and processing values remain unchanged.

Voice lift now offers up to +3 dB around detected speech, as requested. Six controlled speech/noise cases measure 2.84-2.99 dB more wanted speech, averaging 2.93 dB. All-off output matches the saved 1.3.24 engine exactly in those six runs; prior broader testing established only floating-point repeat precision, not universal byte identity. This change raises level and does not recover letters removed by the model. Clarity and smoothing algorithms are unchanged.

All eight combinations deliver finite audio with the custom resource overlay in the File receiver and the installed 1.3.27 RTL-SDR receiver. RF median UI rate is 95.9 FPS in 3D; measured neural worker CPU varies from 4.1% to 7.7% of one core during these short intervals. This is not a matched performance comparison or listening acceptance. The bundled model remains byte-identical to 1.3.25; installation preserves all 141 checked profile/config/plugin/resource files.

The initial normal-profile launch encountered a separate CoreAudio wait while PortAudio probes BlackHole 2ch. A standalone PortAudio program reproduced it outside Beige at both 8 kHz and 48 kHz. The bundled PortAudio and legacy Audio sink binaries are byte-identical to 1.3.25. Oz restarted the macOS audio service; the standalone probe then passed every device/rate and the normal profile launched successfully. Its saved Audio/BlackHole routing works, live RF and waterfall updates resume, Neural reaches Ready, and the +3 dB label is verified in the UI. The original neural enable selection is restored, a normal-profile stop/restart/stop passes, and the app exits cleanly. No sink preference or audio binary was changed for this recovery. All Beige instances are closed after testing.

## Reproduction and sources

```bash
python3 beige/build_neural_audio.py build-beige
bash beige/build-macos.sh
cmake --build build-beige --target beige_audio_nr_lab
python3 beige/tests/audio_nr_smoke.py --url http://127.0.0.1:18873 \
  --pid <verified-isolated-receiver-pid> --output /tmp/audio-rf.json
```

- [GTCRN official implementation, model and streaming inference](https://github.com/Xiaobin-Rong/gtcrn).
- [RNNoise official source and model training description](https://github.com/xiph/rnnoise).
- [OM-LSA/MCRA original paper](https://webee.technion.ac.il/Sites/People/IsraelCohen/Publications/sp_Nov2001.pdf).
- [DeepFilterNet official implementation and C API](https://github.com/Rikorose/DeepFilterNet); [published speech enhancement results](https://arxiv.org/abs/2305.08227).
- [ONNX Runtime Core ML provider, supported operators and configuration](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html).
- [Open Speech Repository source and attribution](https://www.voiptroubleshooter.com/open_speech/american.html).
