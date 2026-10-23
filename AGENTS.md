# Agent Instructions for SDR++ Brown Fork

This document provides guidance for AI agents working with the SDR++ Brown Fork codebase.

## Additional Docs
- [AGENTS-windows.md](./AGENTS-windows.md)  -  Windows build via windows-mcp (winget/vcpkg toolchain, VS2022 build, runtime assembly, procdump/cdb crash diagnosis, E2E tests)
- [AGENTS-debugging.md](./AGENTS-debugging.md)  -  HTTP debug server, module/procfs commands, GUI automation, lldb crash/hang debugging, bugfix workflow

## Building Locally (Linux - Debian/Ubuntu based)

The following steps outline how to build the SDR++ project locally on a Debian/Ubuntu-based system. These steps are derived from the CI build process and local build attempts.

**1. Install System Dependencies:**

Use `apt` to install necessary packages. `libvolk2-dev` might not be available on newer distributions (e.g., Ubuntu 24.04 Noble); replace it with `libvolk-dev` and `libcpu-features-dev`.

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libfftw3-dev \
    libglfw3-dev \
    libvolk-dev \
    libcpu-features-dev \
    libzstd-dev \
    libairspyhf-dev \
    libairspy-dev \
    libiio-dev \
    libad9361-dev \
    librtaudio-dev \
    libhackrf-dev \
    librtlsdr-dev \
    libbladerf-dev \
    liblimesuite-dev \
    p7zip-full \
    wget \
    portaudio19-dev \
    libcodec2-dev \
    autoconf \
    libtool \
    xxd \
    libspdlog-dev
