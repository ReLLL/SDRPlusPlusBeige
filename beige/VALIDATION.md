# Beige validation

## Replay frequency tuning and default (October 23, 1.3.44)

Oz confirms that 1.3.43 double-click seeking and divider dragging work physically. He reports that replay cannot be retuned afterward. Build 1.3.44 adds frequency-only tuning through graph clicks/drags, the top frequency input, wheel and arrow keys. The golden IQ marker identifies historical listening. Double-click still chooses a new time/frequency; Live restores the live frequency display. New or missing preferences enable Waterfall replay by default, while a saved Off survives restart.

All 29 CTests, plugin ABI comparison, package signatures, Python/Ruff and whitespace checks pass. The packaged five-module smoke measures 498.05 -> 1798.83 -> 498.05 Hz while replay moves between carriers at the same advancing timeline, with zero steady audio underruns. Its selected sample, revision and acquisition generation remain unchanged; the cursor advances rather than restarting. Thirty-two rapid retunes preserve the revision. Nonfinite, outside-band and filter-edge frequencies are rejected. Live RF center/VFO offset and persistent tuning flags remain unchanged; the header restores live frequency on Live.

The full integration smoke also passes all eight Radio modes, exact time/frequency tone checks, independent audio rewind, live Recorder isolation at 500 Hz while listening to historical 1800 Hz, stopped tails, stale-generation rejection, disk failure auto-off/recovery and release on disable. Separate packaged launches verify a missing preference defaults on and an explicitly saved Off allocates no IQ storage. These are bounded audio/backend checks; native click/drag acceptance remains pending.

Installed 1.3.44 preserves all 137 checked profile/plugin/resource files. Normal startup loads 39 instances, receives effective 800 kS/s RTL-SDR IQ, retains a 72.45-second buffer for the user's 1401-row waterfall and exits with code zero. One earlier build-path launch blocked in the existing PortAudio/CoreAudio device-format probe during Radio construction, before replay initialization; a clean process restart clears it. No audio routing or audio binary changed. All test instances are closed. Private logs and the installed-app backup receipt are under `/tmp/beige-iq-qa-20261023/`.

## Waterfall IQ replay (October 23, 1.3.43)

The optional Radio checkbox defaults off. Double-clicking seeks the displayed time/frequency point; a first click reserves the gesture, while deliberate tuning drags and existing divider/bandwidth drags retain priority. Live in the waterfall context menu or audio strip exits IQ replay. Audio rewind cursors and exports remain independent. Retention follows visible rows and actual FFT sample intervals, with a bounded disk queue, safety margin and resize hysteresis. See [IQ-REPLAY.md](IQ-REPLAY.md).

All 29 CTests pass, including normal/AddressSanitizer storage and mapping tests. They cover exact floats, wrap/expiration, concurrent generation resets, injected I/O/queue/allocation failures, free-space rejection, unlinked storage, randomized packet framing, exact FFT centers, stale displayed snapshots, resize rotation, invalid/fractional/outside coordinates, frequency edges and small retention changes. During development, the linked-core tests exposed a static initialization order fault: global GUI constructors reached the new map before its constructor. Function-local sidecar initialization corrects that startup failure.

The packaged five-module integration test measures actual output tones at 398.44, 597.66 and 1798.83 Hz for expected 400/600/1800 Hz selections. Each steady replay check reports zero audio underruns. During historical 1800 Hz listening, Recorder captures the distinct live 500 Hz signal, with the replay-frequency component below the test's 1% amplitude limit. Two passes through all eight Radio modes, stale/outside seeks, restart invalidation, independent audio controls, a one-sample stopped tail, filesystem-failure auto-off and subsequent recovery pass. Disabling releases storage and leaves no IQ pathname. The older four-module audio rewind/WAV regression also passes finite stereo audio, exact 30/60-second export overlap, failed-save recovery and stopped playback.

An isolated real RTL-SDR profile captures the effective 800 kS/s bandwidth and replays it with 103802 rendered audio frames, zero underruns and zero invalid sink samples. Four enable/disable cycles and a clean exit pass. Its brief replay phase has median 60.0 UI FPS; there is no paired RF baseline for measuring overhead. In the paced File test, short off/capture/stopped-replay phases report median 103.8/114.6/114.3 FPS and 16.0/18.4/12.9% of one CPU core for the whole process. Earlier runs have a different 72-73 FPS ceiling. Other desktop activity and stopped versus live DSP differ, so these numbers establish usable throughput in these bounded checks, not a speed improvement or a precise overhead estimate.

