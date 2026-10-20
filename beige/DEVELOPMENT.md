# Beige development

Read when: building Beige, importing Brown, adding features or integrating either upstream.

## Scope

Keep Brown's interface and radio behavior. Beige's first addition replaces the spectrum pane with a perspective frequency/level/time surface. The waterfall stays unchanged. Newest data is at the front. Color follows the selected waterfall palette. Default to 3D; persist an explicit 2D selection.

The user requested only the Yaesu-style 3D view, not other Yaesu features, controls or a broader interface redesign. This is a personal open-source radio fork; no accounts, payments or analytics were added.

## Bases

Checked October 18, 2026:

- Brown: `5fca05446b3f985ab49c7c3c257980aaf2ec34fe`.
- Original SDR++: `8c9f5ee8fe405775bfcd62c8c8f8c0fc928a64af`.
- Installed Brown plugin ABI reference: `679f48deee5299a199d15d30e6e785fac6054896`.

Both upstream histories are retained. Merge resolution preserves Brown's multi-sink splitter, IF/AF plugin commands and AGC freeze, with original SDR++'s CTCSS/power squelch, high-pass controls, RDS fixes, recorder UTC filenames and rigctl bandwidth fix. Extra VHF's legacy FM call uses the updated four-argument FM initializer. Optional demodulator capability methods have defaults for Brown extensions.

Fetch `brown/master` and `upstream/master`, inspect their changes, merge deliberately, and rerun the build, ABI check, tests and receiver smoke checks. Never push without Oz's instruction.

## Renderer

`spectrum_history.h` holds 64 slices of 4,096 peak-preserving bins, about 1 MiB of fixed storage. Slice spacing is at least 100 ms, giving up to 6.3 seconds of history. The front slice refreshes on every FFT. A frozen slice enters at depth zero when captured. Its timestamp then stays fixed. Retuning, span changes, resizing, source start and long gaps clear stale history.

`spectrum_3d.cpp` draws every retained time slice. Front resolution follows the pane width up to 4,096 intervals, then decreases with distance to 2,048, 1,024 and 512. Adjacent strips share vertices and join grids without cracks. The complete cached mesh stays below the 16-bit index limit, including its front face and outline. The FFT path allocates no history buffers.

Topology and colors rebuild only when FFT history, dimensions, palette or levels change. Cached vertex positions are reprojected using monotonic display time on every UI frame. FFT arrival never controls Z movement. Stop freezes the last displayed time; the oldest surface tapers to the floor before retirement. Do not subsample history row indices or add index-based ridge lines: both caused visible jumps as history advanced.

New state lives outside the exported WaterFall object, preserving its size and member offsets. This matters because Brown plugins read public waterfall fields. `check_plugin_abi.py` compares sizes and offsets against the installed Brown reference. It does not establish compatibility with every future plugin or hardware API.

## Build and package

`bash beige/build-macos.sh` builds, runs history tests, checks the ABI, installs into the local staging folder and packages the app. New core and supported stock modules are compiled together. Missing hardware modules can be carried over from the installed Brown app, with their dependency closure bundled into Beige. Imported hardware modules still require tests with their actual devices.

The package is a local development artifact, ad-hoc signed under `com.tepesoft.sdrppbeige`. The effective minimum is macOS 26 because local Homebrew libraries target that version, even though the core compilation target is 15. Public Developer ID signing, notarization and distribution are separate work.

The Codec2 compatibility header is generated inside the build folder from the detected library version when the Homebrew bottle omits it. No global dependency headers are modified.

## Private profile import

On October 20, Oz requested removal of CW Notebook and Advanced FT8/FT4 from the installed Beige profile. Their load paths and module instances were removed. Plugin payloads and the original configuration are retained in the profile's `plugin-removal-backups/`; CW logs and DX history remain in their existing data folders. Brown is unchanged. Do not reimport these plugins into the active profile without a new request.

Default Beige profile: `~/Library/Application Support/sdrpp-beige`. The importer preserves module instances and enabled/disabled states, selected band plan, source settings and exact frequency-manager bytes. It remaps Brown paths to Beige. Recordings and backup directories are not imported.

