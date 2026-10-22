#!/usr/bin/env python3
"""Package a newly built Beige app without touching an installed SDR application."""
import argparse
import datetime
import pathlib
import plistlib
import re
import shutil
import subprocess


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def package(build, brown_app=None, usb_prefix=None, rtl_prefix=None):
    root = pathlib.Path(__file__).resolve().parents[1]
    build = build.resolve()
    install = build / 'install'
    app = build / 'SDR++Beige.app'
    if not (install / 'bin/sdrpp').is_file():
        raise SystemExit('Run cmake --install on this build first.')
    if app.exists():
        stamp = datetime.datetime.now().strftime('%Y%m%dT%H%M%S%f')
        app.rename(build / f'SDR++Beige.app.backup-{stamp}')
    contents = app / 'Contents'
    for directory in ('MacOS', 'Frameworks', 'Plugins', 'Resources'):
        (contents / directory).mkdir(parents=True)
    shutil.copytree(root / 'root/res', contents / 'Resources', dirs_exist_ok=True)
    shutil.copy2(root / 'license', contents / 'Resources/COPYING')
    neural = build / 'neural-dependency'
    if (neural / 'gtcrn.onnx').exists():
        models = contents / 'Resources/models'
        models.mkdir()
        shutil.copy2(neural / 'gtcrn.onnx', models / 'gtcrn.onnx')
        shutil.copy2(neural / 'GTCRN-LICENSE', contents / 'Resources/GTCRN-LICENSE')
        shutil.copy2(neural / 'provenance.json', contents / 'Resources/neural-audio-provenance.json')
        runtime = neural / 'onnxruntime-osx-arm64-1.31.0'
        for name in ('LICENSE', 'ThirdPartyNotices.txt'):
            if (runtime / name).exists():
                shutil.copy2(runtime / name, contents / 'Resources' / ('ONNXRuntime-' + name))
    version_header = (root / 'core/src/version.h').read_text()
    version = re.search(r'VERSION_STR "([0-9.]+)', version_header)[1]
    build_number = re.search(r'BEIGE_BUILD_NUMBER (\d+)', version_header)[1]
    plist = {
        'CFBundleName': 'SDR++ Beige', 'CFBundleDisplayName': 'SDR++ Beige',
        'CFBundleIdentifier': 'com.tepesoft.sdrppbeige',
        'CFBundleShortVersionString': version, 'CFBundleVersion': build_number,
        'CFBundlePackageType': 'APPL', 'CFBundleExecutable': 'sdrpp',
        'CFBundleIconFile': 'sdrpp', 'NSHighResolutionCapable': True,
        # Local Homebrew dependencies can require a newer system than the core.
        'LSMinimumSystemVersion': '26.0',
        'NSMicrophoneUsageDescription': 'Microphone input for radio transmission and voice control.',
    }
    with (contents / 'Info.plist').open('wb') as stream:
        plistlib.dump(plist, stream)
    iconset = build / 'beige.iconset'
    iconset.mkdir(exist_ok=True)
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            suffix = '@2x' if scale == 2 else ''
            run('sips', '-z', str(size * scale), str(size * scale),
                str(root / 'root/res/icons/sdrpp.macos.png'), '--out',
                str(iconset / f'icon_{size}x{size}{suffix}.png'))
    run('iconutil', '-c', 'icns', str(iconset), '-o', str(contents / 'Resources/sdrpp.icns'))

    copied = {}
    dependencies = {}

    def copy_binary(source, destination):
        source = source.resolve()
        if destination in copied:
            if copied[destination] != source:
                raise RuntimeError(f'Dependency name collision: {destination.name}')
            return
        copied[destination] = source
        shutil.copy2(source, destination)
        destination.chmod(destination.stat().st_mode | 0o200)
        load_commands = run('otool', '-arch', 'arm64', '-l', str(source))
        paths = re.findall(r'cmd LC_RPATH\n.*?\n\s*path (.*?) \(offset', load_commands)
        deps = []
        for line in run('otool', '-arch', 'arm64', '-L', str(source)).splitlines()[1:]:
            dependency = line.strip().split(' (compatibility')[0]
            if dependency.startswith(('/System/', '/usr/lib/')):
                continue
            if pathlib.Path(dependency).name == source.name:
                continue
            if dependency.startswith('@'):
                candidates = []
                fallback = [str(brown_app / 'Contents/Frameworks')] if brown_app else []
                for path in [str(install / 'lib')] + paths + [str(build / 'core'), '/opt/homebrew/lib'] + fallback:
                    path = path.replace('@loader_path', str(source.parent))
                    path = path.replace('@executable_path', str(install / 'bin'))
                    candidates.append(pathlib.Path(path) / dependency.removeprefix('@rpath/'))
                if dependency.startswith('@loader_path/'):
                    candidates.insert(0, source.parent / dependency.removeprefix('@loader_path/'))
                resolved = next((p for p in candidates if p.is_file()), None)
                if resolved is None:
                    resolved = next(build.rglob(pathlib.Path(dependency).name), None)
                if resolved is None:
                    raise RuntimeError(f'Unresolved {dependency} in {source.name}')
            else:
                resolved = pathlib.Path(dependency)
                if not resolved.exists() and brown_app:
                    resolved = brown_app / 'Contents/Frameworks' / resolved.name
            target = contents / 'Frameworks' / resolved.name
            if target not in copied:
                copy_binary(resolved, target)
            deps.append((dependency, target.name))
        dependencies[destination] = deps
        for dependency, name in deps:
            run('install_name_tool', '-change', dependency, '@rpath/' + name, str(destination))
        for path in paths:
            run('install_name_tool', '-delete_rpath', path, str(destination))
        run('install_name_tool', '-add_rpath', '@loader_path/../Frameworks', str(destination))
        if destination.suffix == '.dylib':
            run('install_name_tool', '-id', '@rpath/' + destination.name, str(destination))

    if usb_prefix:
        # Every plugin must share the patched backend, including Brown carryovers.
        usb_prefix = usb_prefix.resolve()
        copy_binary(usb_prefix / 'lib/libusb-1.0.0.dylib', contents / 'Frameworks/libusb-1.0.0.dylib')
        shutil.copy2(usb_prefix / 'share/licenses/libusb/COPYING', contents / 'Resources/libusb-COPYING')
        shutil.copy2(usb_prefix / 'beige-libusb.json', contents / 'Resources/beige-libusb.json')
        shutil.copy2(root / 'beige/patches/libusb-darwin-disconnect.patch', contents / 'Resources/libusb-darwin-disconnect.patch')
    if rtl_prefix:
        rtl_prefix = rtl_prefix.resolve()
        copy_binary(rtl_prefix / 'lib/librtlsdr.0.dylib', contents / 'Frameworks/librtlsdr.0.dylib')
        shutil.copy2(rtl_prefix / 'share/licenses/rtlsdr/COPYING', contents / 'Resources/librtlsdr-COPYING')
        shutil.copy2(rtl_prefix / 'beige-rtlsdr.json', contents / 'Resources/beige-rtlsdr.json')
        shutil.copy2(root / 'beige/patches/rtlsdr-async-lifetime.patch', contents / 'Resources/rtlsdr-async-lifetime.patch')
        shutil.copy2(root / 'beige/patches/rtlsdr-r82xx-gains.patch', contents / 'Resources/rtlsdr-r82xx-gains.patch')
    copy_binary(install / 'bin/sdrpp', contents / 'MacOS/sdrpp')
    copy_binary(install / 'lib/libsdrpp_core.dylib', contents / 'Frameworks/libsdrpp_core.dylib')
    for plugin in sorted((install / 'lib/sdrpp/plugins').glob('*.dylib')):
        copy_binary(plugin, contents / 'Plugins' / plugin.name)
    if brown_app:
        for plugin in sorted((brown_app / 'Contents/Plugins').glob('*.dylib')):
            target = contents / 'Plugins' / plugin.name
            if not target.exists():
                copy_binary(plugin, target)
    # This is a local development build. Public distribution needs Developer ID/notarization.
    for binary in copied:
        run('codesign', '--force', '--sign', '-', str(binary))
    run('codesign', '--force', '--sign', '-', str(app))
    run('codesign', '--verify', '--deep', '--strict', str(app))
    print(f'{app}\nBundled {len(copied)} Mach-O files; ad-hoc signature verified.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--brown-app', type=pathlib.Path)
    parser.add_argument('--usb-prefix', type=pathlib.Path)
    parser.add_argument('--rtl-prefix', type=pathlib.Path)
    options = parser.parse_args()
    package(options.build, options.brown_app, options.usb_prefix, options.rtl_prefix)
