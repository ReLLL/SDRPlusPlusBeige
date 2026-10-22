#!/usr/bin/env python3
"""Exercise only a four-module File/Radio/NR/NullAudioSink QA profile."""
import argparse
import json
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--supported-fft-file', help='204800 S/s IQ fixture for a supported 8192-point NR FFT')
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
        apple = cycle % 2 == 0
        request(nr + 'acceleration', 'true' if apple else 'false')
        request(nr + 'baseband_nr', 'true')
        if cycle % 4 == 0:
            request('/proc/display/ham-band', '40m')
        time.sleep(0.35) # Original NR needs its initial noise capture after restart.
        before = samples()
        time.sleep(0.6)
        delta = samples() - before
        backend = request(nr + 'backend')
        assert delta > 0, (cycle, 'audio stopped')
        # This 256 kS/s fixture produces a 10240-point NR FFT, unsupported by
        # Apple's radix-2 API. Auto must therefore use the original FFTW path.
        assert backend == 'FFTW', (cycle, backend)
        assert request(nr + 'baseband_nr') == 'true'
        results.append({'cycle': cycle, 'backend': backend, 'samples': delta,
                        'cpu': int(request(nr + 'cpu_usage'))})
    request(nr + 'acceleration', 'true')
    supported = []
    if args.supported_fft_file:
        playback(False)
        response = json.loads(request('/module/File%20Source/command', json.dumps({
            'cmd': 'set_path', 'args': args.supported_fft_file
        })))
        assert response.get('status') == 'ok', response
        for apple in [True, False, True, False]:
            request(nr + 'acceleration', 'true' if apple else 'false')
            playback(True)
            time.sleep(0.4)
            before = samples()
            time.sleep(0.5)
            backend = request(nr + 'backend')
            delta = samples() - before
            assert backend == ('Apple Accelerate' if apple else 'FFTW'), backend
            assert delta > 0
            supported.append({'backend': backend, 'samples': delta})
            playback(False)
    print(json.dumps({'passed': len(results) + len(supported), 'fallback': results,
                      'supported': supported}, indent=2))


if __name__ == '__main__':
    main()
