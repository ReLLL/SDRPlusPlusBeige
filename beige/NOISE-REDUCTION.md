# Noise reduction and Apple math

Read when: changing Baseband NR, FFT backends, noise-reduction settings or quality tests.

## Baseband NR

Beige 1.3.15 replaces the wideband LogMMSE path with a bounded complex-IQ spectral suppressor. It preserves phase, uses decision-directed Wiener gains, a two-scale local lower-quartile noise estimate, fast signal attack and slower gain release. The broader estimate protects occupied SSB/AM channels from contaminating the local floor. Persistent narrow carriers are excluded from the floor at startup. This is a conventional DSP algorithm, not a speech-only neural model or a full MCRA implementation.

The control limits attenuation to 0-24 dB. The default is 12 dB to protect weak signals. More suppression can increase distortion: the weak-SSB fixture improves most near 12 dB, not at its highest setting. Zero dB reconstructs the original IQ at unity level with the processing delay. The old unconditional 4x output boost is removed, so enabling NR no longer adds 12 dB to the displayed signal level.

Sine analysis/synthesis windows, 50% overlap and inverse-FFT normalization give unit squared overlap. Input and output counts always match, including tiny and irregular source blocks. Startup delay is one FFT length, usually 5-11 ms, instead of an initial 280 ms noise capture. The FFT length is a power of two from 256 to 32768; storage is bounded independently of running time. Buffers and plans are reused. Lower-quartile estimates refresh every four hops; gain and noise smoothing continue every hop. Retuning, start/restart, sample-rate and backend changes discard stale history. Backend changes can produce a brief startup gap.

Settings are in Beige's existing noise-reduction config: `IFNR`, `BasebandReductionDB` and `BasebandAcceleration`. They preserve existing enable states. Transmission holds noise adaptation. Control flags and published status cross the worker boundary through atomics. The existing CPU protection uses processing time relative to received IQ duration.

No spectral suppressor can reliably distinguish every wanted signal from overlapping noise. The fixtures below do not establish general speech intelligibility or digital decode-rate improvements. Use a lower strength or bypass for marginal digital signals; retain unprocessed IQ when recording measurements.

## Acceleration and fallback

Baseband NR defaults to automatic Apple Accelerate on macOS. Its checkbox selects Apple FFT, magnitude-squared and gain-vector kernels when supported. Disabling it selects FFTW. Non-Apple builds also use FFTW. Failure to create an Apple setup, or a length unsupported by its radix-2 API, selects FFTW automatically.

The shared array FFT wrapper uses the same validated backend, removes per-transform resize allocation, and correctly normalizes inverse transforms. Existing explicit spectrum FFT settings remain respected; a new Mac profile defaults to Apple FFT. These shared routines also serve the legacy channel LogMMSE code. Its background-noise histogram now handles silence, constant/nonfinite data and the maximum-bin endpoint without indexing past its bucket array.

Apple Accelerate provides CPU vector acceleration; this change does not claim Metal GPU or Neural Engine execution. Small streaming FFTs fit this path without GPU synchronization or a neural speech model that could discard CW/data signals. The existing fixed-point Audio NR2 engine and its 1.3.14 heap-corruption correction remain in place.

Primary API reference: [Apple Accelerate](https://developer.apple.com/documentation/accelerate), [vDSP](https://developer.apple.com/accelerate/).

## Repeatable checks

`beige_baseband_nr_test` covers Apple/FFTW agreement, unsupported-length fallback (7680), forward/inverse reconstruction, zero-dB unity gain, irregular block partitions, 48/192/256/768 kS/s and 2.4 MS/s, reset/rate/backend changes, silence, nonfinite input, colored-noise changes and SSB/AM/NFM/BPSK/CW waveforms. `beige_baseband_nr_asan` runs the independent numerical path with AddressSanitizer. The core-linked comparison exits after all local denoisers are destroyed because the GUI core's global services require application-owned shutdown.

Commands:

```
ctest --test-dir build-beige -R baseband --output-on-failure
build-beige/beige/tests/beige_baseband_nr_test legacy 192000 4
build-beige/beige/tests/beige_baseband_nr_test apple 192000 4
build-beige/beige/tests/beige_baseband_nr_test portable 192000 4
```

Synthetic results on this Mac, October 22:

- Default 12 dB setting: 11.66 dB full-band residual-noise reduction around a carrier; wanted-tone level changes by -0.029 dB. The legacy normalized path suppresses this easy fixture more strongly (17.33 dB). The new default deliberately caps attenuation.
- SSB/AM/NFM/BPSK/CW fixtures: 11.02-11.61 dB full-band error reduction; clean-signal EVM 0.0008-0.104%. These include out-of-channel noise, so they are not channel SNR claims.
- Weak SSB, approximately 0 dB channel SNR, measured only in the 300-3000 Hz USB passband: 4.43/5.58/5.42 dB error reduction at 6/12/18 dB settings.
- Colored noise: 7.99 dB attenuation before, 8.41 dB after a fourfold amplitude increase and settling.
- Repeat benchmark at 192 kS/s: Apple 1.20%, FFTW 1.35% of one real-time core; Apple at 768 kS/s: 5.24%. Initial pre-change legacy measurement: 2.30% at 192 kS/s. Measurements vary with thermal state and profiling overhead; they exclude source/UI/audio costs.
- A Time Profiler capture targeted the legacy harness; native sampled stacks verified both actual LogMMSE processing and the new adaptive path through libvDSP. Profiled runs measured 3.20% legacy and 1.22% new at 192 kS/s. The legacy profile uses the corrected shared FFT wrapper, so this is not an untouched-binary comparison.

Runtime QA uses a separate profile with synthetic IQ and NullAudioSink. The normal profile must not receive synthetic data. Listening quality on live RF still needs the operator's judgment.
