#!/usr/bin/env python3
"""Isolated four-module receiver validation for NR2 and neural voice NR."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--fixture')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()

    def request(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=15) as response:
            return response.read().decode()

    modules = json.loads(request('/modules'))
    expected = ['noise_reduction_logmmse', 'null_audio_sink', 'radio', 'file_source' if args.fixture else 'rtl_sdr_source']
    assert sorted(value['module'] for value in modules.values()) == sorted(expected), 'Use only the isolated QA profile'
    nr = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/'
    audio = nr + 'audio/Radio/'

    def sample_count():
        return int(request('/proc/null_audio_sink/samples'))

    def playback(playing):
        request('/sdr/start' if playing else '/sdr/stop')
        for _ in range(100):
            if json.loads(request('/sdr/status'))['playing'] == playing:
                return
            time.sleep(.05)
        raise AssertionError('Playback state failed to change')

    def wait_status(expected):
        for _ in range(100):
            if request(audio + 'neural_status') == expected:
                return
            time.sleep(.05)
        raise AssertionError(request(audio + 'neural_status'))

    def demod(mode):
        result = json.loads(request('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': '0' if mode == 'NFM' else mode})))
        assert result.get('status') == 'ok'

    def cpu_time():
        pieces = subprocess.check_output(['ps', '-p', str(args.pid), '-o', 'time='], text=True).strip().split(':')
        return sum(float(value) * 60**i for i, value in enumerate(reversed(pieces)))

    playback(False)
    request('/proc/source/type', 'File' if args.fixture else 'RTL-SDR')
    if args.fixture:
        request('/module/File%20Source/command', json.dumps({'cmd': 'set_path', 'args': args.fixture}))
    request('/proc/null_audio_sink/select', 'Radio')
    demod('USB' if args.fixture else 'LSB')
    request('/module/Radio/command', json.dumps({'cmd': 'set_bandwidth', 'args': '2800'}))
    request('/vfo/set_offset?name=Radio&offset=17400' if args.fixture else '/vfo/set_offset?name=Radio&offset=-1400')
    request(nr + 'baseband_nr', 'false')
    request(audio + 'nr2', 'false')
    request(audio + 'neural', 'false')
    playback(True)
    results = {'performance': [], 'transitions': []}
    request('/proc/display/spectrum3d', 'true')
    for method in ['off', 'nr2', 'neural', 'off']:
        request(audio + 'nr2', 'false')
        request(audio + 'neural', 'false')
        if method != 'off':
            request(audio + method, 'true')
        assert request(audio + 'nr2') == ('true' if method == 'nr2' else 'false')
        assert request(audio + 'neural') == ('true' if method == 'neural' else 'false')
        time.sleep(7)
        before, start, cpu = sample_count(), time.monotonic(), cpu_time()
        fps, loads, rows = [], [], []
        for _ in range(24):
            stat = json.loads(request('/proc/display/spectrum-stats'))
            fps.append(stat['ui_fps'])
            rows.append(stat['history_rows'])
            if method != 'off':
                loads.append(float(request(audio + ('nr2_cpu' if method == 'nr2' else 'neural_cpu'))))
            time.sleep(.25)
        elapsed = time.monotonic() - start
        assert sample_count() > before
        assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
        if method == 'neural':
            wait_status('Ready')
        if loads:
            assert min(loads) >= 0
        results['performance'].append({'method': method, 'fps_median': statistics.median(fps),
                                       'history_rows_median': statistics.median(rows),
                                       'whole_process_cpu_percent': (cpu_time() - cpu) / elapsed * 100,
                                       'audio_worker_cpu_median': statistics.median(loads) if loads else None,
                                       'scaling': float(request(audio + 'scaling'))})
        print(json.dumps(results['performance'][-1]), flush=True)
    request(audio + 'neural', 'true')
    for repeat in range(2):
        for mode in ['USB', 'LSB', 'AM', 'DSB', 'NFM', 'CW', 'RAW', 'WFM']:
            demod(mode)
            wait_status('Mode bypass' if mode in ['CW', 'RAW', 'WFM'] else 'Ready')
            before = sample_count()
            time.sleep(.6)
            assert sample_count() > before
            assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
            assert request(audio + 'neural') == 'true'
            results['transitions'].append({'repeat': repeat, 'mode': mode})
    demod('LSB')
    request(audio + 'nr2', 'true')
    assert request(audio + 'neural') == 'false'
    request(audio + 'neural', 'true')
    assert request(audio + 'nr2') == 'false'
    playback(False)
    playback(True)
    wait_status('Ready')
    time.sleep(.8)
    assert sample_count() > 0
    assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
    args.output.write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
