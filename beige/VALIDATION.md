# Beige validation

Read when: verifying the spectrum renderer, imported plugins or a packaged build.

## Automated gate

- Build all enabled stock modules and the core together.
- `ctest --test-dir build-beige --output-on-failure`.
- `python3 beige/check_plugin_abi.py`.
- `python3 -m py_compile beige/package_macos.py beige/import_brown.py beige/check_plugin_abi.py`.
- Package, verify the complete signature and inspect dependency paths.

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
