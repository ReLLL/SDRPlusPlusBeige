# Interface and feature shortlist

Read when: planning Beige interface changes, receiver tools or a feature roadmap.

Research date: October 22, 2026. Desktop audio rewind is implemented in 1.3.33; see [AUDIO-REWIND.md](AUDIO-REWIND.md). Other items remain proposals. Sources were reviewed through project documentation, repositories and selected issue listings. Other applications were not installed or benchmarked in this review. Cost/effort ratings below are design estimates unless linked to measured validation.

## Existing Beige capabilities

Retain the HAM toolbar, one-click 2D/3D selection, current NR combobox, accepted neural defaults and optional RTL-SDR manual controls. The code already has multiple VFOs/audio routes, bookmark lists and bookmark scanning, range scanning, WAV/baseband recording, file playback and an SNR chart. Imported decoder plugins supply further capabilities; inspect them before adding duplicate decoders.

Brown's `QSOAudioRecorder` retains up to three minutes, but incoming radio audio is gated to the SSB-first transceiver layout. It uses vector insertion/front erasure and is not a general desktop rewind timeline. A desktop replay feature should use a bounded ring and explicit worker ownership rather than expose this buffer unchanged. The Radio module's notch fields are declarations only; no working manual notch UI/DSP was found there. Its ordinary AM path uses envelope demodulation with carrier/audio AGC, not synchronous AM.

## Open-source references

