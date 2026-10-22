#!/bin/bash
set -euo pipefail
beige_root="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$beige_root/beige/build_libusb.py" "$beige_root/build-beige"
python3 "$beige_root/beige/build_rtlsdr.py" "$beige_root/build-beige"
python3 "$beige_root/beige/build_neural_audio.py" "$beige_root/build-beige"
export PKG_CONFIG_PATH="$beige_root/build-beige/rtl-prefix/lib/pkgconfig:$beige_root/build-beige/usb-prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cmake -S "$beige_root" -B "$beige_root/build-beige" -G Ninja \
    -U '*LIBUSB*' \
    -U '*LIBRTLSDR*' \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_INSTALL_PREFIX="$beige_root/build-beige/install" \
    -DBEIGE_ONNXRUNTIME_ROOT="$beige_root/build-beige/neural-dependency/onnxruntime-osx-arm64-1.31.0" \
    -DUSE_BUNDLE_DEFAULTS=ON -DOPT_BUILD_PLUTOSDR_SOURCE=OFF \
    -DOPT_BUILD_HACKRF_SOURCE=OFF -DOPT_BUILD_AUDIO_SOURCE=OFF \
    -DOPT_BUILD_AUDIO_SINK=OFF -DOPT_BUILD_PORTAUDIO_SINK=ON \
    -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON \
    -DOPT_BUILD_MPEG_ADTS_SINK=OFF -DOPT_BUILD_SOAPY_SOURCE=ON
cmake --build "$beige_root/build-beige" -j 6
ctest --test-dir "$beige_root/build-beige" --output-on-failure
python3 "$beige_root/beige/check_plugin_abi.py"
cmake --install "$beige_root/build-beige"
python3 "$beige_root/beige/package_macos.py" "$beige_root/build-beige" --brown-app '/Applications/SDR++Brown.app' \
    --usb-prefix "$beige_root/build-beige/usb-prefix" --rtl-prefix "$beige_root/build-beige/rtl-prefix"
