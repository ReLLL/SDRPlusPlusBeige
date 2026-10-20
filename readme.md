# SDR++ Beige

An SDR++Brown fork with a fast, perspective 3D spectrum display. Frequency runs left to right, signal level controls height, and older FFT slices recede into depth. The newest spectrum stays at the front.

- 3D replaces the upper spectrum pane and is enabled by default.
- Click the toolbar's **3D / 2D** button to switch views. Display settings offer the same choice. Your choice persists.
- The 3D surface uses the selected waterfall palette and its level range. The regular waterfall remains below it.
- Fixed history storage, peak-preserving reduction, bounded geometry and a cached mesh keep rendering inexpensive.
- Separate Beige settings and decoder histories support side-by-side installation with Brown.
- Compact, color-coded HAM band buttons center the receiver and spectrum without changing center/free tuning mode. All 16 bands from 160m to 23cm are one click away.

This is a macOS development build. Its bundled Homebrew libraries currently require macOS 26 or newer. It is locally ad-hoc signed; public distribution has not been prepared.

## Build and validation

Run `bash beige/build-macos.sh`. The local build needs CMake, Ninja, a C++17 compiler, GLFW, FFTW, VOLK, zstd, PortAudio, Airspy libraries, Codec2 and SoapySDR. See [development notes](beige/DEVELOPMENT.md) and [validation](beige/VALIDATION.md).

The bundle is created under `build-beige/SDR++Beige.app`. The packaging script can carry over installed Brown hardware modules after the plugin ABI check passes. It never edits Brown's application.

To copy an existing installation's private settings, plugins, band plans, frequency lists and decoder histories, use `python3 beige/import_brown.py DESTINATION_PROFILE BEIGE_APP`. An existing destination is backed up first. Run the importer while Beige is closed. No user configuration, logs or recordings belong in this repository.

## Upstream and license

- [SDR++Brown](https://github.com/sannysanoff/SDRPlusPlusBrown), by sannysanoff and contributors.
- [Original SDR++](https://github.com/AlexandreRouma/SDRPlusPlus), by Alexandre Rouma and contributors.
- Beige additions by ReLLL. GPLv3; see [license](license). Existing third-party licenses and credits remain with their sources.

The display takes its frequency/level/time presentation from Yaesu's 3D spectrum concept. No other Yaesu receiver features or controls are included.