The plugin ABI comparison, build, Python/Ruff, whitespace and full packaged signatures pass. Packaged early-return checks and four alternating enabled/disabled TETRA exits pass. Installed normal-profile startup loads all 39 instances, receives RTL-SDR audio and exits cleanly; all 137 checked preference/plugin/resource files match the fresh baseline. Imported plugins and accepted NR settings are preserved.

Native single-click mapping in the earlier 1.3.42 build matches the exact displayed row stamp. Final double-click/drag mouse verification needs a physical check: automation no longer delivers clicks even to pre-existing buttons in the final test window. This is not counted as a passing gesture test. Private logs, generated IQ, WAVs and receipts: `/tmp/beige-iq-qa-20261023/`. These checks do not establish freedom from long-run crashes or compatibility with untested hardware/plugins.

## Spectrum controls and shutdown crashes (October 23, 1.3.37)

Right-clicking either Max or Min and choosing Set Defaults resets both to 0/-120 dB, updates the graph/palette and persists the pair. Native checks pass on the installed package. Radio's padlock defaults locked, permits numeric bandwidth edits and modulation changes, blocks graph resizing, and permits resizing when unlocked. Both saved choices survive restart; 2D and 3D input paths were checked in an isolated File profile. Native automation here needs explicit `clickCount: 1` as well as a held click; several omitted-count clicks did not reach ImGui and are not counted as successful tests.

The 1.3.34 exit report places a DX Notebook writer inside a static regex during process teardown. The host called module end hooks without destroying instances, so the writer destructor/join never ran. Desktop shutdown now destroys consumers before Radio/device providers and runs end hooks only after instance destruction. A regression using the real ModuleManager verifies worker completion, provider lifetime and end-hook ordering. Three old-host timing trials did not reproduce the reported regex crash; that limits the reproduction evidence. Three corrected-host trials exited cleanly while an isolated bridge imported 500, 1000 and 2040 receptions. An earlier text-import fixture produced no rows and is excluded from active-writer evidence. All synthetic reception stores and CSVs stay outside the normal profile and are removed after testing.

The second supplied 1.3.34 report came from an agent test with an incorrect resource path. QSO audio started before main and survived the resulting early return. A scope guard now destroys that startup audio before global stream/mutex teardown. The packaged regression verifies --help exits 0 and a missing resource directory exits 1 rather than a signal.

The two 1.3.36 reports identify the same TETRA destruction fault, first in the full-profile test and then in a minimal reproducer. TETRA intentionally leaves conversion/resampling workers idle while disabled. Their implicit base destructors stopped too late, after member-owned stream mutex destruction. Explicit final stops fix that lifetime ordering. The old packaged reproducer aborts with -6; the correction passes ten alternating enabled/disabled cycles. The new reusable packaged smoke repeats ten cycles, followed by two checks after the script's final isolation assertions. Run with a new empty directory:

```sh
python3 beige/tests/shutdown_smoke.py --app '/Applications/SDR++Beige.app' --work /tmp/beige-shutdown-new --cycles 10
```

Installed 1.3.37 passes all 25 CTests, public plugin ABI comparison, Python/Ruff, whitespace, build and full package signature checks. Three normal-profile launches load all 39 instances, start RTL-SDR reception and exit 0. All 137 checked settings/plugin/resource files match the fresh pre-test baseline after every run. Rewind/export smoke also passes with finite stereo audio and matching retained 30/60-second WAV contents. These bounded checks do not establish long-run freedom from crashes. Private evidence: `/tmp/beige-level-defaults-20261023/`.

## Rewind shortcuts and listening acceptance (October 22, 1.3.34)

Oz confirmed "Works well" on installed 1.3.33 when asked whether -10s replay preserves voice clarity and returns cleanly to Live. This supersedes the pending listening status below for those behaviors.

