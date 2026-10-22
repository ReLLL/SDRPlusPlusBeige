# Beige validation

Read when: verifying the spectrum renderer, imported plugins or a packaged build.

## Automated gate

Audio NR2/neural audio qualification for 1.3.22 is in [AUDIO-NR.md](AUDIO-NR.md). The twelve-test gate includes normal/ASAN neural packet/backend/rate/bypass checks and an exact array-ownership regression for repeated Audio NR2 resets. Operator listening remains separate from controlled speech metrics.

- Build all enabled stock modules and the core together.
- `ctest --test-dir build-beige --output-on-failure`.
- `python3 beige/check_plugin_abi.py`.
- `python3 -m py_compile beige/package_macos.py beige/import_brown.py beige/check_plugin_abi.py`.
- Package, verify the complete signature and inspect dependency paths.

Modulation-aware Baseband NR qualification and per-build evidence are in [BASEBAND-MODES.md](BASEBAND-MODES.md). Version 1.3.20 passes nine CTests, 48 File-source mode transitions across Apple/FFTW rates and eight final-build RTL-SDR mode transitions. Profile persistence, finite audio, bypass, public plugin ABI, package signature and normal-profile RF startup pass. Oz subsequently compared Automatic with Original on voice and confirmed clearer audio with no distortion; responsiveness was not separately confirmed in that answer.

History tests cover newest-first order, live front updates, bounded retention, narrow-peak preservation, irregular gaps, clock reversal, invalid samples, small input widths and perspective direction.

## Runtime checks

Use a separate QA profile and deterministic synthetic IQ. Verify 3D by default, 2D/3D switching, saved preference after reopening, palette behavior, retuning, resizing, source stop/start and history reset. The normal receiver profile must never be used for synthetic reception tests.

Compare the same running receiver in both display modes. Record median, mean and 95th-percentile spectrum CPU drawing times and UI frame rate. Check that unchanged frames reuse the mesh. Check bounded vertex/index counts. These measurements do not measure total radio processing or GPU latency.

Initial local measurements on synthetic 256 kS/s IQ, a 65,536-point FFT and the standard 1280-pixel window: 2D mean 0.037 ms; 3D mean 0.109 ms, median 0.040 ms, 95th percentile 0.391 ms. These were measured before the final plugin migration; repeat after renderer changes. History ingestion for an 8,192-bin FFT measured about 16 microseconds per update in the standalone run; storage was 49,696 bytes.

For an imported profile, compare every module instance and enabled state, all frequency records, selected band plan and its files. Confirm every imported custom plugin and hardware module loads. Confirm notebooks use Beige data folders and database counts survive import. Live RF/device operation needs the corresponding connected hardware; synthetic display tests do not prove RF reception.

## October 18 installation checks

The imported profile preserved 44 module-instance settings and all five custom plugin paths. All five custom plugins loaded and instantiated, retaining their original enabled/disabled states. Original Brown bundled modules are included alongside newly built Beige modules. Five stale module-instance names already lacked matching binaries in Brown (Audio Source, Brown Audio Sink, Harogic, Perseus and USRP); their saved configuration remains preserved.

All 472 General bookmarks were returned by the live Frequency Manager. Frequency-manager bytes and all custom band-plan resource files matched Brown. The selected Oz HAM First plan remained selected. SQLite backup retained 131,274 decodes. Notebook roots, CSV settings and CW log paths were redirected to Beige's private profile.

After preserving the host ABI, a second synthetic IQ comparison measured 2D mean 0.034 ms and 3D mean 0.131 ms; 3D median 0.038 ms, 95th percentile 0.393 ms. Median UI frame rates were about 93 and 95 respectively in this run. This is a local CPU drawing measurement with synthetic IQ, not a general GPU or RF performance guarantee.

The installed 1.3.3 build reopened the QA profile with its saved 2D setting. The imported profile opened in 3D, loaded all five custom plugins, returned 472 bookmarks and reported the Beige DX CSV path with no notebook error. No live RF transmission or reception test was performed.

## Depth motion and detail correction

