#!/usr/bin/env python3
"""Verify neural NR with a custom resource overlay in an isolated receiver."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--template', type=Path, required=True)
    parser.add_argument('--overlay', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--port', type=int, default=18873)
    args = parser.parse_args()
    app = args.app.resolve()
    conf = json.loads((args.template / 'config.json').read_text())
    expected = ['file_source', 'noise_reduction_logmmse', 'null_audio_sink', 'radio']
    assert sorted(item['module'] for item in conf['moduleInstances'].values()) == expected
    assert not (args.overlay / 'models/gtcrn.onnx').exists(), 'Overlay must omit the new model'
    args.work_dir.mkdir(parents=True)
    profile = args.work_dir / 'profile'
    shutil.copytree(args.template, profile)
    conf['resourcesDirectory'] = str(args.overlay.resolve())
    conf['modulesDirectory'] = str(app / 'Contents/Plugins')
    (profile / 'config.json').write_text(json.dumps(conf, indent=2) + '\n')

    def request(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(
                f'http://127.0.0.1:{args.port}' + path, data=data), timeout=10) as response:
            return response.read().decode()

    audio = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/audio/Radio/'
    with (args.work_dir / 'receiver.log').open('w') as log:
        process = subprocess.Popen([str(app / 'Contents/MacOS/sdrpp'), '--root', str(profile.resolve()),
                                    '--http', str(args.port)], cwd=app / 'Contents/MacOS',
                                   stdout=log, stderr=subprocess.STDOUT)
        ready = False
        try:
            for _ in range(100):
                assert process.poll() is None, 'Receiver exited during startup'
                try:
                    ready = json.loads(request('/status'))['ready']
                except (OSError, ValueError):
                    pass
                if ready:
                    break
                time.sleep(.1)
            assert ready, 'Receiver did not start'
            request('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': 'USB'}))
            request('/proc/null_audio_sink/select', 'Radio')
            request(audio + 'neural', 'false')
            request(audio + 'neural', 'true')
            request('/sdr/start')
            for _ in range(100):
                status = request(audio + 'neural_status')
                if status in ('Ready', 'Unavailable; bypassed'):
                    break
                time.sleep(.05)
            assert status == 'Ready', status
            time.sleep(.5)
            samples = int(request('/proc/null_audio_sink/samples'))
            assert samples > 0
            assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
            result = {'status': status, 'samples': samples, 'invalid_samples': 0,
                      'overlay_has_model': False}
            (args.work_dir / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result))
        finally:
            if process.poll() is None:
                if ready:
                    request('/exit')
                else:
                    process.terminate()
            process.wait(timeout=20)


if __name__ == '__main__':
    main()
