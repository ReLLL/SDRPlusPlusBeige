# Desktop audio rewind

The Live action also ends optional [waterfall IQ replay](IQ-REPLAY.md), even with audio history disabled or empty. Other audio controls and live-history WAV exports remain independent of the IQ cursor. The historical Radio listening decoder is selected after live audio capture, so it cannot overwrite the audio-history ring or Recorder taps.

Read when: changing audio transport, retained history, WAV export, sink lifecycle or the desktop rewind strip.

## User behavior

Version 1.3.34 provides -3s, -5s, -10s and -30s shortcuts below the desktop waterfall, alongside pause/resume, scrubbing and an explicit Live action. Each shortcut steps backward from the current playback position and resumes, limited to available history. The selected audio receiver retains 60 seconds in memory by default. Wide windows expose Save 30s and Save 60s directly; smaller windows place both in Save.... Below 700 scaled pixels of strip width, the timeline/save controls wrap onto their own row. The status distinguishes live audio, replay, pause and stopped RF. Transport buttons have fixed widths so Pause/Play and changing time values do not move surrounding controls.

Replaying or pausing affects the selected listening output. RF acquisition, spectrum/waterfall, live decoder inputs and the ordinary Recorder tap continue on their existing live paths. After reception stops, retained audio can still play; reaching its end pauses safely. Returning to Live selects current audio and clears pause.

Saving exports the latest received audio ending at the click time, independent of the playback cursor. Short history produces a shorter clip, with the actual duration shown after saving. Exports are stereo IEEE float32 WAV files under the active Beige profile's `recordings/` folder, with a UTC timestamp and receiver name. They include the already-applied audio NR and precede the speaker volume/mute control. Replay does not rerun NR or recover speech already removed by a reducer. Show reveals the last successful clip in Finder on macOS.

The feature is enabled by default without writing a new preference during startup. An explicit Rewind checkbox change persists `audioRewind`. Turning it off returns to live audio and releases retained storage. Switching the selected receiver, changing audio sample rate or entering/leaving RAW clears its history; ordinary retuning retains earlier audio. Thus a clip can include more than one frequency, and filenames intentionally do not claim a single RF frequency. Unsupported rates outside 8-192 kHz bypass history. RAW never enters the audio-history buffer.

## Ownership and signal path

`SinkManager::Stream` keeps its original exported layout. Core-owned sidecar processors connect between each stream's listening `volumeInput` branch and `volumeAjust`. The existing live splitter branches are unchanged. Only the selected eligible receiver allocates history; unselected outputs pass through. New output buffers hold 4,096 stereo frames and oversized input blocks are split safely.

`audio/rewind_buffer.h` owns a fixed ring, absolute received-frame positions and a separate playback cursor. Live output is bit-identical. Seeking, resuming and returning to Live use a five-millisecond transition to reduce clicks. Pause outputs silence while incoming samples continue to advance the ring. At the retention limit, expired history is dropped and the paused cursor clamps to the oldest retained position.

The physical ring contains 65 seconds: 60 seconds exposed to transport plus five seconds of export headroom. At 48 kHz this is 24,960,000 bytes (about 25 MB), preserving stereo. At the supported 192 kHz maximum it is 99,840,000 bytes. Steady-state processing copies existing audio; it adds no FFT or neural inference and makes no recurring ring allocation. A mutex protects short copies and controls. Export memory is allocated on an owned asynchronous worker before its snapshot lock; filesystem creation and writing also run there. Only one export runs at a time. The extra headroom supports ordinary real-time click-to-snapshot scheduling; an unusually late worker can save less retained audio rather than read overwritten storage.

The sidecar's ordinary DSP worker follows the live input rate. With retained history, stopped-RF output uses paced ten-millisecond chunks, including silence while paused or live. An atomic stream key lets the audio callbacks consume retained output when RF is stopped, without looking up UI state or locking the history. PortAudio callbacks read only available data in this state. CoreAudio keeps output available while closing microphone input at RF stop. The additive, nonvirtual `stream::readFor` method supports bounded waits without adding object fields or changing virtual tables. On teardown, listening consumers stop before rewind output storage is destroyed. A pending export is joined before shutdown completes. Rate changes and stream removal invalidate snapshots safely.

## Regression checks

