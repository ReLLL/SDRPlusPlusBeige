#!/usr/bin/env python3
"""Exercise only a four-module File/Radio/NR/NullAudioSink QA profile."""
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
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=8) as response:
            return response.read().decode()

    modules = json.loads(request('/modules'))
    assert sorted(v['module'] for v in modules.values()) == [
        'file_source', 'noise_reduction_logmmse', 'null_audio_sink', 'radio'
    ], 'Use the isolated QA profile, never the normal receiver profile'
    nr = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/'
    def samples():
        return int(request('/proc/null_audio_sink/samples'))

    def playback(playing):
        request('/sdr/start' if playing else '/sdr/stop')
        for _ in range(100):
            if json.loads(request('/sdr/status'))['playing'] == playing:
                return
            time.sleep(0.05)
        raise AssertionError('Receiver did not reach requested playback state')

    request('/proc/null_audio_sink/select', 'Radio')
    playback(True)
    results = []
    for cycle in range(12):
        request(nr + 'baseband_nr', 'false')
        if cycle % 3 == 0:
            playback(False)
            playback(True)
        request(nr + 'reduction_db', str([0, 6, 12, 24][cycle % 4]))
        apple = cycle % 2 == 0
        request(nr + 'acceleration', 'true' if apple else 'false')
        request(nr + 'baseband_nr', 'true')
        if cycle % 4 == 0:
            request('/proc/display/ham-band', '40m')
        time.sleep(0.15)
        before = samples()
        time.sleep(0.6)
        delta = samples() - before
        backend = request(nr + 'backend')
        assert delta > 0, (cycle, 'audio stopped')
        assert backend == ('Apple Accelerate' if apple else 'FFTW'), (cycle, backend)
        assert request(nr + 'baseband_nr') == 'true'
        results.append({'cycle': cycle, 'backend': backend, 'samples': delta,
                        'cpu': int(request(nr + 'cpu_usage'))})
    request(nr + 'reduction_db', '12')
    request(nr + 'acceleration', 'true')
    print(json.dumps({'passed': len(results), 'results': results}, indent=2))


if __name__ == '__main__':
    main()