```

**2. Install SDRPlay API:**

Download and install the SDRPlay API.

```bash
wget https://www.sdrplay.com/software/SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2.run
7z x ./SDRplay_RSP_API-Linux-3.15.2
SDRPLAY_ARCH=$(dpkg --print-architecture)
sudo cp $SDRPLAY_ARCH/libsdrplay_api.so.3.15 /usr/lib/libsdrplay_api.so
sudo cp inc/* /usr/include/
rm -f SDRplay_RSP_API-Linux-3.15.2.run SDRplay_RSP_API-Linux-3.15.2 # Clean up
rm -rf $SDRPLAY_ARCH inc # Clean up
```

**3. Install Additional Libraries (libperseus-sdr, librfnm, libfobos):**

The CI script involves cloning these libraries from GitHub, building, and installing them. However, due to persistent tooling issues with the `run_in_bash_session` tool when handling directories created by `git clone` (even if cloned into `/tmp`), these libraries were **disabled** in the local build attempt via CMake options.

If these libraries are essential for your task and the tooling issues are resolved, the original steps from `docker_builds/debian_bookworm/do_build.sh` are:

*   **libperseus:**
    ```bash
    # git clone https://github.com/Microtelecom/libperseus-sdr
    # cd libperseus-sdr
    # autoreconf -i
    # ./configure
    # make
    # sudo make install
    # sudo ldconfig
    # cd ..
    # rm -rf libperseus-sdr
    ```
*   **librfnm:**
    ```bash
    # git clone https://github.com/AlexandreRouma/librfnm
    # cd librfnm
    # mkdir build
    # cd build
    # cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    # make -j$(nproc)
    # sudo make install
    # cd ../..
    # rm -rf librfnm
    ```
*   **libfobos:**
    ```bash
    # git clone https://github.com/AlexandreRouma/libfobos
    # cd libfobos
    # mkdir build
    # cd build
    # cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
    # make -j$(nproc)
    # sudo make install
    # cd ../..
    # rm -rf libfobos
    ```

**Pitfall:** The `run_in_bash_session` tool may error out with `ValueError: Unexpected error: return_code: 1 stderr_contents: "cat: /app/libname: Is a directory"` after `git clone`. This seems to be an internal tooling issue when it tries to synchronize workspace state.

**4. Build SDR++:**

Create a build directory and run CMake with the appropriate options (disabling problematic libraries as needed) and then Make.

```bash
# Ensure no problematic cloned directories are in /app if previous attempts failed
sudo rm -rf /app/libperseus-sdr
sudo rm -rf /app/librfnm
sudo rm -rf /app/libfobos

sudo rm -rf build
sudo mkdir build
sudo chown $(whoami):$(whoami) build
cd build

cmake /app \
    -DOPT_BUILD_BLADERF_SOURCE=ON \
    -DOPT_BUILD_LIMESDR_SOURCE=ON \
    -DOPT_BUILD_SDRPLAY_SOURCE=ON \
    -DOPT_BUILD_NEW_PORTAUDIO_SINK=ON \
    -DOPT_BUILD_M17_DECODER=ON \
    -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON \
    -DOPT_BUILD_PERSEUS_SOURCE=OFF \
    -DOPT_BUILD_RFNM_SOURCE=OFF \
    -DOPT_BUILD_FOBOSSDR_SOURCE=OFF

make VERBOSE=1 -j$(nproc)
```

**Key Pitfalls & Solutions during Build SDR++ step:**
*   **Initial `libvolk2-dev` failure:** Replaced with `libvolk-dev` and `libcpu-features-dev` during dependency installation.
*   **CMake source directory issues:**
    *   Initially, `cmake ..` from within the `build` directory failed to find `CMakeLists.txt` or picked the wrong directory.
    *   **Solution:** Explicitly provide the path to the source directory: `cmake /app [options...]`.
*   **Permission issues with `build` directory:**
    *   `mkdir build` failed with permission denied.
    *   **Solution:** Use `sudo mkdir build` and then `sudo chown $(whoami):$(whoami) build` to ensure the current user owns the build directory before `cd`ing into it and running `cmake` and `make` without `sudo`.
*   **Tooling errors with `git clone`:** As mentioned in step 3, direct cloning and building of some external libraries caused persistent errors with the `run_in_bash_session` tool. The workaround was to disable these modules via CMake flags (`-DOPT_BUILD_PERSEUS_SOURCE=OFF`, etc.).

**5. Output:**

The compiled application `sdrpp` and various module `.so` files will be located in the `build/` directory (specifically, `build/sdrpp` and `build/core/` for `libsdrpp_core.so`, with other modules in their respective subdirectories under `build/`).

## Agent Behavior Guidelines

### ⚠️ CRITICAL: Git Operations

**NEVER push commits to the remote repository unless explicitly requested by the user.**

- ❌ DO NOT run `git push` automatically after commits
- ❌ DO NOT run `git push` after merges
- ❌ DO NOT run `git push` as part of any automated workflow
- ✅ ONLY push when the user explicitly says "push" or requests it
- ✅ Always confirm before pushing if unsure

This applies to all branches including `master` and feature branches.

### Committing
- You MAY commit changes locally with appropriate messages
- You MAY create commits to save work
- You MAY prepare commits for review
- Wait for explicit user approval before pushing

## Building Locally (macOS / ARM64)

**CMake Configuration:**

```bash
cd /Users/san/Fun/SDRPlusPlus/cmake-build-debug
cmake .. \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -G "Unix Makefiles" \
    -DOPT_BUILD_PLUTOSDR_SOURCE=OFF \
    -DCMAKE_INSTALL_PREFIX=/Users/san/Fun/SDRPlusPlus/root_dev/inst \
    -DOPT_BUILD_CH_EXTRAVHF_DECODER:BOOL=ON
```

**Build & Install:**

```bash
make -j$(nproc)
make install
```

> **⚠️ Must run `make install` after any source change to redeploy `.dylib`/`.so` modules into `root_dev/inst/`. Without `make install`, `sdrpp` loads stale modules from the build directory or an old install location, making it impossible to test runtime fixes (e.g., stream teardown hangs).

## Debugging

See [AGENTS-debugging.md](./AGENTS-debugging.md) for detailed debugging and remote control capabilities.

## Windows Build Caveats

### MSVC `min`/`max` macro clash

`<Windows.h>` defines `min` and `max` as preprocessor macros that break `std::min(...)` and `std::max(...)` with `error C2589: '(': illegal token on right side of '::'`.

Fix by parenthesizing the call:
```cpp
// Before (breaks on MSVC):
int x = std::min(a, b);
// After:
int x = (std::min)(a, b);
```

Search for unguarded uses with `grep 'std::\(min\|max\)('`.

### VS 2026 drops Windows 7/8 runtime support

Since June 2026, GitHub Actions `windows-latest` and `windows-2025` images ship **VS 2026** (MSVC 14.51), whose redistributable no longer supports Windows 7/8/8.1. The minimum target is Windows 10.

To build binaries compatible with Windows 7, pin the runner to `windows-2022` in CI:
```yaml
runs-on: windows-2022
```

## General Notes
* The build process can be lengthy due to the number of dependencies and modules.
* The CI scripts in `.github/workflows/build_all.yml` and `docker_builds/` are good references for platform-specific dependencies and build commands.
* Many warnings may appear during compilation (e.g., from IT++, libmp3lame, deprecated C++ features). For this exercise, they were non-fatal for the selected build configuration. Investigation may be needed if they cause issues with specific functionalities.

IMPORTANT!!! Never change layout and formatting of C/C++ code unrelated to the task you're doing. NO braces edits in unrelated code, no spaces evenly distributed in array initializations. In existing C++ code, each position matter, NEVER change unless you fix bug related to that line. NEVER add changes to source that just alter code layout!


# SDR++ Beige

- Read `beige/DEVELOPMENT.md` and `beige/VALIDATION.md` before changing the fork, renderer, packaging or profile import.
- Mac-first personal open-source fork. Keep Brown's existing interface. Only the Yaesu-style perspective spectrum was requested; no other Yaesu features or controls.
- 3D replaces the spectrum pane, defaults on, and switches to 2D through the toolbar. Frequency is horizontal, height is level, depth is elapsed time, newest at the front. Use the waterfall palette. No OLDER label.
- Preserve exported Brown object sizes, field offsets and module command IDs. New renderer state stays outside WaterFall. Run the plugin ABI comparison after host changes.
- Bound history and geometry; cache unchanged meshes. Measure CPU drawing time and UI frame rate after render changes.
- Build/package with `bash beige/build-macos.sh`; never edit or re-sign the installed Brown application. Beige's bundle and private profile are separate.
- Preserve imported module enabled states, frequency records and selected band plan. Isolate notebook databases, CSV paths and logs. Keep all private profiles and migration receipts out of Git.
- HAM shortcuts must center the band without toggling persistent tuning mode. Keep all 16 labels, chart-based ranges and compact responsive placement; run the isolated free/center tuning smoke checks after navigation changes.
- Local Kondor v1.0.55 is installed in Beige for its next launch; it starts hidden while enabled processing continues. Keep the recovered preferences and isolated Beige data. Preserve removal/update backups and update plugin payloads without replacing the full profile.
- Neural NR: preserve the 1.3.24 base model, normalization and voice-level values as the default. Oz likes its suppression and restored volume, but reports lost speech fragments. Keep clarity experiments independent and off by default; all off returns to that base. Do not turn a promising offline score into a default change without listening acceptance.
- Audio rewind: read `beige/AUDIO-REWIND.md`. Preserve live RF/decoder/Recorder paths; replay only the listening branch. Keep stereo, bounded history, worker-owned export and explicit Live/Replay state. Stop consumers before freeing sidecar output storage; preserve exported object layouts.
- IQ waterfall replay: read `beige/IQ-REPLAY.md`. Optional and off by default; double-click seeks, single clicks reserve the gesture, deliberate live drags retain priority. Size unlinked temporary disk storage from visible rows and actual FFT sample spacing, with bounded queues, margin and resize hysteresis. Tag the exact FFT center sample and use the previously displayed row/frequency snapshot. Reject stale/outside points; never clamp to another signal. Plugins, Recorder and audio history stay live. Keep allocation/I/O failure fallback, one-sample tails, ring/mapping/ASAN tests and real tone/Recorder isolation checks.
- Spectrum signal measurement: FFT boundaries may equal the bin count; inclusive sample loops must stop at `bins - 1`. Keep `spectrumSignalInfo` edge/ASAN tests and RAW-to-WFM stress checks when changing measurement or channel bandwidth behavior.
- Shutdown must destroy and join module instances before module end hooks/static destruction, with consumers before Radio/providers. Keep the startup QSO cleanup guard for early returns. Test the full imported profile as well as a minimal receiver: TETRA keeps audio blocks alive while disabled and must stop them explicitly in its destructor before output streams are destroyed.
- Spectrum bandwidth lock defaults on and persists per Radio instance. It locks mouse edge resizing only; numeric settings and modulation changes remain usable. Keep both VFO-limit paths consistent. Max/Min context-menu defaults reset both levels to 0/-120 dB together.
