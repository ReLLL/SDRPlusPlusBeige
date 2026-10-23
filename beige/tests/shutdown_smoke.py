#!/usr/bin/env python3
"""Exercise packaged startup failures and enabled/disabled TETRA destruction."""

import argparse
import json
from pathlib import Path
import socket
import subprocess
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path,
                        help='New empty QA directory; never an existing receiver profile')
    parser.add_argument('--port', type=int, default=18878)
    parser.add_argument('--cycles', type=int, default=10)
    args = parser.parse_args()
    app = args.app.resolve()
    executable = app / 'Contents/MacOS/sdrpp'
    assert executable.is_file(), executable
    assert args.cycles > 0
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', args.port))
    # A new directory prevents accidental edits to the normal receiver profile.
    args.work.mkdir(parents=False, exist_ok=False)
    work = args.work.resolve()
    results = []
    for label, options, expected in [
        ('help', ['--help'], 0),
        ('missing-resources', ['-r', str(work / 'early-exit')], 1),
    ]:
        if label == 'missing-resources':
            root = work / 'early-exit'
            root.mkdir()
            (root / 'config.json').write_text(json.dumps({
                'resourcesDirectory': str(root / 'absent-resources'),
            }))
        with (work / f'{label}.log').open('w') as log:
            completed = subprocess.run([str(executable), *options],
                                       cwd=executable.parent, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=30)
        assert completed.returncode == expected, (label, completed.returncode)
        results.append({'case': label, 'exit': completed.returncode})

    root = work / 'tetra'
    root.mkdir()
    for cycle in range(args.cycles):
        enabled = bool(cycle % 2)
        (root / 'config.json').write_text(json.dumps({
            'modulesDirectory': str(app / 'Contents/Plugins'),
            'resourcesDirectory': str(app / 'Contents/Resources'),
            'moduleInstances': {
                'TETRA Demodulator': {
                    'module': 'ch_tetra_demodulator', 'enabled': enabled,
                },
            },
        }))
        with (work / f'tetra-{cycle}.log').open('w') as log:
            process = subprocess.Popen(
                [str(executable), '-r', str(root), '--http', str(args.port)],
                cwd=executable.parent, stdout=log, stderr=subprocess.STDOUT)
            url = f'http://127.0.0.1:{args.port}'
            try:
                for _ in range(200):
                    assert process.poll() is None, process.returncode
                    try:
                        with urllib.request.urlopen(url + '/status', timeout=1) as response:
                            ready = json.load(response)['ready']
                        if ready:
                            break
                    except OSError:
                        pass
                    time.sleep(.1)
                else:
                    raise AssertionError('Receiver startup timed out')
                with urllib.request.urlopen(url + '/modules', timeout=5) as response:
                    modules = json.load(response)
                assert set(modules) == {'TETRA Demodulator'}, modules
                assert modules['TETRA Demodulator']['enabled'] == enabled, modules
                with urllib.request.urlopen(url + '/exit', timeout=5) as response:
                    response.read()
                code = process.wait(timeout=30)
                assert code == 0, (cycle, enabled, code)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=15)
        result = {'case': 'tetra', 'enabled': enabled, 'exit': code}
        results.append(result)
        print(json.dumps(result), flush=True)
    (work / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
