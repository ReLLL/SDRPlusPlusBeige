# Baseband NR research: October 22, 2026

Read when: changing the noise estimator or suppression gain, evaluating a new NR algorithm, or reproducing the offline comparisons.

Study snapshot before the implementation work. References below to unchanged installed 1.3.17 describe that study period. Subsequent [1.3.20 receiver qualification](../BASEBAND-MODES.md) promotes the frame-clock/Wiener candidate for automatic SSB voice while retaining Original and bypass choices. Raw measurements below remain the original offline results.

The best speech candidate in this experiment combines a fixed FFT-frame noise-update clock with Wiener gain. On ten held-out cases it increased mean eSTOI by 0.0165 at 800 kS/s and 0.0166 at 204.8 kS/s over the installed algorithm. At 800 kS/s with 1,984-sample packets, NR processing used 8.36% of one M2 Max core versus 13.23% for the reference, a 36.8% reduction. These are controlled offline measurements. Neither live audio quality nor UI frame rate has been qualified for this candidate. Installed Beige 1.3.17 is unchanged.

## What Beige uses

Baseband NR is Brown's adaptive LogMMSE implementation, derived from [rajivpoddar/logmmse](https://github.com/rajivpoddar/logmmse/). It estimates the noise spectrum, estimates signal-to-noise ratio in each FFT bin, applies a real gain to complex IQ, then overlap-adds the inverse transforms. It runs before channelization and affects the entire received bandwidth. [Brown documents LogMMSE and its separate OMLSA/MCRA Audio NR2](https://sdrpp-brown.san.systems/).

The actual reference is `misc_modules/noise_reduction_logmmse/src/logmmse.h`, through the production `classic_nr.h` wrapper. At 800 kS/s it has:

- A 20 ms Hann window, 10 ms hop and 32,000-point zero-padded FFT. Bin spacing is 25 Hz; zero-padding does not provide independent 25 Hz spectral resolution.
- A 280 ms startup buffer. Twelve nonoverlapping 20 ms magnitude spectra initialize the noise estimate; a 120-bin moving average is squared. Speech present during startup can enter the noise estimate.
- Brown's adaptive wideband noise model: 200 frames of magnitude/deviation history, a background-deviation histogram and interpolation across occupied bins.
- Decision-directed prior SNR smoothing of 0.98, minimum prior SNR of -25 dB and posterior SNR capped at 40 in linear units.
- Gain `G = xi/(1+xi) * exp(E1(v)/2)`, where `v = gamma*xi/(1+xi)`. The wrapper retains the original 4x output level.