Version 1.3.34 adds -3s, -5s and -30s beside -10s, with timeline/save wrapping at narrow widths. Native mouse tests verify each new step from the current position, existing ten-second rewind and layouts at 900/1120/1400 pixels. Native compact-menu Save 60s and direct Save 30s create valid stereo float WAV sound files. Packaged export tests repeat exact 30/60-second overlap, finite audio, failed-save recovery and transport checks. All 24 CTests, ABI, build and signatures pass. Installed normal-profile startup preserves 137 checked settings/plugin/resource files, and the app closes after testing. The accepted audio processing and history engine are unchanged. Private evidence: `/tmp/beige-audio-rewind-20261022/native-buttons-34.json`, `buttons-export-smoke.log`, `build-1.3.34.log` and `install-1.3.34.json`.

## Audio rewind and spectrum-edge crash (October 22, 1.3.33)

Desktop rewind retains 60 seconds of the selected listening output, with ten-second rewind, pause/resume, a timeline, Live and background Save 30s/60s. Native mouse checks cover the wide layout and the compact Save menu at 1120 pixels. At 48 kHz, retained stereo storage is 24,960,000 bytes; disabling the feature releases it. Existing RF, decoder and Recorder branches remain live. Accepted NR algorithms and settings are unchanged. See [AUDIO-REWIND.md](AUDIO-REWIND.md) for signal ownership, export semantics and performance limits.

All 24 CTests, public plugin ABI, Python/Ruff, complete build and bundle signatures pass. Native/ASAN tests cover retained transport and spectrum measurement boundaries. Packaged File tests verify exact 30/60-second WAV overlap, finite nonzero stereo samples, failed-save recovery, RAW bypass and stopped replay. The actual-output regression fails on the initial 1.3.31 mute gate and passes after its fix through Audio, New Audio and CoreAudio. Stopped playback does not advance captured frames; pause holds position and playback pauses at the end of retained audio.

The supplied 1.3.32 crash report matches the File QA failure, reproduced in LLDB at `WaterFall::calculateVFOSignalInfo + 980`. A 250 kHz WFM channel at offset +300 kHz in an 800 kHz captured span maps its upper edge to the exclusive FFT boundary. The old inclusive peak loop reads one bin beyond its row, reaching unmapped memory on the final retained row. Measurement now bounds inclusive sample indices by `bins - 1`, rejects invalid spans and wholly out-of-span channels, and uses thread-local scratch. Other uses of the exclusive FFT-boundary mapping remain unchanged. The original recovery sequence now passes 56 RAW-to-audio NR recoveries and 100 concurrent mode/reducer request pairs with continuing finite audio. These checks do not establish long-run crash stability or listening quality.

Final 1.3.33 live RF comparison with rewind off, Live and Replay retains median 116.4-118.1 FPS with zero invalid audio samples. Whole-process CPU varies from 19.9% to 22.6% of one core, so these intervals do not resolve a small CPU cost. Installed 1.3.33/build33 passes the real Audio-output regression in the normal RTL-SDR profile, including stopped replay and end-of-history pause. Its 137 checked profile/plugin/resource files match the pre-install baseline byte-for-byte after testing. An earlier QA launch saved a different Radio VFO offset; that single field was restored before final installation. Accepted NR choices, audio routing, plugins and band plans are retained. All Beige processes exit cleanly and are closed after automated testing. Operator listening acceptance is pending. Private logs, exports, profiler capture, hashes and installation receipts: `/tmp/beige-audio-rewind-20261022/`.

## Audio NR recovery and QA crash (October 22, 1.3.30)

The 1.3.29 regression reproduces a checked but disconnected NR2 after RAW-to-USB, with idle worker CPU and frozen Scaling despite dry audio continuing. Re-enabling its checkbox does not recover it. The fixed package restores actual AF-chain membership for NR2 and neural NR when returning to audio modes. Suppression algorithms and accepted neural values are unchanged. Oz subsequently confirmed on installed 1.3.30: "NR2 reduces noise again." This confirms recovery of the audible effect, not long-run crash stability.