`audio_rewind_test.cpp` covers stereo/live parity, ring wrap, exact delayed samples, pause retention, replay with stopped input, click-time 30/60-second snapshots, rate changes, disabled/unsupported rates and concurrent capture/export. It runs normally and with AddressSanitizer.

`audio_rewind_smoke.py` runs the actual packaged core with only File Source, Radio, Noise Reduction and Null Audio Sink:

```sh
python3 beige/tests/audio_rewind_smoke.py --url http://127.0.0.1:18877 \
  --fixture /path/to/baseband.wav
```

It verifies continued capture while paused, stereo float WAV headers and finite nonzero audio, exact agreement between the last 30 seconds of the 60-second export and the separate 30-second export, stopped-RF replay, memory release, RAW bypass and recovery. In a `/tmp` QA profile it also forces an export-folder error and verifies the receiver survives and a subsequent save succeeds. Never run synthetic reception against the normal private profile.

QA endpoints are under `/proc/audio-rewind/`: `status`, `seek` (seconds behind live), `paused`, `live`, `enabled`, `save` (30 or 60) and `export`. All mutations use the existing UI request queue. `status` exposes actual received frames, available history, delay, rate, memory and RF state; transport labels are not used as proof that processing occurred.

Local File tests pass, including stopped replay and bit-exact export overlap. Native mouse interaction verifies rewind/pause actions. On the isolated live RTL-SDR/Radio/NR/Null receiver, matched off/live/replay phases measure median 114.5-116.1 FPS, with zero invalid audio samples. Process CPU varies from 22.7% to 25.5% of one core; these short runs do not resolve a small CPU difference. The old-build baseline has a different observed frame-rate ceiling, so it does not establish a cross-build FPS improvement. A normal native ring benchmark reports about 0.0022% wall processing time per audio-second, excluding stream/thread/UI overhead. A live 30-second RF export and stopped-RF replay also pass.

Private logs, comparison results, exported QA clips and Time Profiler capture: `/tmp/beige-audio-rewind-20261022/`. Listening acceptance is separate from these numerical and transport checks.

The real-output regression `audio_rewind_output_smoke.py --url http://127.0.0.1:18879` exercises the selected Audio sink without changing receiver settings. `--sink 'New Audio'` and `--sink CoreAudio` select alternate expected providers in isolated profiles. The initial 1.3.31 package fails stopped replay because the old output callback mutes without draining its ring whenever RF stops. A native thread sample confirms the rewind worker blocked on output backpressure. Version 1.3.32 permits retained output through that gate and emits paced silence between replay operations. The regression checks continued capture during pause, stopped replay, pause/resume, end-of-history pause, unchanged captured frames and return to Live.

The 1.3.33 gate contains 24 native/ASAN tests and preserves the public plugin ABI. Final File export checks and 56 NR mode recoveries plus 100 concurrent request pairs pass. During that qualification, an independent spectrum-edge overread reproduced the supplied 1.3.32 crash report; the bounded measurement fix and its regression are documented in [VALIDATION.md](VALIDATION.md). Audio, New Audio and CoreAudio each pass real-output transport checks; CoreAudio was tested at 44.1 kHz, the other two at 48 kHz. This confirms transport and callback consumption, not audible fidelity.

Final 1.3.33 live RF comparison with rewind off, Live and Replay measures median 116.4-118.1 FPS and 19.9-22.6% of one core, with no invalid samples. No material frame-rate loss is observed; a small CPU difference remains unresolved. Installed 1.3.33 passes the normal-profile Audio output regression. All 137 checked profile/plugin/resource files match their baseline after testing, and all Beige instances are closed after automated testing. Oz subsequently answered "Works well" when asked whether replay preserves voice clarity and returns cleanly to live audio. This accepts those listening behaviors; it does not qualify long-run stability.

Version 1.3.34 adds the three requested shortcuts while retaining -10s and both WAV save actions. Native clicks verify cumulative replay delays of 3, 8 and 38 seconds after -3s, -5s and -30s; -10s from Live still produces ten seconds of delay. Layouts at 900, 1120 and 1400 pixels fit the controls, including the wrapped narrow layout. A compact-menu Save 60s produces a valid 23,040,056-byte stereo float WAV, and direct Save 30s produces 11,520,056 bytes at 48 kHz. The packaged export regression again passes exact 30/60-second overlap, finite audio, error recovery and transport checks. All 24 CTests, ABI and signatures pass. Installation and normal-profile startup preserve all 137 checked files; the app closes after testing.
