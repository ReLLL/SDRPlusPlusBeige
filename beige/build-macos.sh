#!/bin/bash
set -euo pipefail
beige_root="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$beige_root" -B "$beige_root/build-beige" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_INSTALL_PREFIX="$beige_root/build-beige/install" \
    -DUSE_BUNDLE_DEFAULTS=ON -DOPT_BUILD_PLUTOSDR_SOURCE=OFF \
    -DOPT_BUILD_HACKRF_SOURCE=OFF -DOPT_BUILD_AUDIO_SOURCE=OFF \
    -DOPT_BUILD_AUDIO_SINK=OFF -DOPT_BUILD_PORTAUDIO_SINK=ON \
    -DOPT_BUILD_M17_DECODER=ON -DOPT_BUILD_CH_EXTRAVHF_DECODER=ON \
    -DOPT_BUILD_MPEG_ADTS_SINK=OFF -DOPT_BUILD_SOAPY_SOURCE=ON
cmake --build "$beige_root/build-beige" -j 6
ctest --test-dir "$beige_root/build-beige" --output-on-failure
python3 "$beige_root/beige/check_plugin_abi.py"
cmake --install "$beige_root/build-beige"
python3 "$beige_root/beige/package_macos.py" "$beige_root/build-beige" --brown-app '/Applications/SDR++Brown.app'
