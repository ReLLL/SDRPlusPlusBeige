#!/usr/bin/env python3
"""Exercise desktop rewind and real stereo WAV exports in an isolated File receiver."""
import argparse
import array
import json
import math
from pathlib import Path
import struct
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--fixture', required=True)
    args = parser.parse_args()

    def req(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=15) as response:
            return response.read().decode()

    def status():
        return json.loads(req('/proc/audio-rewind/status'))

    def wait(predicate):
        for _ in range(200):
            value = status()
            if predicate(value):
                return value
            time.sleep(.05)
        raise AssertionError(value)

    def export(seconds):
        req('/proc/audio-rewind/save', str(seconds))
        for _ in range(200):
            result = json.loads(req('/proc/audio-rewind/export'))
            if not result['busy']:
                assert not result['error'], result
                break
            time.sleep(.05)
        else:
            raise AssertionError('Export did not finish')
        data = Path(result['path']).read_bytes()
        assert data[:4] == b'RIFF' and data[8:16] == b'WAVEfmt '
        assert struct.unpack_from('<I', data, 4)[0] == len(data) - 8
        assert struct.unpack_from('<HHI', data, 20) == (3, 2, 48000)
        assert data[36:40] == b'fact' and data[48:52] == b'data'
        assert struct.unpack_from('<I', data, 52)[0] == seconds * 48000 * 8
        samples = array.array('f')
        samples.frombytes(data[56:])
        assert all(math.isfinite(x) for x in samples)
        assert sum(x*x for x in samples) > 0
        return result, data[56:]

    modules = json.loads(req('/modules'))
    assert sorted(v['module'] for v in modules.values()) == sorted([
        'file_source', 'radio', 'noise_reduction_logmmse', 'null_audio_sink'])
    req('/module/File%20Source/command', json.dumps({'cmd': 'set_path', 'args': args.fixture}))
    req('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': 'USB'}))
    req('/proc/null_audio_sink/select', 'Radio')
    req('/proc/audio-rewind/enabled', 'true')
    req('/sdr/start')
    try:
        initial = wait(lambda s: s['available_seconds'] == 60)
        assert initial['buffer_bytes'] == 48000 * 65 * 8
        req('/proc/audio-rewind/seek', '5')
        replay = wait(lambda s: not s['live'] and abs(s['delay_seconds'] - 5) < .05)
        req('/proc/audio-rewind/paused', 'true')
        paused = status()
        time.sleep(.3)
        assert status()['captured_frames'] > paused['captured_frames']
        assert status()['paused']
        req('/sdr/stop')
        wait(lambda s: not s['receiving'])
        time.sleep(.3)
        head = status()['captured_frames']
        time.sleep(.2)
        assert status()['captured_frames'] == head
        clip30, audio30 = export(30)
        clip60, audio60 = export(60)
        assert audio60[-len(audio30):] == audio30, 'Exports must contain exact retained audio'
        directory = Path(clip60['path']).parent
        if directory.is_relative_to('/tmp'):
            backup = directory.with_name('recordings-smoke-backup')
            assert not backup.exists()
            directory.rename(backup)
            try:
                directory.write_text('Intentional export error in isolated QA profile')
                req('/proc/audio-rewind/save', '30')
                for _ in range(100):
                    failed = json.loads(req('/proc/audio-rewind/export'))
                    if not failed['busy']:
                        assert failed['error'], failed
                        break
                    time.sleep(.05)
                else:
                    raise AssertionError('Failed export did not finish')
                assert status()['captured_frames'] == head
            finally:
                directory.unlink()
                backup.rename(directory)
            export(30)
        req('/proc/audio-rewind/seek', '3')
        req('/proc/audio-rewind/paused', 'false')
        before = status()['delay_seconds']
        time.sleep(.5)
        after = status()['delay_seconds']
        assert .2 < before - after < .9, (before, after)
        assert status()['captured_frames'] == head, 'Stopped replay must not record itself'
        req('/proc/audio-rewind/live', 'true')
        assert status()['live'] and not status()['paused']
        req('/proc/audio-rewind/enabled', 'false')
        assert status()['buffer_bytes'] == 0
        req('/sdr/start')
        time.sleep(.3)
        assert status()['captured_frames'] == 0
        req('/proc/audio-rewind/enabled', 'true')
        wait(lambda s: s['captured_frames'] > 0)
        req('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': 'RAW'}))
        wait(lambda s: s['rate'] == 0 and s['buffer_bytes'] == 0)
        req('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': 'USB'}))
        wait(lambda s: s['rate'] == 48000 and s['captured_frames'] > 0)
        assert int(req('/proc/null_audio_sink/invalid_samples')) == 0
        print(json.dumps({'initial': initial, 'replay': replay, 'exports': [clip30, clip60],
            'stopped_replay_advance': before - after, 'finite_audio': True}), flush=True)
    finally:
        req('/proc/audio-rewind/live', 'true')
        req('/sdr/stop')


if __name__ == '__main__':
    main()
