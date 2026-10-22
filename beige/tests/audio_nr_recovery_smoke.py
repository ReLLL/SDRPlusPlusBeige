#!/usr/bin/env python3
"""Verify actual NR worker activity after RAW transitions in an isolated File receiver."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--cycles', type=int, default=1)
    parser.add_argument('--stress', type=int, default=0, help='Concurrent mode/reducer request pairs')
    args = parser.parse_args()
    audio = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/audio/Radio/'

    def request(path, body=None):
        with urllib.request.urlopen(urllib.request.Request(args.url + path,
                data=body.encode() if body is not None else None), timeout=10) as response:
            return response.read().decode()

    def demod(mode):
        result = json.loads(request('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': mode})))
        assert result.get('status') == 'ok', result

    def activity(engine, active):
        for _ in range(60):
            cpu = float(request(audio + engine + '_cpu'))
            if (cpu >= 0) == active:
                return cpu
            time.sleep(.05)
        raise AssertionError(f'{engine} enabled={request(audio + engine)}, expected active={active}, CPU={cpu}')

    modules = json.loads(request('/modules'))
    assert sorted(v['module'] for v in modules.values()) == sorted([
        'file_source', 'radio', 'noise_reduction_logmmse', 'null_audio_sink']), 'Use an isolated File profile'
    request('/module/File%20Source/command', json.dumps({'cmd': 'set_path', 'args': args.fixture}))
    request('/proc/null_audio_sink/select', 'Radio')
    request('/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/baseband_nr', 'false')
    demod('USB')
    request('/sdr/start')
    results = []
    try:
        for engine in ['nr2', 'neural']:
            request(audio + engine, 'true')
            activity(engine, True)
            modes = ['USB', 'LSB', 'AM', 'DSB', '0', 'CW', 'WFM', 'USB'] if engine == 'nr2' else [
                'USB', 'LSB', 'AM', 'DSB', '0', 'USB']
            for mode in modes * args.cycles:
                demod('RAW')
                activity(engine, False)
                assert request(audio + engine) == 'true', 'RAW must preserve the selected reducer'
                demod(mode)
                cpu = activity(engine, True)
                before = int(request('/proc/null_audio_sink/samples'))
                time.sleep(.2)
                assert int(request('/proc/null_audio_sink/samples')) > before
                assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
                results.append({'engine': engine, 'return_mode': mode, 'worker_cpu': cpu})
                print(json.dumps(results[-1]), flush=True)
            if engine == 'nr2':
                request(audio + engine, 'false')
                activity(engine, False)
                request(audio + engine, 'true')
                activity(engine, True)
        # Module commands and procfs writes must both execute on the UI thread.
        # Previously these concurrent requests corrupted the AF stream chain.
        with ThreadPoolExecutor(max_workers=2) as pool:
            for index in range(args.stress):
                mode = ['RAW', 'USB', 'LSB', '0', 'AM', 'WFM'][index % 6]
                engine = ['nr2', 'neural'][index % 2]
                mode_request = pool.submit(demod, mode)
                reducer_request = pool.submit(request, audio + engine, 'true')
                mode_request.result()
                reducer_request.result()
        demod('USB')
        request(audio + 'nr2', 'true')
        activity('nr2', True)
        before = int(request('/proc/null_audio_sink/samples'))
        time.sleep(.3)
        assert int(request('/proc/null_audio_sink/samples')) > before
        assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
        print(json.dumps({'passed_recoveries': len(results), 'concurrent_pairs': args.stress}))
    finally:
        request('/sdr/stop')


if __name__ == '__main__':
    main()