The 1.3.3 reproducer held retained-row depth unchanged between FFTs, then jumped 50 ms on the next FFT. Its 192-column front spaced samples about 20 pixels apart across a 3,840-pixel pane. Subsampling 64 history rows to 48 also changed selected rows while filling the history.

The correction uses display-time projection, fixed capture timestamps, every retained row and width-dependent frequency detail. Regression tests cover movement between FFTs, continuous position through a ring rollover, a fixed live front, narrow-peak separation, joined frequency grids and the complete 16-bit mesh budget. The history test measured about 9.3 microseconds per 8,192-bin update, using 1,049,120 bytes of fixed history storage.

Synthetic IQ measurements during development: 1,280-pixel window mean spectrum CPU drawing time 0.394 ms, 95th percentile 1.097 ms, median UI rate 74.6 fps. The preceding build measured 0.145 ms mean and 72.9 fps in that session. A 1,512-pixel window measured 0.598 ms mean, 2.018 ms at the 95th percentile and 99.9 fps. Different display refresh rates prevent comparing those two window sizes by frame rate. These timings exclude GPU execution. An eight-second Time Profiler capture targeted the exact QA process and confirmed spectrum drawing frames; only environment-free sample exports are retained.

The QA profile's VFO must remain inside the synthetic recording bandwidth. A stale near-edge VFO offset triggered the existing signal-info bounds bug in both the old and revised builds; resetting that isolated profile's VFO to the center removed the unrelated failure.

## HAM toolbar (October 20)

The final 1.3.8 package passed all 16 band jumps in free mode and all 16 in center mode. Each jump set the exact chart-based frequency, spectrum center and zero VFO/view offsets. The saved mode and single-click behavior stayed unchanged. Each case began with a VFO move: free mode retained an offset; center mode followed it by retuning. Toolbar rendering was inspected in wide and 1,280-pixel windows, including the two-row layout, selected outline and readable tooltip.

History tests, public plugin ABI comparison, build, Python smoke-script compilation, whitespace check and complete app signature verification passed. Removal of CW Notebook and Advanced FT8/FT4 changed only plugin load paths and their two module-instance entries in the active profile. The previous configuration and plugin payloads were backed up; notebook data was preserved.

## Band information tooltips (October 20)

Build 1.3.11 adds formatted operating-time, typical-use and approximate distance sections, with no center frequency. A visual QA preview left enabled in 1.3.10 caused a tooltip to follow the pointer outside the band buttons. The preview state and endpoint were removed entirely. Native UI checks confirmed the 20m panel appears over its button and disappears over the spectrum and other controls. The panel background is opaque.

All 16 band jumps passed again in the isolated free-tuning profile. The full build, history test, public plugin ABI comparison, whitespace check and packaged app signature verification passed. Distance ranges are qualified editorial estimates, not measured averages or record maxima.

## RTL-SDR hotplug crash (October 21)

Build 1.3.13 includes the private patched USB backend and source-state corrections described in [USB-HOTPLUG.md](USB-HOTPLUG.md). LLDB reproduced the reported null-interface failure in pristine libusb. The fixed native test passed the missing-interface case and 1,000 simulated removals during submission. Both CTest checks, public plugin ABI comparison, build, Python compilation, whitespace and complete bundle signature checks passed. The packaged backend's code section and provenance receipt matched the private build.

The isolated QA profile received live RF from the RTL-SDR Blog V4. Ten automated refresh/start/stop cycles passed, including rejecting refresh while the read handle was active. No synthetic reception was added to the normal profile. Oz subsequently confirmed that physical unplug/replug followed by Refresh recovers and receives normally in installed 1.3.13. This verifies the reported scenario on his setup; it does not cover every USB hub or disconnect timing.

## Audio NR2 heap corruption (October 21)

The 1.3.13 report crashed in the OMLSA/MCRA worker's FFT reorder step. LLDB disassembly showed an invalid index read from its table. Running the actual numerical denoiser under AddressSanitizer reproduced an earlier heap write in `G_calculate_process`: mirroring bin zero to index N wrote past an N-element SNR array. The smoothed-SNR array had the same error; the spectrum mirror also crossed into the following packed spectrum.

