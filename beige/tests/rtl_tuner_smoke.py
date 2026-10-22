#!/usr/bin/env python3
"""Check optional tuner controls on an isolated RTL-SDR/Radio/NR/Null profile."""
import argparse
import json
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()

    def request(path, body=None):
        data = body.encode() if body is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=10) as response:
            return response.read().decode()

    def state():
        return json.loads(request('/proc/source/RTL-SDR/tuner'))

    def update(value):
        request('/proc/source/RTL-SDR/tuner', json.dumps(value))
        return state()

    def readback():
        request('/proc/source/RTL-SDR/tuner', 'readback')
        return state()['readback']

    def playback(playing):
        request('/sdr/start' if playing else '/sdr/stop')
        for _ in range(300):
            if json.loads(request('/sdr/status'))['playing'] == playing:
                return
            time.sleep(.02)
        raise AssertionError('Playback transition did not finish')

    modules = json.loads(request('/modules'))
    assert sorted(v['module'] for v in modules.values()) == [
        'noise_reduction_logmmse', 'null_audio_sink', 'radio', 'rtl_sdr_source'], 'Use the isolated QA profile'
    request('/proc/null_audio_sink/select', 'Radio')
    original = state()
    assert original['supported'] and not original['bypassed'], original
    try:
        playback(True)
        update({'enabled': False})
        standard = readback()
        update({'enabled': True})
        remembered = [original[k] for k in ('lna', 'mixer', 'vga')] if original['saved'] else standard
        assert readback() == remembered, 'Manual failed to snapshot or restore remembered gains'
        expected = [7, 6, 9]
        changed = update({'lna': 7, 'mixer': 6, 'vga': 9, 'bandwidth': 900000})
        assert changed['enabled'] and changed['error'] == 0 and readback() == expected, changed
        before = state()
        update({'enabled': False, 'lna': 17})
        assert state() == before, 'Invalid request partially changed mode'
        update({'bandwidth': 1 << 50})
        assert state() == before
        playback(False)
        playback(True)
        assert readback() == expected and state()['bandwidth'] == 900000, state()
        update({'enabled': False})
        assert readback() == standard, 'Auto failed to restore standard gain distribution'
        update({'enabled': True})
        assert readback() == expected, 'Manual settings were forgotten'
        request('/proc/source/RTL-SDR/tuner', 'reset_defaults')
        defaults = state()
        assert defaults['enabled'] and defaults['bandwidth'] == 0 and defaults['vga'] == 8, defaults
        assert readback() == [defaults[k] for k in ('lna', 'mixer', 'vga')], defaults
        samples = int(request('/proc/null_audio_sink/samples'))
        time.sleep(.5)
        assert int(request('/proc/null_audio_sink/samples')) > samples
        assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
        print('Gain snapshot, stage changes, bandwidth, reset defaults, atomic validation, restart, Auto restoration and finite RF audio passed.')
    finally:
        update({'enabled': True, **{k: original[k] for k in ('lna', 'mixer', 'vga', 'bandwidth')}})
        update({'enabled': original['enabled']})
        playback(False)


if __name__ == '__main__':
    main()
