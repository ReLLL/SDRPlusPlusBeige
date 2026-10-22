#!/usr/bin/env python3
"""Check retained playback through a real Audio sink without editing receiver settings."""
import argparse
import json
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--sink', default='Audio')
    args = parser.parse_args()

    def req(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=20) as response:
            return response.read().decode()

    def status():
        return json.loads(req('/proc/audio-rewind/status'))

    def wait(predicate):
        for _ in range(150):
            s = status()
            if predicate(s):
                return s
            time.sleep(.1)
        raise AssertionError(s)

    initial = status()
    assert initial['enabled'] and initial['rate'] and initial['stream'], initial
    streams = json.loads(req('/streams'))['streams']
    assert any(s['name'] == initial['stream'] and s['sink'] == args.sink for s in streams), streams
    req('/sdr/start')
    try:
        live = wait(lambda s: s['available_seconds'] >= 3)
        req('/proc/audio-rewind/seek', '1')
        replay = status()
        assert not replay['live'] and abs(replay['delay_seconds'] - 1) < .1, replay
        req('/proc/audio-rewind/paused', 'true')
        a = status()
        time.sleep(.4)
        b = status()
        assert b['paused'] and b['captured_frames'] > a['captured_frames'], (a, b)
        req('/proc/audio-rewind/live', 'true')
        req('/sdr/stop')
        time.sleep(.6)
        head = status()['captured_frames']
        time.sleep(.3)
        assert status()['captured_frames'] == head
        req('/proc/audio-rewind/seek', '2')
        a = status()
        b = wait(lambda s: s['delay_seconds'] < a['delay_seconds'] - .3)
        assert not b['receiving'] and b['captured_frames'] == head, b
        req('/proc/audio-rewind/paused', 'true')
        time.sleep(.2)
        a = status()
        time.sleep(.3)
        b = status()
        assert b['paused'] and b['delay_seconds'] == a['delay_seconds'], (a, b)
        req('/proc/audio-rewind/paused', 'false')
        end = wait(lambda s: s['paused'] and s['delay_seconds'] == 0)
        assert end['captured_frames'] == head
        req('/proc/audio-rewind/live', 'true')
        assert status()['live'] and not status()['paused']
        print(json.dumps({'live': live, 'replay': replay, 'stopped_end': end,
            'stopped_capture_unchanged': True, 'output_transport_pass': True}), flush=True)
    finally:
        req('/proc/audio-rewind/live', 'true')
        req('/sdr/stop')


if __name__ == '__main__':
    main()
