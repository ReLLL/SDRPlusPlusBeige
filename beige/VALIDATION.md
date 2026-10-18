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