| Project and primary source | Relevant capability | Best application to Beige |
|---|---|---|
| [Gqrx](https://github.com/gqrx-sdr/gqrx), [receiver controls](https://github.com/gqrx-sdr/gqrx/blob/master/src/qtgui/dockrxopt.ui) | Grouped receiver controls, separate audio controls and saved configurations; Mac binaries/source | A clear selected-receiver panel and saved listening setups |
| [CubicSDR manual](https://cubicsdr.readthedocs.io/en/latest/application-window.html) | Per-channel audio output, mute/solo, direct numerical entry and keyboard actions | Make existing multi-VFO operation easier to see and control |
| [SDRangel workspaces](https://github.com/f4exb/sdrangel/wiki/Quick-start), [common device controls](https://github.com/f4exb/sdrangel/blob/master/sdrgui/device/readme.md) | Resizable workspaces, presets, supported-device replay and explicit device states; ARM Mac builds documented | Saved compact layouts, replay controls and recovery feedback |
| [SDRangel Frequency Scanner](https://github.com/f4exb/sdrangel/blob/master/plugins/channelrx/freqscanner/readme.md) | Activity counts, channel-specific thresholds/bandwidths, priority choices, scan-only mode and optional SSB voice detection | Extend the existing scanner with an activity table and explicit scan policies |
| [SDRangel WDSP receiver](https://github.com/f4exb/sdrangel/blob/master/plugins/channelrx/wdsprx/readme.md) | Automatic notch, CW peaking, AM/SAM and several NR/blanker options | Qualify selected audio tools independently against Beige's accepted baseline |
| [SigDigger](https://github.com/BatchDrake/SigDigger), [feature overview](https://batchdrake.github.io/SigDigger/) | Unknown-signal inspection, burst analysis and channel recordings | A lightweight signal-inspector panel; full analysis remains optional. The current README describes Mac source/on-request DMG support rather than a routinely published ARM Mac package |
| [inspectrum](https://github.com/miek/inspectrum) | Frequency/time/phase measurements, selection export, large recordings and SigMF input; Mac package instructions | Measurement cursors and interoperable capture export |
| [OpenWebRX+](https://github.com/luarvique/openwebrx) | Broadcast/repeater bookmarks, bookmark scanning, decoder results and received-image browsing | A time/location-aware station directory and consistent result browser. This is a server/browser reference, not a native Mac receiver replacement |
| [SDR++ Brown feature list](https://sdrpp-brown.san.systems/) | Existing NR, QSO recording, scanning, SNR chart and network reception | Improve inherited workflows before adding duplicates |

The checked SDRangel, Gqrx, CubicSDR, OpenWebRX+, SigDigger and inspectrum repositories are not archived. This does not qualify their code as a drop-in dependency or establish release/package quality. Gqrx's [issue listing](https://github.com/gqrx-sdr/gqrx/issues) includes incomplete bookmark dialogs and startup/device reports; SDRangel's [issue listing](https://github.com/f4exb/sdrangel/issues) includes a GUI/DSP shared-state crash report. Treat these as individual reports. For Beige, they reinforce complete preset capture, explicit device state and serialized DSP control.

## Interface direction

1. Keep the spectrum central and preserve the existing band colors and tuning behavior. Use restrained neutral surfaces elsewhere, aligned labels/values and consistent spacing. Offer compact/comfortable density and scalable text.
2. Organize the sidebar into Receive, Audio, Memories and Tools. Allow pinning frequently used sections and retain a familiar all-sections view. Advanced source/plugin settings stay available.
3. Put selected-VFO mode, bandwidth, AGC and audio route together. A small VFO list exposes select/mute/solo/output with consistent channel colors. Multiple VFOs share the current captured RF span; an RTL-SDR cannot tune independent bands simultaneously.
4. Show a compact receiver status row. Distinguish requested NR from Active, Starting, Mode bypass, Unavailable or Fault. Use recent sample activity plus stream membership, not checkbox state alone. Keep Scaling separate from CPU. Baseband wall-time load and audio thread CPU are different measures; label them accordingly.
5. Provide command search and shortcuts for tuning with units, finding memories, jumping to controls and switching views. Preserve the existing NR algorithm combobox; no duplicate Standard/Improved toggle. Any future A/B action should be a temporary bypass or saved-scene comparison.
6. Save explicitly chosen Voice, Digital and SWL workspaces. USB/LSB alone cannot distinguish speech from digital payloads. Do not silently enable speech processing or change center/free tuning when a band button is pressed.

## Feature priorities

| Order | Proposal | Concrete benefit | Expected running cost |
|---|---|---|---|
| 1 | Desktop audio rewind with Save last 30/60 seconds, implemented in 1.3.33 | Replay a missed callsign while capture continues; clear delayed/live indicator | About 25 MB at 48 kHz; [measured checks](AUDIO-REWIND.md) |
| 2 | Searchable/tagged memories, recent frequencies and tuning Back | Find channels quickly and undo a tuning detour; build on Frequency Manager | Low, with cached indexes and visible-row rendering |
| 3 | Click-and-drag manual notch, then optional automatic notch | Suppress narrow whistles/carriers without changing NR settings | Low expected audio-rate DSP cost; requires measurement |
| 4 | Signal inspector and activity overview | Measure occupied bandwidth, drift and repeated interference; see where activity occurred | Low to moderate if reusing FFT data |
| 5 | Scanner activity table | Last heard, active count, skip, priority, per-memory bandwidth and trigger recording | Low to moderate; optional voice detection has extra cost |
| 6 | SWL station directory with schedule and location filters | Show likely broadcasts now, language, transmitter and mode | Low CPU; data freshness/licensing work |
| 7 | Synchronous AM with selectable sideband | Experiment with fading broadcast reception and adjacent-channel interference | Moderate development; audio-rate DSP cost must be measured |
| 8 | Scheduled/triggered captures and SigMF export | Preserve interesting receptions with frequency, UTC, rate and annotations for other tools | Disk throughput and worker I/O |

Audio-first replay is practical: 60 seconds of mono 48 kHz float audio is 11.52 MB before metadata; stereo is twice that. Sixty seconds of complex float32 IQ at 800 kS/s is 384 MB, and at 3.2 MS/s is 1.536 GB. IQ replay should therefore be a later opt-in feature with a RAM limit or disk buffer. Retuning changes what is captured; replay cannot recover another band or earlier RF outside that captured span.

A notch helps narrow tones. It cannot cleanly remove a wandering 6 kHz interferer that overlaps wanted speech. The inspector can quantify drift, mark suspected RFI and compare before/after changes, but it cannot identify the physical source with certainty.

The existing SNR chart could gain an antenna-comparison workflow: timed A/B markers, medians and notes with identical gain, sample rate, bandwidth and FFT settings. Measure before denoising; post-NR displayed SNR is not evidence of a better antenna. Physical antenna switching remains manual unless switching hardware exists.

## Implementation boundaries

- Oz selected desktop audio rewind first. Sidebar organization, selected-VFO controls, processing status, memory search, notch and inspector remain future proposals.
- Keep existing ImGui/rendering and public Brown plugin layouts. New host state should remain outside exported objects. Avoid a toolkit rewrite for a visual refresh.
- Reuse existing FFT data and update diagnostic numbers at a bounded rate. Stop optional background analysis when off; refresh display overlays only when needed.
- Use preallocated DSP/ring storage and worker-thread file I/O. Keep mode/chain mutations on the UI control queue. Preserve device settings across bounded reconnect attempts.
- Keep existing accepted NR paths selectable. Test new audio methods with matched speech levels, weak consonants, fading, tones, CPU time, latency and frame rate before proposing a default change.
- Choose SIMD, Accelerate, Metal or other execution methods by measured end-to-end cost and output parity. Merely dispatching a small workload to a GPU can add overhead.
- Learn from designs independently. Review each component's license, dependencies and Mac maintenance before copying or linking code; OpenWebRX+ is AGPL, and the Qt projects are not ABI-compatible Beige plugins.
- Defer general AI signal identification, always-on voice transcription, large decoder bundles, full-band IQ rewind by default and a complete Metal renderer migration until a measured user need justifies them.