Custom plugin binaries are copied from the installed version and relocated to Beige's core and dependency paths. Private helper runtimes stay beside their plugins. Copied binaries are signed and checked; original Brown binaries are untouched. Plugin payload manifests are updated after relocation.

DX Notebook's SQLite database uses SQLite's read-only backup API so committed WAL transactions are included. CSV and map cache are copied separately. CW logs are copied into Beige's profile. Process-local data-root overrides isolate both notebooks from Brown.

Normal launches disable the HTTP debug server. For QA, explicitly pass `--http PORT`. Useful endpoints: `/proc/display/spectrum3d` (read/write boolean) and `/proc/display/spectrum-stats` (CPU drawing times, mesh size, rebuilds and history rows). CPU drawing time excludes the GPU's execution time.

## HAM band toolbar

The toolbar offers 160m, 80m, 60m, 40m, 30m, 20m, 17m, 15m, 12m, 10m, 6m, 2m, 1.25m, 70cm, 33cm and 23cm in that order. Receive-navigation ranges and colors follow Oz's supplied chart. The presets are independent of the selected band-plan overlay.

Centers in MHz, in the same order: 1.900, 3.750, 5.36750, 7.150, 10.125, 14.175, 18.114, 21.225, 24.940, 28.850, 52.000, 146.000, 223.500, 435.000, 915.000 and 1270.000. Tooltips show the band, frequency range, best operating time, typical use and typical distance. The center frequency is intentionally omitted.

A selection calls the existing center-tuning operation once. It resets the selected VFO and spectrum view offsets and saves the new frequency. It never changes `MainWindow::tuningMode`, `centerTuning` or `VFOMoveSingleClick`; subsequent tuning keeps its original behavior. No exported object layout changes are required.

The existing 12-point utility font, small rounded color fills, a selected-band outline and adaptive rows keep the controls compact. Wide windows use one row to the right of the tuning controls; medium windows use two. When that space cannot fit two rows, the band strip moves below the controls. The SNR meter and logo retain their space.

QA-only endpoints: `/proc/display/ham-band` selects a band by label, and `/proc/display/ham-band-state` reports frequency, spectrum center, offsets and tuning flags. Run `python3 beige/tests/ham_bands_smoke.py --url http://127.0.0.1:18863` against a separate free-tuning QA profile; repeat with `--center-tuning` against a centered QA profile. Each pass moves the VFO before every band jump: free mode retains the offset, while center mode follows the move by retuning.

Tooltips appear only while the corresponding band button is hovered and disappear when the pointer leaves. No forced-preview override remains. The tooltip uses a colored heading, a muted frequency range and three spaced sections in an opaque 320-point panel with wrapped text.

Typical Distance values are rough planning ranges for ordinary contacts, not record maxima or measured population averages. Separate local FM/repeater use from directional weak-signal operation and routine HF DX. Satellite footprints, EME path lengths and rare long-distance propagation records are excluded. Do not relabel one-way WSPR spots as QSOs or claim the editorial ranges are statistical averages.

Primary context: [ARRL introduction to HF/VHF band behavior](https://www.arrl.org/files/file/Ham%20Radio%20License%20Manual/HRLM%203rd%20ed/Choosing%20a%20Ham%20Radio-2014.pdf), [ARRL beginner guidance on local/repeater reach](https://www3.arrl.org/files/file/Get%20Licensed/Quickstart%20Guide%20EBOOK.pdf), [ARRL propagation discussion](https://www.arrl.org/files/file/QST/This%20Month%20in%20QST/2021/06%20June%202021/2106%20Stanley%20article%20of%20the%20month.pdf), [K1RA's published rover QSO tables](https://k1ra.us/roving/k1ra-k8gp-rover-arrl-june-vhf-2016/) and [operators' reports in the 2022 ARRL 222 MHz and Up contest](https://contests.arrl.org/222/soaps/2022/). These support the propagation/mode distinctions and examples; the tooltip ranges remain editorial estimates rather than averages computed from those reports.
