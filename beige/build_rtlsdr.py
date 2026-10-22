#!/usr/bin/env python3
"""Build pinned RTL-SDR with safe async cleanup; leave Homebrew and Brown alone."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

VERSION = '2.0.3'
SHA256 = '851b87a62e548470c287c26669b83abb665d83bccb8d8492d07a697c7b9c4e37'
URL = f'https://github.com/steve-m/librtlsdr/archive/refs/tags/v{VERSION}.tar.gz'


def run(*args, **kwargs):
    subprocess.run(args, check=True, **kwargs)


def build_dependency(build):
    root = Path(__file__).resolve().parents[1]
    build = build.resolve()
    work, prefix = build/'rtl-dependency', build/'rtl-prefix'
    source = work/f'librtlsdr-{VERSION}'
    patch = root/'beige/patches/rtlsdr-async-lifetime.patch'
    gain_patch = root/'beige/patches/rtlsdr-r82xx-gains.patch'
    fingerprint = {'version':VERSION, 'archive_sha256':SHA256,
                   'patch_sha256':hashlib.sha256(patch.read_bytes()).hexdigest(),
                   'gain_patch_sha256':hashlib.sha256(gain_patch.read_bytes()).hexdigest()}
    receipt = prefix/'beige-rtlsdr.json'
    work.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PKG_CONFIG_PATH=str(build/'usb-prefix/lib/pkgconfig'))
    if not receipt.exists() or json.loads(receipt.read_text()) != fingerprint:
        archive = work/f'rtl-sdr-{VERSION}.tar.gz'
        if not archive.exists():
            with urllib.request.urlopen(URL,timeout=60) as response:
                archive.write_bytes(response.read())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('RTL-SDR source checksum mismatch')
        if source.exists():
            source.rename(work/f'previous-{datetime.datetime.now():%Y%m%dT%H%M%S%f}')
        run('tar','-xzf',str(archive),'-C',str(work))
        run('patch','-p1','-i',str(patch),cwd=source)
        run('patch','-p1','-i',str(gain_patch),cwd=source)
        run('cmake','-S',str(source),'-B',str(source/'build'),'-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OSX_ARCHITECTURES=arm64',
            '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0','-DCMAKE_INSTALL_PREFIX='+str(prefix),
            '-DDETACH_KERNEL_DRIVER=ON','-DENABLE_ZEROCOPY=OFF',env=environment)
        run('cmake','--build',str(source/'build'),'-j','6')
        run('cmake','--install',str(source/'build'))
        licenses = prefix/'share/licenses/rtlsdr'
        licenses.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source/'COPYING',licenses/'COPYING')
        receipt.write_text(json.dumps(fingerprint,indent=2)+'\n')
    for test_name, asan in [('rtl_async_lifetime',False),('rtl_async_lifetime',True),
                            ('rtl_gain_stages',False),('rtl_gain_stages',True)]:
        target = build/('beige_'+test_name+('_asan' if asan else '_test'))
        command = ['clang','-g','-O1','-UNDEBUG',
            '-DRTLSDR_SOURCE_FILE="'+str(source/'src/librtlsdr.c')+'"',
            '-I'+str(source/'include'),'-I'+str(source/'src'),'-I'+str(source/'build/src'),
            '-I'+str(build/'usb-prefix/include/libusb-1.0'),str(root/'beige/tests'/(test_name+'_test.c'))]
        command += [str(source/'src'/name) for name in
                    ['tuner_e4k.c','tuner_fc0012.c','tuner_fc0013.c','tuner_fc2580.c','tuner_r82xx.c']]
        command += ['-L'+str(build/'usb-prefix/lib'),'-lusb-1.0','-o',str(target)]
        if asan:
            command += ['-fsanitize=address','-fno-omit-frame-pointer']
        run(*command)
        run(str(target))
    print(f'Private RTL-SDR {VERSION}: native/ASAN lifetime and gain regressions passed.')


if __name__ == '__main__':
    build_dependency(Path(sys.argv[1]))