Original SDR++ also has a different [FM IF reducer](https://raw.githubusercontent.com/AlexandreRouma/SDRPlusPlus/master/core/src/dsp/noise_reduction/fm_if.h), using a small sliding FFT and strongest-bin selection. That is not Beige's wideband LogMMSE. SDR# documents NINR IF/AF controls in its [official guide](https://airspy.com/downloads/SDRsharp_Big_Book_2024.pdf), linked from [Airspy downloads](https://airspy.com/download/). Its implementation was not inspected or benchmarked here; no claim that SDR# uses this LogMMSE code.

## Changes tested

Fourteen experimental candidates retain the original window, hop, transform and output level. `clone` is an additional unchanged control: every scored reference case requires bit-identical output between the experimental clone and actual production code. No production module includes `logmmse_variants.h`.

Development used 20 cases at 204.8 kS/s with Apple FFT: one recorded human voice and one synthetic male voice, each with white noise at -5/0/10/30 dB, changing noise level, colored noise, fading, impulses, a strong adjacent carrier and a drifting in-band tone. All deltas below are against production, averaged over those cases. Positive eSTOI and SI-SDR deltas are better. eSTOI is a 0-1 intelligibility proxy; SI-SDR measures reconstruction error after fitting one overall signal gain.

| Candidate | Change | Mean eSTOI delta | Worst delta | Mean SI-SDR delta, dB |
|---|---|---:|---:|---:|
| `cap` | Limit LogMMSE gain to 1 | -0.0001 | -0.0030 | +0.03 |
| `fast` | Prior smoothing 0.94 | +0.0050 | -0.0048 | -0.50 |
| `smooth` | Time/frequency gain smoothing and floor | -0.0600 | -0.1018 | -4.49 |
| `bias` | Noise-power correction by 4/pi | -0.0066 | -0.0130 | -0.08 |
| `bootstrap` | Local lower-quartile startup estimate | +0.0022 | -0.0006 | +0.48 |
| `tracker` | Gain-coupled soft-presence noise recursion | -0.0241 | -0.0634 | -0.08 |
| `gh` | Fixed-prior speech-presence noise recursion | -0.0242 | -0.0792 | -0.14 |
| `wiener` | Gain `xi/(1+xi)` | +0.0094 | -0.0103 | +0.18 |
| `sqrtwiener` | Square root of Wiener gain | -0.0002 | -0.0211 | -1.44 |
| `balanced` | Lower-quartile startup plus smoothing 0.96 | +0.0076 | -0.0008 | +0.26 |
| `framewise` | Update noise once per FFT frame | -0.0001 | -0.0043 | -0.12 |
| `blend95` | Mix 5% unprocessed IQ | -0.0031 | -0.0077 | -0.20 |
| `blend85` | Mix 15% unprocessed IQ | -0.0103 | -0.0221 | -0.77 |
| `framewiener` | Frame clock plus Wiener gain | Held-out suite only | | |

The simple tracker and `gh` reduced CPU but lost speech quality. The time/frequency smoother was the largest regression. Neither is recommended. Faster prior tracking traded reconstruction quality for a small intelligibility gain. Dry mixing did not improve the metrics. Startup protection alone had a small positive result.

The `tracker` is not full IMCRA. The `gh` experiment independently implements equations 18 and 22-24 of [Gerkmann and Hendriks, 2012](https://uol.de/f/6/dept/mediphysik/ag/speech/download/paper/gerkmann_unbiasedMMSE_TASL2012.pdf), with a different startup estimate and complex-wideband input. These negative results concern this RF adaptation, not the paper's speech results. More complete [IMCRA noise tracking](https://webee.technion.ac.il/Sites/People/IsraelCohen/Publications/SAP_Sep2003.pdf) and [OM-LSA speech-presence estimation](https://webee.technion.ac.il/Sites/People/IsraelCohen/Publications/SPL_Apr2002.pdf) remain possible, untested replacements.

## Held-out speech results

Candidates were fixed before running ten new cases: unused sentences from the human recording and a new synthetic female voice, with different random seeds, white noise at -3/+5 dB, changing noise, colored noise and fading at +5 dB. The first comparison used 204.8 kS/s and 1,024-sample packets. This checks limited generalization; it is not a large independent speech corpus.

| Candidate | Mean eSTOI delta | Worst delta | Mean SI-SDR delta, dB | NR CPU ratio to reference at 204.8 kS/s |
|---|---:|---:|---:|---:|
| Wiener | +0.0181 | -0.0025 | +0.27 | 0.867 |
| Frame clock | -0.0012 | -0.0086 | -0.20 | 0.854 |
| Frame clock + Wiener | +0.0166 | -0.0027 | +0.02 | 0.733 |
| Startup + 0.96 prior | +0.0050 | -0.0019 | -0.07 | 0.989 |

Wiener improved eSTOI in nine of ten cases, with a small loss on one colored-noise example. Gains were modest. Standard STOI improved less: +0.0034 for Wiener and +0.0012 for frame-clock/Wiener. Neither metric establishes the absence of watery or musical artifacts. The combined candidate's average SI-SDR was effectively unchanged.

Repeating the same ten scenario definitions at 800 kS/s with 4,000-sample packets exercises the actual 32,000-point FFTW fallback instead of the supported 8,192-point Apple FFT. Candidates were not retuned. This is a rate/backend cross-check, not ten additional independent speech examples.

| Candidate at 800 kS/s | Mean eSTOI delta | Worst delta | Mean SI-SDR delta, dB |
|---|---:|---:|---:|
| Wiener | +0.0180 | -0.0005 | +0.26 |
| Frame clock | -0.0012 | -0.0072 | -0.18 |
| Frame clock + Wiener | +0.0165 | -0.0006 | +0.04 |
| Startup + 0.96 prior | +0.0045 | -0.0041 | -0.06 |

The direction and size of the speech results persisted at 800 kS/s. These tests still exclude complete receiver operation and human listening.

## Packet timing and CPU

Production updates the wideband noise model once per processing call, including calls without a new FFT frame. Multiple FFT frames in one call share a single update. The background histogram therefore advances according to source packet partitioning. Moving that update inside the FFT loop removes repeated work on unchanged history and supplies the same history sequence for every packet size.

On identical changing-noise IQ, 317-sample versus 4,096-sample packets produced a relative RMS output difference of 0.4952 in production. `framewise` and `framewiener` were bit-identical across these partitions. Independent numerical checks also passed at 48, 204.8 and 800 kS/s, with Apple math both enabled and disabled. A timing correction changes adaptation behavior, so it still needs receiver qualification.

Dedicated CPU measurements: Apple M2 Max, median of three runs, alternating candidate order, four seconds of recorded speech with white noise. Percentages are NR processing CPU time divided by input duration, as a percentage of one core. File I/O, Python scoring, channelization, display and audio output are excluded.

| Rate / packet samples | Production | Wiener | Frame clock | Frame clock + Wiener | Startup + 0.96 |
|---|---:|---:|---:|---:|---:|
| 204.8 kS/s / 1,024 | 2.12% | 1.87% | 1.83% | 1.56% | 2.09% |
| 800 kS/s / 4,000 | 10.49% | 9.54% | 9.09% | 8.17% | 10.50% |
| 800 kS/s / 1,984 | 13.23% | 12.21% | 9.28% | 8.36% | 13.12% |

The 1,984-sample case approximates RTL input packets after decimation from 3.2 MS/s: the source requests 15,872 bytes, giving 7,936 complex samples, then fourfold decimation gives 1,984. Lower NR CPU is measured; a resulting UI FPS increase is not. Short runs, background load and CPU frequency affect timings. Startup is included, so this is not an indefinite steady-state benchmark.

## CW, AM and digital checks

These are separate, single-seed guard fixtures at 204.8 kS/s. All outputs use a common length.

- CW: frame-clock/Wiener preserved wanted tone level within 0.05 dB of production and gave essentially the same channel reconstruction score, 13.05 versus 13.08 dB. `balanced` scored 14.15 dB. No Morse decoder or human copy test was run.
- AM: `balanced` improved eSTOI from 0.5317 to 0.5542 and SI-SDR from 12.22 to 13.74 dB. Frame-clock/Wiener barely changed eSTOI, 0.5323, while SI-SDR fell to 11.75 dB. A speech candidate should not silently replace AM processing on this evidence.
- Generic 100-baud BPSK at Eb/N0 = 2 dB: NR off made 22 errors out of 698 bits; production made 31; frame-clock/Wiener made 29; `balanced` made 30. Raw matched-filter detection is appropriate for this fixture. The bit count is too small for a general BER ranking. No FT8, FT4, WSPR or FreeDV decoder benefit is established.

## Practical direction

1. Separate the packet-clock correction from the gain change. It has a reproducible partitioning fault and measurable CPU benefit. Qualify startup, Hold/reset, backend changes, live sources and GUI timing before promotion.
2. Evaluate optional Wiener gain for SSB voice, retaining the accepted LogMMSE choice. It removes the per-bin exponential-integral/exponential operations and improved held-out speech eSTOI. Include weak consonants, deep fading, colored noise and matched-level listening. Do not make it a universal default from these results.
3. Explore startup protection plus a slightly faster prior for AM/CW separately. Its larger AM benefit came from one fixture and needs more recordings. Full IMCRA or signal-presence tracking should proceed only with broader data; the cheaper recursions tested here regressed.
4. Keep digital modes able to bypass NR. Cleaner-looking spectrum alone does not demonstrate more decodes.
5. Optimize allocation and noise-history work before adding GPU transfers. Reuse buffers and vectorize loops while requiring output equivalence. The 800 kS/s transform currently uses FFTW; 204.8 kS/s uses validated Apple Accelerate. These experiments preserve that selection. No Metal or Neural Engine speed claim is made.

For larger voice-quality improvements, an additional per-VFO audio stage is more realistic than a speech model over whole-band IQ. [RNNoise](https://github.com/xiph/rnnoise) or [DeepFilterNet](https://arxiv.org/abs/2110.05588) could be evaluated after voice demodulation, with explicit bypass for CW/data and bounded resampling/latency. Neither was installed or tested here. Start with CPU/SIMD profiling; CoreML/Metal offload needs measured end-to-end benefit on the available Mac, including transfers and scheduling.

## Reproduction and artifacts

The lab is an explicit `EXCLUDE_FROM_ALL` CMake target. It neither opens a GUI nor reads a receiver profile. The standalone AddressSanitizer lab instruments the experimental code; the linked core is not wholly instrumented. Both normal and ASAN lab self-tests pass. All seven existing CTests pass. Python lint/compilation and whitespace checks pass.

Input speech: [Open Speech Repository](https://www.voiptroubleshooter.com/open_speech/american.html), `OSR_us_000_0010_8k.wav`, plus locally generated macOS Daniel and Samantha voices. Attribution: Open Speech Repository. The original host returned HTTP 406; the recording was obtained from [this redistributed copy](https://raw.githubusercontent.com/hrtlacek/td_mfcc/master/OSR_us_000_0010_8k.wav). Its SHA-256 is `a4bf9becd046d7aedb6d05b6e12347a6294a44f74d263089c636fb0a2b1e6561`; identity against an original-host download was not verified. No real RF capture was used. Preserve the exact synthetic speech files because system voices may change between macOS versions.

The evaluation uses complex USB speech at +16 kHz with controlled wideband interference, identical demodulation, resampling and 300-2,700 Hz audio filters. This deterministic demodulator does not model the receiver's complete AGC/filter/audio chain. Scoring excludes the first 0.4 seconds and final 20 ms. Only the established 4x wrapper level is removed. SI-SDR and STOI/eSTOI do not reward simple overall attenuation. Evaluation dependency [pystoi 0.4.1](https://github.com/mpariente/pystoi) is MIT-licensed and isolated from app dependencies. No human listening approval was recorded.

Durable results are under [`results/2026-10-22`](results/2026-10-22/): 521 measurement rows, all 45 dedicated CPU runs, paired summaries, hashes and provenance. The initial pilot incorrectly ignored a returned gain-cap array; it was aborted, fixed and excluded from every result reported here. The unchanged-clone check guards the subsequent measurements.

Local exact input copies are in ignored `build-beige/nr-research/data/`. Locally generated listening WAVs are in `build-beige/nr-research/listening/`. The comparison order is noisy, production, frame-clock/Wiener, then `balanced`, with half-second gaps. These clips match projected wanted-speech level and use one common headroom gain; this listening normalization does not alter the scored metrics. They illustrate one development fixture, not the held-out evaluation.

Run from the repository root. Substitute a writable research data directory containing those three exact input files; the original run used `/tmp/beige-nr-research-20261022` and an isolated venv with host numpy/scipy/soundfile plus `pystoi==0.4.1`.

```bash
cmake --build build-beige --target beige_nr_lab beige_nr_lab_asan -j4
build-beige/beige/tests/beige_nr_lab --self-test
build-beige/beige/tests/beige_nr_lab_asan --self-test
ctest --test-dir build-beige --output-on-failure

task_data=/tmp/beige-nr-research-20261022
task_python="$task_data/venv/bin/python"
task_runner=build-beige/beige/tests/beige_nr_lab
"$task_python" beige/experimental/nr_evaluate.py --runner "$task_runner" --data "$task_data" --output "$task_data/speech-results.json"
"$task_python" beige/experimental/nr_evaluate.py --runner "$task_runner" --data "$task_data" --modes reference gh wiener sqrtwiener --output "$task_data/speech-more-results.json"
"$task_python" beige/experimental/nr_evaluate.py --runner "$task_runner" --data "$task_data" --modes reference framewise balanced --output "$task_data/speech-timing-results.json"
"$task_python" beige/experimental/nr_evaluate.py --runner "$task_runner" --data "$task_data" --holdout --modes reference wiener framewise framewiener balanced
"$task_python" beige/experimental/nr_evaluate.py --runner "$task_runner" --data "$task_data" --rate 800000 --holdout --modes reference wiener framewise framewiener balanced --output "$task_data/holdout-800k-results.json"
"$task_python" beige/experimental/nr_guardrails.py --runner "$task_runner" --data "$task_data" --modes reference wiener framewise framewiener balanced bootstrap
"$task_python" beige/experimental/nr_benchmark.py --runner "$task_runner" --data "$task_data"
"$task_python" beige/experimental/nr_listening.py --runner "$task_runner" --data "$task_data" --output build-beige/nr-research/listening
"$task_python" beige/experimental/nr_summarize.py --data "$task_data" --output beige/experimental/results/2026-10-22
```

Do not run CPU comparisons concurrently with builds, test suites or other benchmark processes. Do not tune candidates against the held-out set and continue calling it held-out evidence. A production decision still requires live listening, decoder checks where relevant, and complete receiver CPU/FPS measurements.
