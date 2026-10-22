#!/usr/bin/env python3
"""Exercise a connected RTL-SDR using an isolated QA profile with HTTP enabled."""
import argparse
import json
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--interrupt', action='store_true', help='Also cancel the async reader and test automatic UI stop/restart')
    args = parser.parse_args()

    def request(path, body=None):
        data = body.encode() if body is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=10) as response:
            return response.read().decode()

    def device():
        state = json.loads(request('/proc/source/RTL-SDR/device'))
        assert 'running' in state, state
        return state

    if args.interrupt:
        modules = json.loads(request('/modules'))
        expected = ['noise_reduction_logmmse', 'null_audio_sink', 'radio', 'rtl_sdr_source']
        assert sorted(value['module'] for value in modules.values()) == expected, 'Use only the isolated QA profile'
        request('/proc/null_audio_sink/select', 'Radio')
    request('/sdr/stop')
    initial = device()
    assert initial['count'] > 0 and initial['gains'] > 0 and initial['selected'], initial
    for cycle in range(10):
        before = device()
        request('/proc/source/RTL-SDR/device', 'refresh')
        refreshed = device()
        assert refreshed['refreshes'] == before['refreshes'] + 1, refreshed
        assert refreshed['selected'] == initial['selected'] and refreshed['gains'] > 0, refreshed
        request('/sdr/start')
        assert device()['running'], device()
        # A refresh request must not replace a handle owned by the read thread.
        request('/proc/source/RTL-SDR/device', 'refresh')
        active = device()
        assert active['running'] and active['refreshes'] == refreshed['refreshes'], active
        time.sleep(0.15)
        request('/sdr/stop')
        assert not device()['running'], device()
    if args.interrupt:
        for cycle in range(10):
            request('/sdr/start')
            before = int(request('/proc/null_audio_sink/samples'))
            for _ in range(150):
                if int(request('/proc/null_audio_sink/samples')) > before:
                    break
                time.sleep(.02)
            else:
                raise AssertionError('Restart did not deliver audio')
            request('/proc/source/RTL-SDR/device', 'cancel_read')
            for _ in range(100):
                state = device()
                if not state['running'] and not json.loads(request('/sdr/status'))['playing']:
                    break
                time.sleep(.02)
            else:
                raise AssertionError(('Reader ended but UI stayed active', state))
            assert state['worker_ended'] and state['read_result'] == 0, state
        request('/sdr/start')
        assert device()['running'], device()
        request('/sdr/stop')
    print(json.dumps({'passed_cycles': 10, 'interrupted_restarts': 10 if args.interrupt else 0, 'final': device()}))


if __name__ == '__main__':
    main()