Build 1.3.14 skips the mirror write for DC in all three places. DC already occupies bin zero. No valid positive-frequency mirror indexes or FFT geometry changed. The standalone regression processes 8,000 blocks, equivalent to 12.8 minutes at 48 kHz, including silence, seeded noise, tones and full-scale square waves. It passes AddressSanitizer and is included in CTest on macOS.

The full package passed all three CTest checks, the plugin ABI comparison, build and signature verification. An isolated synthetic receiver with Audio NR2 enabled delivered audio samples through ten start/stop cycles. LLDB hit the corrected FFT through the same AFNR/OMLSA processing stack shown in the report. The normal receiver profile was not used for synthetic data. Visual and listening checks remain pending because the Mac was locked.

## Baseband NR and Apple math (October 22)

Oz rejected this 1.3.15 replacement in live use: he reported worse/distorted audio and a slower appearance. The synthetic results below are not acceptance evidence for its sound quality. See the correction record after this section.

Installed 1.3.15 includes the adaptive IQ denoiser, 0-24 dB reduction control and optional Apple Accelerate backend described in [NOISE-REDUCTION.md](NOISE-REDUCTION.md). The final build passed all six CTest checks, including the independent AddressSanitizer path and a numerical target linked only to FFTW with Apple code compiled out. The public plugin ABI comparison, Python compilation, whitespace and full bundle signature checks passed.

Controlled weak-SSB measurements inside the actual 300-3000 Hz USB passband show 5.58 dB less reconstruction error at the 12 dB default. Strong/clean modulation fixtures include SSB, AM, narrow FM, BPSK and CW. These synthetic results do not establish live speech intelligibility or decoder success rates. Detailed strength tradeoffs, legacy comparisons and benchmark limits are in the linked document.

`python3 beige/tests/noise_reduction_smoke.py --url http://127.0.0.1:18867` passed twelve receiver cycles in a separate four-module File/Radio/NR/NullAudioSink profile. Every cycle delivered audio samples, alternating the reported backend between Apple Accelerate and FFTW. The test also changed strength, restarted reception and exercised center retuning. It waits for the receiver's actual playback state between asynchronous stop/start requests. No synthetic reception was added to the normal profile.

QA ran in tmux `beige-nr-qa:1.1`, port 18867, repo `~/pro/sdrpp-beige`, logs under `/tmp/beige-baseband-nr/`. Both the process and session were stopped after testing; the stop command was `tmux kill-session -t beige-nr-qa`. The initial detached launch and an early smoke attempt did not count as successful receiver tests.

Installation retained all 46 profile JSON files byte-for-byte. The prior installed application is preserved in `build-beige/installed-before-nr-improvement.app.backup-20261022T071443`. The normal receiver reopened with its RTL-SDR, retained plugins and Baseband NR enabled; HTTP is disabled. Native visual inspection confirmed the 12 dB slider, checked automatic acceleration, Apple Accelerate status and 10.2 ms processing delay at the current effective sample rate. Live RF and plugin decoding resumed. Oz's subsequent listening report rejected this release, so 1.3.14 was restored while preparing 1.3.16.

## Baseband NR regression correction (October 22)

Installed 1.3.16 restores the original LOGMMSE sound path and 4x output level. The rejected adaptive mask is moved to `beige/experimental/` and has no production include. Its attenuation slider and endpoint are removed. The original out-of-place FFTW implementation is restored; a local differential benchmark had shown the replacement slower at the 65536-point display size. This is not evidence that every FFT size was slower, and background builds affected wall-clock timing.

The new `ClassicNR` comparison matches the reference LOGMMSE samples exactly with FFTW at 192000 and 204800 S/s, including original level scaling and stream partition. Automatic unsupported-length fallback is also exact. At 204800 S/s, Apple acceleration's maximum sample difference is 3.73e-7 in the fixture. A bounded-output case verifies retained overlap cannot write past the caller's capacity. The experimental algorithm's favorable synthetic results are retained only as research evidence and do not qualify live sound.

The final package passed all six CTest checks, public plugin ABI comparison, Python/Ruff checks, whitespace and complete signature verification. Sixteen isolated receiver checks passed: twelve fallback cycles at 256 kS/s, then four actual Apple/FFTW switches at 204800 S/s. Every case delivered audio samples. The QA process and `beige-nr-qa` tmux session were stopped after the test.

