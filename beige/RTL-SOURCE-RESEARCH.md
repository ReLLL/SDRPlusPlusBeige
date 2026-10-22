# RTL-SDR source extension review

Read when: adding RTL-SDR gain, hardware filtering, telemetry or AGC controls.

Reviewed October 22, 2026 against installed Beige 1.3.27 and its private RTL-SDR 2.0.3 driver. This is a source/API review, not an RF improvement measurement. No installed app, receiver preference or hardware register was changed.

Subsequent implementation: 1.3.28 adds optional Manual stage gain and nominal hardware bandwidth controls while keeping Auto as the default. V4 RF filters stay automatic. See [RTL-CONTROLS.md](RTL-CONTROLS.md) for behavior, hardware measurements and limits; the findings below describe the preceding review.

## Findings

The requested [older module](https://github.com/Sultan-papagani/sdrpp_new_rtlsdr_source) marks itself incompatible with current SDR++ nightlies and links to its [replacement](https://github.com/Sultan-papagani/sdrpp_rtlsdr_source). Its controls make real driver calls, but neither module is a drop-in replacement for Beige. Symbol inspection finds five missing macOS extension functions in the older module and six in the replacement; Android-only open functions were excluded. The replacement specifically depends on the author's modified old-dab driver, including an extra DAGC readout function.

The installed source already has aggregate tuner gain, tuner AGC, RTL digital AGC, bias power, PPM, direct sampling and offset tuning. It always requests automatic tuner bandwidth. The driver already exports `rtlsdr_set_tuner_bandwidth`; exposing that requires no wholesale driver replacement. Its R82xx table reaches nominal 350 kHz. Automatic bandwidth follows the hardware sample rate, not the post-capture decimation or Radio demodulator bandwidth.

The current aggregate manual gain divides gain between LNA and mixer while holding VGA fixed. Separate stages therefore add actual control. The existing exported `rtlsdr_set_tuner_if_gain` is not an R828D VGA setter: only the E4000 interface supplies that callback, and an unsupported call can return success without doing anything. Add an explicit R82xx capability/API rather than relying on that function.

## Hardware boundaries

[RTL-SDR Blog's V4 design](https://www.rtl-sdr.com/rtl-sdr-blog-v4-dongle-initial-release/) includes HF upconversion, three input bands and modest broadcast rejection. Beige's current driver already switches these automatically. The V4 HF path deliberately bypasses the tuner's RF tracking filter and resets register 0x1B on retunes. Preserve that routing and GPIO behavior.

The four exposed filter sliders are not four freely tunable antenna band-pass filters. The [driver register map](https://github.com/old-dab/rtlsdr/blob/master/src/tuner_r82xx.c) distinguishes IF fine/coarse bandwidth and HP corner from RF tracking LPF/LPNF controls. The older module even labels a write to the IF HP corner as LPF. A hardware bandwidth control is the clearer first feature; raw tracking/calibration registers belong in later measured experiments.

An IF filter can reduce unwanted energy before digitization, but cannot undo LNA/mixer overload that occurred earlier. It acts on the whole captured band and every VFO. A nominal 350 kHz hardware filter also cannot replace a 2.8 kHz SSB demodulator filter. Front-end attenuation or external filtering can still be necessary.

The [V4 manufacturer guide](https://www.rtl-sdr.com/V4/) explicitly says to leave IF AGC off. [Old-dab's explanation](https://github.com/old-dab/rtlsdr/blob/master/README_improvements.md) identifies the missing RC capacitor needed for analog VGA control. This restriction concerns analog IF/VGA AGC, not ordinary LNA/mixer AGC or a software controller. Manual VGA remains a candidate.

## Candidates, in implementation order

| Feature | Assessment | Integration |
| --- | --- | --- |
| Hardware bandwidth: Auto/manual presets | Highest-value first step; current driver already supports it | Keep Auto default. Show nominal/applied width and the effect on off-center VFOs. Reapply after rate/reconnect changes. |
| Separate LNA, mixer and VGA gain | Useful control over sensitivity versus headroom | Keep Basic gain as default. Expert mode needs typed, masked driver operations preserving V4 input bits. Label stage indices and estimated dB honestly. |
| Clipping/headroom and gain readouts | Useful feedback while tuning gain | ADC rail-hit statistics are a clipping proxy, not a complete LNA overload detector. Cache tuner telemetry and poll slowly outside UI drawing. |
| Visible source health and last USB error | Explains an unexpected stream stop | Show receiving/stopped/disconnected/error using the existing worker result. Do not advertise an exact lost-sample count without reliable timing/sequence evidence. |
| Mixer sideband and coordinated IF positioning | Can place a steep filter edge toward a blocker or move an image | Preserve RF frequency and spectrum orientation. This is independent of USB/LSB voice modulation. Use a coordinated driver operation, not an arbitrary IF register box. |
| Software IF gain control and AGC timing | Potential convenience; hardware dependent | Optional, measured for pumping and adjacent blockers. Keep analog IF AGC unavailable on the V4. |
| Fractional PPM/ppb correction and hardware FIR profiles | Useful later for calibration and alias rejection | Port only validated pieces. Do not confuse more gain or more correction digits with extra ADC resolution. |

Defaults and profiles must survive switching Basic/Expert, retuning, source restart and device changes. Preserve center/free tuning mode. Restrict advanced capabilities to detected supported hardware and provide one reset to standard behavior.

## Other repositories checked

- [old-dab/rtlsdr](https://github.com/old-dab/rtlsdr): gain/overload reporting, software gain control, narrower hardware FIR profiles, finer frequency correction and image-rejection calibration. Evaluate image calibration later without automatically writing EEPROM. Its Windows-specific transport work is not a macOS performance result.
- [librtlsdr/librtlsdr](https://github.com/librtlsdr/librtlsdr/blob/master/README_improvements.md): bandwidth readback, band-center/sideband APIs, split gains, PLL dithering and harmonic reception. Dithering concerns the tuner PLL, not audio NR. Harmonic reception needs RF validation and often extra filtering/amplification. The older module still uses the 32-bit frequency API despite its 6 GHz claim; that path cannot represent 6 GHz directly.
- [hayguen/ExtIO_RTL](https://github.com/hayguen/ExtIO_RTL): useful bandwidth/IF-control UI ideas and a warning about R828D IF-AGC pumping. Its RTL2832 impulse cancellation option is an experimental lead, not a proven HF noise-reduction upgrade. The Windows ExtIO binary cannot be used directly in Beige on macOS.
- [SDRangel's RTL source](https://github.com/f4exb/sdrangel/blob/master/plugins/samplesource/rtlsdr/readme.md): explicit source error feedback and separate hardware/baseband rate and decimation concepts. Beige already has decimation and IQ correction; avoid presenting those as new hardware capabilities.
- [RTL-SDR Blog driver](https://github.com/rtlsdrblog/rtl-sdr-blog): reference for V4 input, upconverter and notch routing. These should remain automatic unless a specific override has a measured benefit.

## Integration and validation

Port selected features into Beige's existing source and patched private driver. Preserve USB callback draining, Darwin disconnect protection and plugin ABI. The replacement module reads gain/register state in its render path; copying that polling would add USB traffic and potentially block the UI. Apply controls on change, serialize device operations, and use cached telemetry at roughly 2-5 Hz.

Start with bandwidth, expert gains and clipping feedback. Run existing native USB/ASAN tests, ABI/package checks and repeated start/stop/reconnect tests. Compare identical RF captures with fixed gain, rate and span; measure passband shape, wanted-signal SNR, images, clipping and CPU/FPS. Test HF and VHF/UHF separately, including a VFO away from source center. A louder waterfall is not evidence of improved sensitivity. No RF A/B result is claimed by this review.

Reviewed revisions: older module `0434ef7f8cc2`, replacement `f8d5e73a31e6`, its driver fork `aebeadc7ec21`, old-dab `ebb000f916b8`, librtlsdr `cd36c2881559`, RTL-SDR Blog `aed0ea19f3a2`, ExtIO `c801cfb5e8f9`, SDRangel `4a78beb3a351`.
