# RTL-SDR USB hotplug

Read when: changing USB dependencies, device refresh, source lifecycle or macOS packaging.

## Failure and fix

The October 21 crash in Beige 1.3.11 was a null Darwin device interface in libusb's endpoint-zero control-transfer submission. Refresh called `selectById`, which opened the RTL-SDR and initialized its baseband registers. LLDB matched the crash instruction to `(*dpriv->device)->DeviceRequestAsyncTO`. Disconnect/reenumeration can clear that cached pointer.

Beige 1.3.13 builds a private libusb 1.0.30 with `patches/libusb-darwin-disconnect.patch`. It snapshots and retains the device interface under the same mutex used by cache teardown. A missing interface returns `LIBUSB_ERROR_NO_DEVICE`; a live snapshot stays retained until submission returns. This addresses the observed control-transfer crash, not every possible Darwin USB race.

The RTL-SDR source also clears missing selections/gains, initializes enumeration strings, updates the selected device index, uses a separate probe handle and checks empty gain tables. Refresh cannot replace a running read handle. Before 1.3.26, automatic stop handling depended on the Source menu being visible.

## Stream stop/restart crash (1.3.27)

The October 22 report from 1.3.25 crashes in libusb's `list_add_tail` during `rtlsdr_close` after the reader stops. The pinned RTL-SDR 2.0.3 driver can free submitted transfers after an event-loop error or device loss before their cancellation callbacks arrive. That leaves libusb's active-transfer list pointing at freed storage. Closing the tuner submits a control request through that list.

`rtlsdr-async-lifetime.patch` counts submitted transfers until their callbacks finish, handles initial and callback resubmission failures, and drains cancellations even after device loss or transient event errors. Cancellation request success and the transfer's old status are not completion evidence. Buffers are freed only after the pending count reaches zero. A permanently broken backend that never delivers completions may still stall teardown; freeing live transfers after an arbitrary deadline would reintroduce the crash.

The source joins the drained worker before closing the handle. It retries cancellation while the worker enters async reading, records the reader result, and reconciles unexpected exits on the UI frame event even with the sidebar hidden. It returns playback to stopped without immediately probing a failing device. The next explicit start or Refresh re-enumerates it. No source settings or persistent tuning-mode preferences change.

The HTTP playback status previously cached only HTTP start/stop requests. An interrupted-stream test found that it still reported playing after the UI and worker had stopped correctly. Version 1.3.27 publishes status from `MainWindow::setPlayState`, so UI clicks, automatic stops and HTTP commands agree.

The private driver comes from the [official 2.0.3 archive](https://github.com/steve-m/librtlsdr/releases/tag/v2.0.3), pinned by SHA-256 in `build_rtlsdr.py`. A [related upstream fix](https://github.com/librtlsdr/librtlsdr/commit/56e4b80a1b042e55882aee28797338bdb9b4d6c0) confirms the event-error use-after-free mechanism; Beige also tracks delayed completions after device loss and resubmission failure.

The build installs into `build-beige/rtl-prefix`. Packaging selects this driver before resolving any stock or carried-over plugin dependency, includes its GPL license, patch and archive/patch checksums, and retains the private patched libusb. Homebrew and Brown remain unchanged.

`rtl_async_lifetime_test.c` compiles the actual pinned driver with delayed USB completions. The pristine driver reproducibly frees pending transfers on hard event errors and device loss; that failing test was committed before the fix. The patched driver passes seven scenarios repeated 100 times each, both normally and under AddressSanitizer: event error, device loss, partial submission failure, intentional cancel, resubmission failure, repeated event errors and interrupted-event retry. Two CTests cover these runs. No hardware is opened by the native harness.

Use `rtl_usb_smoke.py --interrupt` with an isolated RTL-SDR/Radio/NR/Null Sink profile to add ten reader cancellation/automatic-stop/restart cycles. Test with Source collapsed and `showMenu=false`. `/proc/source/RTL-SDR/device` now also reports `read_result` and `worker_ended`; its QA-only `cancel_read` action ends the production reader without unplugging USB.

The installed 1.3.27 passes seventeen CTests, ABI comparison and signature/dependency checks. The initially detected dongle returned USB register timeouts; the patched app logged reader error -9, stopped and exited without crashing. Oz reconnected it physically. With exclusive access to that dongle, ten normal refresh/start/stop cycles and ten interrupted-reader recovery cycles pass with audio samples after every restart and both Source and sidebar hidden. One extra final start/stop also passes. A separate normal-profile Beige instance must be closed before these tests because it can claim the tuner. These checks do not reproduce every physical hotplug timing or establish long-run USB stability.

## Build and package

Run `bash beige/build-macos.sh`. `build_libusb.py` downloads the official 1.0.30 release archive, verifies its pinned SHA-256, applies the patch and builds into `build-beige/usb-prefix`. It never changes Homebrew or Brown. `package_macos.py --usb-prefix ...` bundles this backend before resolving other dependencies, so carried-over plugins cannot select a different libusb by traversal order. The bundle contains its license, patch and source/patch checksums.

Source: [official libusb 1.0.30 release](https://github.com/libusb/libusb/releases/tag/v1.0.30). Local patch retained under libusb's LGPL license. Rebuild the dependency from the pinned archive plus the included patch; the resulting dynamic library remains replaceable.

## Verification

`darwin_disconnect_test.c` calls the actual Darwin backend with the missing-interface condition from the report. The pristine backend crashes under LLDB; the patched backend returns the disconnected error. Another 1,000 cases simulate cache removal during submission and check reference balance and both success/error returns. No hardware is opened by this test. The build runs it and registers it with CTest.

With the receiver available, launch the packaged app using a separate QA root and `--http PORT`. Run `python3 beige/tests/rtl_usb_smoke.py --url http://127.0.0.1:PORT`. It exercises ten refresh/start/stop cycles and checks that refresh is ignored while reading. `/proc/source/RTL-SDR/device` reports selection, gain count, running state and refresh count; posting `refresh` uses the production refresh/selection functions. Normal launches keep HTTP off.

Physical unplug/replug remains a separate hardware check: test while stopped and while receiving, refresh after reconnecting, then restart reception. Native fault injection does not establish behavior for every hub, timing or device.
