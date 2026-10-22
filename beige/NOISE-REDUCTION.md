# Noise reduction and Apple math

Read when: changing Baseband NR, FFT backends, noise-reduction settings or quality tests.

## Selection priorities

Oz prioritizes NR quality, clean audio, low CPU usage and high UI/waterfall frame rate. There is no preferred hardware API. Select vDSP, VOLK/SIMD, FFTW, Metal or another available method using numerical equivalence, listening/decoder checks and complete receiver measurements. Availability alone is not a performance result. Keep the noise model independent of its math backend and retain a portable fallback. A new denoising algorithm needs separate quality qualification.

Close all Beige instances when testing is finished unless Oz explicitly requests leaving the receiver open.

## Current receiver: 1.3.17

Oz reported that the replacement Baseband NR in 1.3.15 sounded distorted and looked slower. Its synthetic improvements did not qualify its live sound. Version 1.3.16 restores the established LOGMMSE algorithm, noise estimation, overlap behavior, startup capture and original 4x output level. The new attenuation slider is removed. The replacement engine is retained only under `beige/experimental/` for research and is not included by the production module.

Do not describe the experimental engine's synthetic error reduction as an improvement in the installed receiver. New NR algorithms need operator listening and weak-signal decoding checks before replacing the established path.

## Optional math backend

The separate Automatic acceleration checkbox selects Apple Accelerate FFT for supported power-of-two lengths. Unsupported lengths or an unavailable Apple setup use the original FFTW planner automatically. Disabling the checkbox selects FFTW. The actual backend appears under Baseband NR. Settings use `IFNR` and `BasebandAcceleration`; the unused 1.3.15 strength key is ignored.

For example, the current 800 kS/s receiver uses a 32000-point NR FFT, so it correctly reports FFTW even with Automatic acceleration checked. A 204800 S/s source produces an 8192-point NR FFT and uses Apple Accelerate. The spectrum's explicit FFT preference remains separate and preserved.

The shared FFTW implementation again uses its original out-of-place plan and detected SIMD alignment. The 1.3.15 generic in-place, unaligned replacement is no longer used by the receiver's array FFT path. An early benchmark showed a slowdown at the 65536-point display size; a later comparison with equal input copying did not consistently reproduce it. That fallback change is not an established explanation for the whole UI slowdown. Timings depend on transform length and machine load.

Apple Accelerate provides CPU vector acceleration; this change does not claim Metal GPU or Neural Engine execution. Existing VOLK math also selects supported SIMD kernels. The fixed-point Audio NR2 engine and its 1.3.14 heap-corruption correction remain in place.

Primary API reference: [Apple Accelerate](https://developer.apple.com/documentation/accelerate), [vDSP](https://developer.apple.com/accelerate/).

## Stability and numerical checks

The stream wrapper reads once per block, publishes worker status through atomics and resets before launching the worker. Original NR output counts and level are preserved. The numerical seam in `classic_nr.h` bounds each output to the stream buffer capacity without changing ordinary frame processing. Callback removal uses the correct radio interface name and unregisters tune/module hooks.

The legacy background-noise histogram handles silence, constant/nonfinite data and the maximum-bin endpoint without indexing past its bucket array. This is a memory-safety correction, not a redesigned suppression model.

The original-path comparison uses the same deterministic IQ, rates and source block partition as the old implementation. At 192 kS/s, both forced FFTW and automatic fallback match the reference exactly. At 204800 S/s, FFTW matches exactly and the supported Apple backend differs by at most 3.73e-7 in the tested IQ samples. The comparison includes the restored 4x output scaling. This validates numerical equivalence on the fixtures, not every live signal.

Version 1.3.17 fixes two separately reproduced audio faults. A silent initial capture previously produced zero noise power and invalid samples through 0/0 SNR divisions. A power floor of 1e-20 keeps these calculations finite. The exponential-integral lookup now checks its domain and upper bound before indexing. Ordinary RF noise floors and the established NR algorithm are preserved.

Changing Automatic acceleration now replaces only the FFT plans. It preserves pending samples, overlap and learned noise. Previously every change restarted the initial capture, dropping samples even when both settings selected FFTW. The regression switches repeatedly through the actual `IFNRLogMMSE::process` wrapper and requires unchanged output counts and relative RMS error below 1e-4. FFTW array planning/destruction is serialized across display and NR workers; transform execution remains independent.

Production-path fixtures cover silence, modulated signals and silence followed by a signal at 204800, 819200 and 800000 S/s, using ordinary and fragmented blocks. All outputs remain finite. The largest Apple/FFTW relative RMS difference was 8.99e-7; unsupported-length fallback was exact. These are numerical checks, not listening acceptance.

## UI performance and measured math cost

Sampling the old receiver showed the main thread waiting inside `glBufferSubData` while the Metal-backed OpenGL driver retired previous geometry. The macOS renderer now supplies fresh buffer storage per draw list using `glBufferData`. This preserves the existing mesh resolution, history and waterfall. Other platforms retain their upload path.

In the isolated 1920x1080 File/Radio/NR/NullAudioSink workload, the final full-history 3D/FFTW interval rose from 68.84 to 87.03 FPS. The 3D/Apple interval rose from 61.80 to 81.91 FPS. The source runs faster than real time, so this is a stress comparison, not an RF frame-rate guarantee. Samples remained active with both math backends and both view modes. Short process samples showed 99 upload-wait observations before the change and two buffer-upload observations afterwards; sample counts alone do not establish a universal speedup.

The actual array-plan benchmark measured Apple FFT at 18.1 us versus FFTW 44.8 us for 8192 points, and 212 us versus 1326 us for 65536 points. Whole-NR cost for one second of IQ was about 20 ms versus 27 ms at 204800 S/s, and 83 ms versus 143 ms at 819200 S/s. The usual 800000 S/s case used the same FFTW backend in either setting, about 98-100 ms. Results are local measurements, include copies, and vary with load. Apple math was not slower in these supported-size comparisons.

Commands:

```
ctest --test-dir build-beige -R baseband --output-on-failure
build-beige/beige/tests/beige_baseband_nr_test
build-beige/beige/tests/beige_math_backend_test
build-beige/beige/tests/beige_math_backend_test --benchmark
python3 beige/tests/noise_reduction_smoke.py --url http://127.0.0.1:18867
python3 beige/tests/math_performance_smoke.py --url http://127.0.0.1:18867
```

Receiver smoke tests require an isolated four-module File/Radio/NR/NullAudioSink profile. The standard 256 kS/s fixture has an unsupported 10240-point NR FFT, so both automatic and forced modes must report FFTW and continue delivering samples. Stop/start requests wait for actual playback state before continuing.

## Rejected experiment

The experimental engine used complex-IQ spectral masks, short overlapping sine windows and a local quantile noise estimate. It passed synthetic waveform, channel error, silence, rate-change and backend checks, including independent AddressSanitizer and no-Apple builds. A weak-SSB synthetic fixture showed 5.58 dB less in-channel error at its 12 dB setting. Oz nevertheless heard distortion and perceived a slowdown in live operation.

Those tests remain research fixtures, with no production include of the experimental header. They must not stand in for listening tests or real decoder success rates. No general NR quality improvement is claimed for the corrective release.
