# RTL-SDR USB hotplug

Read when: changing USB dependencies, device refresh, source lifecycle or macOS packaging.

## Failure and fix

The October 21 crash in Beige 1.3.11 was a null Darwin device interface in libusb's endpoint-zero control-transfer submission. Refresh called `selectById`, which opened the RTL-SDR and initialized its baseband registers. LLDB matched the crash instruction to `(*dpriv->device)->DeviceRequestAsyncTO`. Disconnect/reenumeration can clear that cached pointer.

Beige 1.3.13 builds a private libusb 1.0.30 with `patches/libusb-darwin-disconnect.patch`. It snapshots and retains the device interface under the same mutex used by cache teardown. A missing interface returns `LIBUSB_ERROR_NO_DEVICE`; a live snapshot stays retained until submission returns. This addresses the observed control-transfer crash, not every possible Darwin USB race.

The RTL-SDR source also clears missing selections/gains, initializes enumeration strings, updates the selected device index, uses a separate probe handle and checks empty gain tables. When async reading ends, the visible source menu stops playback and joins/closes the read handle before refreshing. Refresh cannot replace a running read handle.

## Build and package

Run `bash beige/build-macos.sh`. `build_libusb.py` downloads the official 1.0.30 release archive, verifies its pinned SHA-256, applies the patch and builds into `build-beige/usb-prefix`. It never changes Homebrew or Brown. `package_macos.py --usb-prefix ...` bundles this backend before resolving other dependencies, so carried-over plugins cannot select a different libusb by traversal order. The bundle contains its license, patch and source/patch checksums.

Source: [official libusb 1.0.30 release](https://github.com/libusb/libusb/releases/tag/v1.0.30). Local patch retained under libusb's LGPL license. Rebuild the dependency from the pinned archive plus the included patch; the resulting dynamic library remains replaceable.

## Verification

`darwin_disconnect_test.c` calls the actual Darwin backend with the missing-interface condition from the report. The pristine backend crashes under LLDB; the patched backend returns the disconnected error. Another 1,000 cases simulate cache removal during submission and check reference balance and both success/error returns. No hardware is opened by this test. The build runs it and registers it with CTest.

With the receiver available, launch the packaged app using a separate QA root and `--http PORT`. Run `python3 beige/tests/rtl_usb_smoke.py --url http://127.0.0.1:PORT`. It exercises ten refresh/start/stop cycles and checks that refresh is ignored while reading. `/proc/source/RTL-SDR/device` reports selection, gain count, running state and refresh count; posting `refresh` uses the production refresh/selection functions. Normal launches keep HTTP off.

Physical unplug/replug remains a separate hardware check: test while stopped and while receiving, refresh after reconnecting, then restart reception. Native fault injection does not establish behavior for every hub, timing or device.
