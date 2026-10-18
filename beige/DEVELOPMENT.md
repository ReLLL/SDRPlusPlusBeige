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

Default Beige profile: `~/Library/Application Support/sdrpp-beige`. The importer preserves module instances and enabled/disabled states, selected band plan, source settings and exact frequency-manager bytes. It remaps Brown paths to Beige. Recordings and backup directories are not imported.

Custom plugin binaries are copied from the installed version and relocated to Beige's core and dependency paths. Private helper runtimes stay beside their plugins. Copied binaries are signed and checked; original Brown binaries are untouched. Plugin payload manifests are updated after relocation.

DX Notebook's SQLite database uses SQLite's read-only backup API so committed WAL transactions are included. CSV and map cache are copied separately. CW logs are copied into Beige's profile. Process-local data-root overrides isolate both notebooks from Brown.

Normal launches disable the HTTP debug server. For QA, explicitly pass `--http PORT`. Useful endpoints: `/proc/display/spectrum3d` (read/write boolean) and `/proc/display/spectrum-stats` (CPU drawing times, mesh size, rebuilds and history rows). CPU drawing time excludes the GPU's execution time.