An initial uninstalled 1.3.30 QA build crashed during concurrent HTTP mode changes and UI chain restoration. The supplied report identifies this process and an invalid stream-mutex lock during Radio teardown. Module commands now share the UI queue with procfs setters. LLDB confirms `RadioModule::handleDebugCommand` runs on `com.apple.main-thread` through `httpdebug::procfs::processQueue` and the render loop. Its replay passes 14 recovery checks plus 20 concurrent request pairs and exits with status zero. The earlier LLDB replay did not reproduce the crash and is not counted as proof of the original failure.

The fixed receiver passes 56 RAW-to-audio recovery checks and 100 concurrent mode/reducer request pairs, with positive worker CPU and continuing finite audio. All twenty CTests, public plugin ABI, Python/Ruff, whitespace, build and complete bundle-signature checks pass. The final installed package repeats all 14 recovery checks successfully. These tests exercise real module handlers and DSP workers; they do not measure listening quality.

Installed 1.3.30/build30 preserves all 137 checked profile/plugin/resource files byte-for-byte. The normal RTL-SDR profile receives through its saved Audio sink, passes two RAW-to-LSB recoveries for each audio reducer, and reaches Neural Ready. NR2 CPU reads 0.37-0.39% of one core; neural reads 4.75-4.77%. A single display snapshot reports 77.5 FPS, not a performance comparison. Original mode, NR choices and every checked file remain unchanged after runtime testing. All Beige processes exit cleanly. Private logs, debugger output, baseline hashes and backup path: `/tmp/beige-nr2-20261022/`.

## RTL-SDR manual controls (October 22, 1.3.28)

Twenty CTests, public plugin ABI comparison, complete build and packaged signature verification pass. The added native/ASAN gain tests cover all 4,096 combinations each; the preference test covers compatibility, malformed values and atomic updates. Live V4 testing confirms independent stage effects, bandwidth response, Auto restoration, saved-mode recovery, finite audio, ten normal and ten interrupted-reader restarts. A later four-case 130-second comparison remains receiving throughout. One earlier stream stopped cleanly with result -1; no cause or universal USB-stability claim is established.

The installed normal profile receives at its saved tune through its existing Audio sink. Installation preserves 137 checked JSON, resource and private-plugin files byte-for-byte. Details, measurements, UI acceptance and limits: [RTL-CONTROLS.md](RTL-CONTROLS.md).

Oz confirmed physical Auto/Manual switching in 1.3.28. Version 1.3.29 adds Reset to defaults for Manual controls, retaining the selected mode. All twenty CTests, ABI, signature and live reset/finite-audio checks pass. The installed normal profile receives with Oz's saved settings; all instances close after testing.

Read when: verifying the spectrum renderer, imported plugins or a packaged build.

## Automated gate

Version 1.3.27 adds native and AddressSanitizer tests of the actual RTL-SDR driver, bringing the gate to seventeen CTests. Seven delayed-completion scenarios run 100 times each per executable. See [USB-HOTPLUG.md](USB-HOTPLUG.md) for the stop/restart crash, pinned dependency and hardware-check limits.

Neural clarity switches add a fifteenth CTest, eight-combination native/ASAN packet checks, and `neural_clarity_smoke.py`. Test missing-preference defaults, independent changes, finite output, return to all-off and saved choices. Compare the all-off native engine with the saved 1.3.24 binary at its floating-point repeat precision; baseline parameters must remain unchanged.

Neural 1.3.24 level recovery adds `beige_neural_voice_level`: known speech attenuation, rejected-noise exclusion, silence hold, clean unity and the gain bound. Actual-engine packet/FFT/rate/ASAN checks include high-level scale invariance. The complete gate now contains fourteen CTests. Controlled speech results and limitations are in [AUDIO-NR.md](AUDIO-NR.md).

For neural model discovery, run `neural_resources_smoke.py` with `--app`, a four-module File-source `--template`, `--overlay` without the model, and a new `--work-dir`. This covers the imported resource layout that 1.3.22's bundled-resource tests missed. It must reach Ready and deliver finite audio without adding a model to the overlay. The standalone `beige_neural_model_path` test also covers bundle relocation and unbundled fallback.

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

With the Source panel collapsed, explicitly send File Source `set_path` before playback; the menu's lazy file-open path otherwise has not run. For RTL restart tests, wait for `/sdr/status` to finish its transition before querying UI-queued procfs endpoints: re-enumeration can exceed their one-second queue deadline.

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
