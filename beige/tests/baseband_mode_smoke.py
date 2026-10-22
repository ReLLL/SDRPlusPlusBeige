#!/usr/bin/env python3
"""Mode transitions and full-receiver timing in a four-module QA profile."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import time
import urllib.request
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--fixture')
    parser.add_argument('--source', choices=['File', 'RTL-SDR'], default='File')
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--measure-only', action='store_true')
    parser.add_argument('--transitions-only', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.source == 'File':
        assert args.fixture
        with wave.open(args.fixture, 'rb') as fixture:
            assert fixture.getnchannels() == 2 and fixture.getsampwidth() == 2, 'Use PCM16 IQ with File Source Float32 Mode off'
            rate = fixture.getframerate()

    def request(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=12) as response:
            return response.read().decode()

    modules = json.loads(request('/modules'))
    expected_modules = ['noise_reduction_logmmse', 'null_audio_sink', 'radio',
                        'file_source' if args.source == 'File' else 'rtl_sdr_source']
    assert sorted(v['module'] for v in modules.values()) == sorted(expected_modules), 'Use only the isolated QA profile'
    nr = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/'

    def playback(playing):
        request('/sdr/start' if playing else '/sdr/stop')
        for _ in range(100):
            if json.loads(request('/sdr/status'))['playing'] == playing:
                return
            time.sleep(0.05)
        raise AssertionError('Playback state failed to change')

    def demod(mode):
        result = json.loads(request('/module/Radio/command', json.dumps({'cmd': 'set_demod', 'args': '0' if mode == 'NFM' else mode})))
        assert result.get('status') == 'ok', result

    def wait_effective(expected):
        for _ in range(40):
            if request(nr + 'effective') == expected:
                return
            time.sleep(0.05)
        raise AssertionError((expected, request(nr + 'effective')))

    def samples():
        return int(request('/proc/null_audio_sink/samples'))

    def process_cpu():
        stamp = subprocess.check_output(['ps', '-p', str(args.pid), '-o', 'time='], text=True).strip()
        pieces = stamp.split(':')
        return sum(float(v) * 60**i for i, v in enumerate(reversed(pieces)))

    def performance(profile, view):
        if not args.baseline:
            request(nr + 'profile', profile)
        request('/proc/display/spectrum3d', view)
        # Warm past the 6.3-second history horizon. RF arrival cadence can
        # retain fewer than 64 slices even with the complete time span filled.
        time.sleep(7)
        before, cpu, start = samples(), process_cpu(), time.monotonic()
        stats, nr_cpu = [], []
        for _ in range(24):
            stats.append(json.loads(request('/proc/display/spectrum-stats')))
            nr_cpu.append(int(request(nr + 'cpu_usage')))
            time.sleep(0.25)
        elapsed = time.monotonic() - start
        delta = samples() - before
        assert delta > 0
        assert request(nr + 'baseband_nr') == 'true'
        assert max(v['history_rows'] for v in stats) >= 32
        return {'profile': profile, 'view3d': view, 'samples': delta,
                'fps_median': statistics.median(v['ui_fps'] for v in stats),
                'history_rows_median': statistics.median(v['history_rows'] for v in stats),
                'draw_us_median': statistics.median(v['frame_us'] for v in stats),
                'nr_cpu_median': statistics.median(nr_cpu),
                'process_cpu_percent': (process_cpu() - cpu) / elapsed * 100}

    playback(False)
    request('/proc/source/type', args.source)
    if args.source == 'File':
        assert args.fixture
        request('/module/File%20Source/command', json.dumps({'cmd': 'set_path', 'args': args.fixture}))
    request('/proc/null_audio_sink/select', 'Radio')
    demod('USB' if args.source == 'File' else 'LSB')
    request('/module/Radio/command', json.dumps({'cmd': 'set_bandwidth', 'args': '2800'}))
    # Endpoint takes channel CENTER, whereas the displayed SSB frequency is
    # its lower (USB) or upper (LSB) reference edge.
    request('/vfo/set_offset?name=Radio&offset=17400' if args.source == 'File' else '/vfo/set_offset?name=Radio&offset=-1400')
    request(nr + 'acceleration', 'false')
    request(nr + 'baseband_nr', 'true')
    playback(True)
    results = {'performance': [], 'transitions': []}
    if args.baseline:
        results['performance'].append(performance('original', 'true'))
    elif not args.transitions_only:
        for profile, view in [('original', 'true'), ('auto', 'true'), ('auto', 'false'), ('original', 'true')]:
            results['performance'].append(performance(profile, view))
            print(json.dumps(results['performance'][-1]), flush=True)
    if not args.baseline and not args.measure_only:
        request(nr + 'profile', 'auto')
        for repeat in range(3):
            for mode, expected in [('LSB', 'Wiener voice'), ('AM', 'Original LogMMSE'),
                                   ('CW', 'Original LogMMSE'), ('DSB', 'Original LogMMSE'),
                                   ('NFM', 'Bypassed'), ('WFM', 'Bypassed'),
                                   ('RAW', 'Bypassed'), ('USB', 'Wiener voice')]:
                demod(mode)
                wait_effective(expected)
                assert request(nr + 'selected_mode') == mode
                before = samples()
                time.sleep(0.5)
                delta = samples() - before
                assert delta > 0, (repeat, mode)
                assert request(nr + 'baseband_nr') == 'true'
                if expected == 'Bypassed':
                    assert int(request(nr + 'cpu_usage')) == -1
                assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
                results['transitions'].append({'repeat': repeat, 'mode': mode, 'effective': expected, 'samples': delta})
        for profile, expected in [('digital', 'Bypassed'), ('original', 'Original LogMMSE'), ('auto', 'Wiener voice')]:
            request(nr + 'profile', profile)
            wait_effective(expected)
            assert request(nr + 'profile') == profile
        request(nr + 'profile', 'invalid')
        assert request(nr + 'profile') == 'auto'
        # Exercise actual Apple/FFTW changes during voice processing.
        for apple in [True, False, True, False]:
            request(nr + 'acceleration', 'true' if apple else 'false')
            before = samples()
            time.sleep(0.5)
            assert samples() > before
            assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
            if args.source == 'File':
                length = int(0.02 * rate)
                length += length % 2
                fft = length * 2
                expected = 'Apple Accelerate' if apple and fft > 1 and (fft & (fft - 1)) == 0 else 'FFTW'
                assert request(nr + 'backend') == expected
        playback(False)
        playback(True)
        wait_effective('Wiener voice')
        time.sleep(0.6)
        assert samples() > 0
        assert int(request('/proc/null_audio_sink/invalid_samples')) == 0
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps({'performance_intervals': len(results['performance']), 'mode_transitions': len(results['transitions'])}))


if __name__ == '__main__':
    main()