Installation preserved all 46 profile JSON files byte-for-byte; backup: `build-beige/recovery-before-1.3.16.app.backup-20261022T073203`. Normal RTL-SDR reception resumed at the saved tuning, with HTTP disabled. The log confirms the original 800 kS/s / 32000-point NR geometry; that length correctly uses FFTW fallback. Native inspection confirmed the installed 1.3.16 window and ongoing RF reception. Oz confirmed both voice audio and display responsiveness are back to normal in 1.3.16. He also confirmed that 1.3.15 distortion occurred with Baseband NR alone.

## Acceleration and UI follow-up (October 22)

The 1.3.17 numerical regression first failed on all-zero input: the original NR produced nonfinite output with FFTW. A separate test through the actual IFNR wrapper failed on output sample counts when acceleration changed. Noise-power flooring and bounded exponential-integral lookup fix the first fault. Replacing FFT plans without resetting the NR state fixes the second. Neither failure establishes that Apple FFT caused Oz's original live distortion.

The seven-test CTest gate passed, including the production numerical/backend-switch test, existing original-path comparison, AddressSanitizer, portable math and Audio NR2 regressions. Numerical details and commands are in [NOISE-REDUCTION.md](NOISE-REDUCTION.md). The public plugin ABI comparison and full package signature passed.

An isolated four-module receiver compared 2D/3D and Apple/FFTW against the same IQ file. Full-history 3D/FFTW improved from 68.84 to 87.03 FPS after the macOS buffer upload change; the 3D/Apple interval improved from 61.80 to 81.91 FPS. Per-process samples corroborated removal of the repeated `glBufferSubData` wait. Files: `/tmp/beige-accel-qa/performance-before.json`, `performance-after.json`, `sample-before.txt` and `sample-after.txt`. The source is unpaced, so this is a local stress measurement rather than a real-time RF guarantee. QA tmux: `beige-accel-qa`, port 18869; stop with `tmux kill-session -t beige-accel-qa`.

Sixteen receiver restart/fallback/backend checks passed. After final packaging, four further live backend switches passed against the verified bundle, with the loaded NR module path checked using `lsof`. The QA app then shut down with exit code zero while receiving. The GLFW loop now honors the explicit HTTP exit flag outside test builds, allowing this teardown check. Normal launches still disable HTTP. The final QA session exited and left no QA receiver running.

One premature QA launch occurred before packaging/signing completed; its -9 exit and that packaging attempt are discarded. The rebuilt package passed full signature verification before the final successful runtime check. Final packaging log: `/tmp/beige-accel-qa/package-final.log`; runtime result: `final-runtime-check.json`.

Installed 1.3.17 preserves all 46 profile JSON files and ten private plugin binaries byte-for-byte during replacement. Backup: `build-beige/installed-before-1.3.17.app.backup-20261022T074940`; receipt: `/tmp/beige-accel-qa/install-receipt.json`. Complete installed-bundle signature verification passed. Native inspection confirmed version 1.3.17 receiving from the RTL-SDR Blog V4 at the saved 7.165 MHz LSB tuning, with the user's selected 2D view retained. Live listening acceptance of these additional fixes remains pending.

After verification, Oz requested closing Beige whenever it is not being tested. Quit was invoked through its native application menu; process inspection confirmed no Beige instances remained. Do not restart it just to request a listening check.

## Separate standard SDR++ startup report (October 22)

The supplied previous-exit report identifies `/Applications/SDR++.app`, standard version 1.2.0, about 0.7 seconds after launch. Static disassembly places the failure in configuration cleanup after erasing the current JSON-map member while retaining its iterator. LLDB reproduced the same invalid iterator sequence using an isolated profile with one extra key and no radio modules. Neither the standard app nor its normal profile was changed.

Beige has no such erase-during-iteration cleanup loop. Installed 1.3.16 successfully started with the same extra-key case, preserved the key and saved 56 configuration fields. The separate QA process was stopped. This report does not demonstrate a Beige shutdown failure.
