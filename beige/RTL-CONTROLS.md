# RTL-SDR tuner controls

Read when: changing source gain, tuner bandwidth, private RTL-SDR APIs or device preferences.

## Behavior

Beige 1.3.28 adds an Auto/Manual row to the desktop RTL-SDR Source panel. Auto is the default and retains the existing aggregate gain slider, separate Tuner AGC choice and sample-rate-based automatic hardware bandwidth. Returning to Auto restores those settings; it does not forget the saved Manual values.

Manual exposes LNA, mixer and VGA steps, each 0-15, and nominal tuner bandwidth presets from about 350 kHz to 2.43 MHz, plus Auto. The steps are register indices, not calibrated dB. Manual disables LNA/mixer AGC and analog VGA-pin AGC; RTL digital AGC remains a separate existing setting. More early gain can improve sensitivity but also increase overload. VGA controls level into the ADC and cannot recover sensitivity lost earlier.

Version 1.3.29 adds **Reset to defaults** below Tuner BW. It restores the standard LNA/mixer distribution for the saved aggregate Gain value, VGA step 8 and automatic bandwidth. Manual stays selected, and the restored values persist. It leaves frequency, tuning mode, AGC preferences and other source settings alone.

The controls require Beige's private driver extension and a detected R820T/R820T2 or R828D tuner. Direct sampling bypasses them. Other tuners and builds without the extension keep standard gain controls. Remote/server mode also retains its standard interface. V4 RF tracking, input switching, HF upconversion and notch routing remain automatic. No raw RF-filter sliders, analog IF AGC, harmonic reception or PLL-dithering controls are added.

Tuner BW changes the analog IF passband for the entire captured band, before digitization. It can attenuate blockers and off-center VFOs. It cannot undo overload that already occurred in the LNA/mixer or replace the much narrower demodulator filter. Auto follows the hardware sample rate, even when the displayed baseband is decimated. The presets are nominal driver settings, not measured calibrated widths.

The first live Manual selection snapshots the current gain distribution once. For stages already in manual gain mode, use the driver's programmed register values: the RF indicator keeps estimating AGC gains even in manual mode, and those estimates differ from the active manual setting. For AGC-controlled stages, use the indicator. This distinction was confirmed on the connected V4; the [R820T2 register description](https://www.rtl-sdr.com/wp-content/uploads/2016/12/R820T2_Register_Description.pdf) distinguishes RF_INDICATOR from manual gain registers. VGA is always taken from its programmed register.

Preferences live under each device's optional `manualTuner` object. Absent preferences select Auto. Manual settings reapply on start and retune. No gain telemetry is polled during drawing; USB control requests occur on user changes, tune/start or explicit QA readback. Failed operations show an error in the source panel. A new handle is required after USB recovery, as before.

## Driver and QA

`patches/rtlsdr-r82xx-gains.patch` adds typed gain-stage setters/getters after the existing async-lifetime patch to pinned RTL-SDR 2.0.3. Masked writes preserve V4 routing, filter and PLL bits. The getter combines programmed manual values with AGC status; it is not independent analog gain measurement. Both patches and their checksums ship in the bundle. Brown and Homebrew remain unchanged.

The normal build runs the native and ASAN gain harnesses through CTest. They exercise all 4,096 stage combinations using production register code, preserve unrelated registers, simulate AGC estimates different from manual values, and reject invalid/unsupported/disconnected requests. A separate preference test covers old profiles, malformed values, atomic updates, one-time snapshot and saved-mode recovery.

Launch an isolated RTL-SDR/Radio/NR/Null Audio Sink profile with `--http PORT`, then run:

```sh
python3 beige/tests/rtl_tuner_smoke.py --url http://127.0.0.1:PORT
python3 beige/tests/rtl_usb_smoke.py --url http://127.0.0.1:PORT --interrupt
```

The QA-only `/proc/source/RTL-SDR/tuner` endpoint reports saved preferences, capability, bypass and error state. `active_bandwidth` is zero in Auto even while a Manual width remains saved. POST a JSON object with `enabled`, `lna`, `mixer`, `vga` and/or `bandwidth`; stage edits require Manual. POST `readback` for an explicit driver snapshot. Normal launches leave HTTP disabled.

POST `reset_defaults` invokes the same reset used by the button. It only operates on supported, active Manual controls and reapplies bandwidth before the gain stages.

## Hardware evidence and limits

On October 22, the connected Blog V4 at 3.2 MS/s and 17.490 MHz responded independently to all three stage controls. A repeated reference setting measured 7.55 and 7.60 RMS ADC counts; lowering LNA gave 1.59, lowering mixer gave 2.79, and raising VGA gave 19.04. These uncalibrated live captures verify control effect, not sensitivity or audio-quality gains.

With identical stages, a separate Auto/350 kHz/900 kHz/Auto sequence changed the received passband and restored the initial response. The 350 kHz preset reduced median energy 0.5-0.7 MHz from center by roughly 9.5 dB; near-center energy also fell about 3.4 dB. ADC clipping was absent in those captures. No controlled wanted-signal SNR or listening improvement is claimed. Default bandwidth remains Auto.

The packaged receiver passed gain snapshot, live edits, bandwidth changes, invalid-request rejection, Auto restoration, finite RF audio, application-reopen persistence, ten normal restarts and ten interrupted-reader recovery cycles with the sidebar hidden. A longer exploratory stream ended with driver result -1 and stopped cleanly; this is retained as a stability limitation rather than attributed to a proven cause. Physical hotplug timing, calibrated passband width, VHF/UHF and other tuner hardware still need separate coverage.

A later 130-second Auto/Manual/Auto/Manual comparison ran continuously without a reader exit. Four 25-second measured intervals reported median UI rates of 85.5/78.9/72.6/87.5 FPS and process CPU of 25.6/24.5/21.1/31.1 percent of one core. The isolated four-module profile used 3.2 MS/s, 4x display decimation, a 65,536-point FFT and the 3D view, with 47-52 retained history rows. These variable local timings cannot establish a small performance difference; neither mode polls USB during UI drawing. Oz confirmed physical Auto/Manual switching works correctly in 1.3.28.

Live jumps through 40m, 2m, 70cm and 23cm retained programmed gains with continuing finite audio. This checks retuning across V4 input bands, not reception quality or filter shape on VHF/UHF. Installation preserved 137 checked profile, resource and private-plugin files byte-for-byte. The normal profile received at its existing tune through its existing Audio sink, and live Auto/Manual/Auto switching retained the standard gains. Native screenshots confirmed the expanded layout; physical mouse acceptance is a separate check.

The 1.3.29 reset addition passes the same twenty CTests, ABI and signature gates. Live QA confirms Reset restores the standard gain stages and automatic bandwidth while retaining Manual and delivering finite RF audio. The button layout was inspected. Installation again preserves all 137 checked files, including Oz's intervening Manual and tuning choices. The normal installed receiver starts with those saved choices and its existing Audio sink. All Beige instances are closed after testing.
