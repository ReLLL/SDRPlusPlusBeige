#!/usr/bin/env python3
"""Build and test Beige's private hotplug-safe libusb; never alter Homebrew."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import urllib.request

VERSION = '1.0.30'
SHA256 = 'fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf'
URL = f'https://github.com/libusb/libusb/releases/download/v{VERSION}/libusb-{VERSION}.tar.bz2'


def run(*args, cwd=None, env=None):
    subprocess.run(args, cwd=cwd, env=env, check=True)


def build_dependency(build):
    root = pathlib.Path(__file__).resolve().parents[1]
    build = build.resolve()
    work = build / 'usb-dependency'
    prefix = build / 'usb-prefix'
    work.mkdir(parents=True, exist_ok=True)
    patch = root / 'beige/patches/libusb-darwin-disconnect.patch'
    fingerprint = {'version': VERSION, 'archive_sha256': SHA256,
                   'patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest()}
    receipt = prefix / 'beige-libusb.json'
    source = work / f'libusb-{VERSION}'
    if not receipt.exists() or json.loads(receipt.read_text()) != fingerprint:
        archive = work / f'libusb-{VERSION}.tar.bz2'
        if not archive.exists():
            with urllib.request.urlopen(URL, timeout=60) as response:
                archive.write_bytes(response.read())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
            raise RuntimeError('libusb archive checksum mismatch')
        if source.exists():
            # Preserve previous work when patch contents change.
            import datetime
            source.rename(work / f'previous-{datetime.datetime.now():%Y%m%dT%H%M%S%f}')
        run('tar', '-xjf', str(archive), '-C', str(work))
        run('patch', '-p1', '-i', str(patch), cwd=source)
        env = dict(os.environ, CFLAGS='-O2 -g -mmacosx-version-min=15.0')
        run('./configure', '--prefix=' + str(prefix), '--enable-shared', '--enable-static',
            '--disable-examples-build', '--disable-tests-build', cwd=source, env=env)
        run('make', '-j', '6', cwd=source)
        run('make', 'install', cwd=source)
        licenses = prefix / 'share/licenses/libusb'
        licenses.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'COPYING', licenses / 'COPYING')
        receipt.write_text(json.dumps(fingerprint, indent=2) + '\n')
    test = build / 'beige_darwin_disconnect_test'
    run('clang', '-g', '-UNDEBUG', '-I', str(source), '-I', str(source / 'libusb'),
        str(root / 'beige/tests/darwin_disconnect_test.c'),
        str(prefix / 'lib/libusb-1.0.a'), '-framework', 'IOKit', '-framework',
        'CoreFoundation', '-framework', 'Security', '-lobjc', '-o', str(test))
    run(str(test))
    print(f'Private libusb {VERSION} built and disconnect regression passed: {prefix}')


if __name__ == '__main__':
    build_dependency(pathlib.Path(sys.argv[1]))
