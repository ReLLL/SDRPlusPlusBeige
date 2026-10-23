#!/usr/bin/env python3
"""Check real IQ demodulation and live Recorder isolation in a disposable profile."""
import argparse
import json
from pathlib import Path
import statistics
import struct
import subprocess
import time
import urllib.request
import wave

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--create-fixture', action='store_true', help='Create the known-tone fixture at a new disposable path')
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert str(args.profile.resolve()).startswith('/private/tmp/') or str(args.profile).startswith('/tmp/')
    if args.create_fixture:
        path = Path(args.fixture).resolve()
        assert path.is_relative_to(args.profile.resolve().parent) and not path.exists()
        with wave.open(str(path), 'wb') as fixture:
            fixture.setparams((2, 2, 800000, 0, 'NONE', 'not compressed'))
            for sec in range(24):
                t = (np.arange(800000) + sec * 800000) / 800000
                first = .2 * (1 + .5 * np.cos(2 * np.pi * (400 + 100 * (sec // 3)) * t)) * np.exp(2j * np.pi * 20000 * t)
                second = .16 * (1 + .5 * np.cos(2 * np.pi * 1800 * t)) * np.exp(-2j * np.pi * 20000 * t)
                z = first + second
                fixture.writeframes((np.column_stack((z.real, z.imag)) * 32767).astype('<i2').tobytes())

    def req(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=20) as response:
            return response.read().decode()

    def status():
        return json.loads(req('/proc/iq-replay/status'))

    def command(module, cmd, value=''):
        return json.loads(req('/module/' + module + '/command', json.dumps({'cmd': cmd, 'args': value})))

    def wait(predicate):
        for _ in range(400):
            s = status()
            if predicate(s):
                return s
            time.sleep(.05)
        raise AssertionError(s)

    def seek(sample, frequency, epoch=None):
        req('/proc/iq-replay/seek', json.dumps({'receiver': 'Radio',
            'epoch': status()['epoch'] if epoch is None else epoch, 'sample': int(sample), 'frequency': frequency}))

    def spectrum(samples):
        a = np.asarray(samples, dtype=float)
        assert len(a) >= 2048 and np.isfinite(a).all()
        sp = np.abs(np.fft.rfft((a - a.mean()) * np.hanning(len(a))))
        sp[:5] = 0
        return float(np.argmax(sp) * 48000 / len(a)), float(np.sqrt(np.mean(a * a))), sp

    def tone(expected):
        time.sleep(.65)
        hz, rms, _ = spectrum(json.loads(req('/proc/null_audio_sink/audio')))
        assert abs(hz - expected) < 20 and rms > .0001, (hz, expected, rms, status())
        s = status()
        assert s['audio_rendered'] > 0 and s['audio_underruns'] / s['audio_rendered'] < .005, s
        return {'expected_hz': expected, 'measured_hz': hz, 'rms': rms,
                'audio_frames': s['audio_rendered'], 'underruns': s['audio_underruns']}

    def indicator(expected):
        time.sleep(.05)
        m = json.loads(req('/proc/iq-replay/map'))
        x = m['min'][0] + (expected - m['low']) / (m['high'] - m['low']) * (m['max'][0] - m['min'][0])
        assert abs(m['indicator_x'] - x) <= 2, (expected, x, m['indicator_x'])
        return {'frequency': expected, 'pixel_error': abs(m['indicator_x'] - x), 'live_frequency': m['live_vfo_frequency']}

    def cpu():
        pieces = subprocess.check_output(['ps', '-p', str(args.pid), '-o', 'time='], text=True).strip().split(':')
        return sum(float(v) * 60**i for i, v in enumerate(reversed(pieces)))

    def performance(seconds):
        start, before = time.monotonic(), cpu()
        frames = []
        while time.monotonic() - start < seconds:
            frames.append(json.loads(req('/proc/display/spectrum-stats'))['ui_fps'])
            time.sleep(.2)
        return {'fps_median': statistics.median(frames), 'cpu_percent': (cpu() - before) * 100 / (time.monotonic() - start)}

    modules = json.loads(req('/modules'))
    assert sorted(v['module'] for v in modules.values()) == sorted([
        'file_source', 'radio', 'noise_reduction_logmmse', 'null_audio_sink', 'recorder'])
    req('/sdr/stop')
    req('/proc/iq-replay/enabled', 'false')
    req('/proc/iq-replay/time-step', '-1')
    assert not status()['enabled'] and not status()['replaying']
    command('File%20Source', 'set_path', args.fixture)
    command('Radio', 'set_demod', 'AM')
    command('Radio', 'set_bandwidth', '8000')
    req('/vfo/set_offset?name=Radio&offset=20000')
    req('/proc/null_audio_sink/select', 'Radio')
    nr = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/'
    for path in ['baseband_nr', 'audio/Radio/nr2', 'audio/Radio/neural']:
        req(nr + path, 'false')
    req('/proc/null_audio_sink/realtime', 'true')
    req('/sdr/start')
    results = {'off': performance(3)}
    req('/sdr/stop')
    req('/proc/iq-replay/enabled', 'true')
    wait(lambda s: s['ready'])
    req('/proc/iq-replay/time-step', '-1')
    assert status()['enabled'] and not status()['replaying'], 'Empty history must stay live'
    req('/sdr/start')
    wait(lambda s: s['head'] >= 800000 * 8)
    results['capture'] = performance(3)
    req('/sdr/stop')
    time.sleep(.2)
    head = status()['head']
    assert head < 800000 * 24, status()

    # Down starts from the live capture head at the selected live VFO, while
    # Up crossing the newest retained point must return fully to Live.
    req('/vfo/set_offset?name=Radio&offset=-20000')
    req('/proc/iq-replay/time-step', '1')
    assert not status()['replaying'], 'Up while live should stay live'
    before = status()
    req('/proc/iq-replay/time-step', '-1')
    after = status()
    assert after['replaying'] and after['receiver'] == 'Radio' and after['frequency'] == 7080000
    assert after['selected_sample'] == before['head'] - before['rate'], (before, after)
    results['down_from_live'] = tone(1800)
    seek(head - 40000, 7080000)
    req('/proc/iq-replay/time-step', '.1')
    assert not status()['replaying'], 'Up must cross the fractional tail into Live'
    assert json.loads(req('/proc/audio-rewind/status'))['live']
    results['up_fractional_tail_returns_live'] = True
    req('/vfo/set_offset?name=Radio&offset=20000')
    results['tones'] = []
    for sample, frequency, expected in [(800000, 7120000, 400), (800000 * 7, 7120000, 600), (800000, 7080000, 1800)]:
        seek(sample, frequency)
        assert status()['selected_sample'] == sample
        results['tones'].append(tone(expected))
    assert status()['head'] == head
    results['replay'] = performance(2)

    # Frequency-only tuning must preserve the timeline and live VFO while the
    # listening decoder changes carriers. No repeated decoder construction.
    seek(2560000, 7120000)
    results['retuning'] = [tone(500)]
    results['indicators'] = [indicator(7120000)]
    before = status()
    live_before = json.loads(req('/proc/display/ham-band-state'))
    for frequency, expected in [(7080000, 1800), (7120000, 500)]:
        req('/proc/iq-replay/frequency', str(frequency))
        after = status()
        assert after['frequency'] == frequency and after['revision'] == before['revision']
        assert after['selected_sample'] == before['selected_sample'] and after['epoch'] == before['epoch']
        assert before['cursor'] <= after['cursor'] < before['cursor'] + 800000 * 2
        results['retuning'].append(tone(expected))
        results['indicators'].append(indicator(frequency))
        before = status()
    for frequency in ['nan', 'inf', '7500000', '6700000', '7499999']:
        req('/proc/iq-replay/frequency', frequency)
        assert status()['frequency'] == 7120000 and status()['replaying']
    for i in range(32):
        req('/proc/iq-replay/frequency', str(7080000 if i % 2 == 0 else 7120000))
        assert status()['revision'] == before['revision']
    live_after = json.loads(req('/proc/display/ham-band-state'))
    for key in ['spectrum_center', 'vfo_offset', 'center_tuning', 'single_click_vfo']:
        assert live_before[key] == live_after[key], (key, live_before, live_after)
    assert live_after['frequency'] == 7120000, live_after

    # Relative time navigation keeps frequency/live RF and rejects unavailable
    # history without ending playback or overflowing its sample coordinate.
    seek(2560000, 7120000)
    tone(500)
    results['time_steps'] = []
    for seconds, expected, frequency in [(3, 600, 7120000), (-3, 500, 7120000),
                                          (.1, 1800, 7080000), (-.1, 1800, 7080000)]:
        req('/proc/iq-replay/frequency', str(frequency))
        before = status()
        req('/proc/iq-replay/time-step', str(seconds))
        after = status()
        target = before['cursor'] + round(seconds * before['rate'])
        assert target <= after['selected_sample'] < target + before['rate'] * .2, (before, after)
        assert after['frequency'] == frequency and after['epoch'] == before['epoch']
        assert after['revision'] > before['revision'] and after['replaying']
        results['time_steps'].append(tone(expected))
    before = status()
    for seconds in ['-1e30', '-1e308', 'nan', 'inf', '-inf', '0']:
        req('/proc/iq-replay/time-step', seconds)
        after = status()
        assert after['replaying'] and after['revision'] == before['revision']
        assert after['frequency'] == 7080000
    live_after = json.loads(req('/proc/display/ham-band-state'))
    for key in ['spectrum_center', 'vfo_offset', 'center_tuning', 'single_click_vfo']:
        assert live_before[key] == live_after[key], (key, live_before, live_after)
    req('/proc/audio-rewind/live', 'true')
    time.sleep(.1)
    restored = json.loads(req('/proc/display/ham-band-state'))
    assert restored['frequency'] == restored['spectrum_center'] + restored['vfo_offset'], restored
    results['indicators'].append(indicator(restored['frequency']))
    seek(800000, 7080000)

    # Rewind controls own their cursor; only the shared Live action exits IQ.
    revision = status()['revision']
    req('/proc/audio-rewind/seek', '3')
    req('/proc/audio-rewind/paused', 'true')
    assert status()['replaying'] and status()['revision'] == revision
    req('/proc/audio-rewind/live', 'true')
    assert not status()['replaying']

    # Live AM is at +20 kHz, replay at -20 kHz. Recorder must hear live AM.
    req('/sdr/start')
    wait(lambda s: s['head'] > 800000 * 3)
    seek(800000, 7080000)
    assert command('Recorder', 'start')['status'] == 'recording'
    results['live_and_replay'] = tone(1800)
    time.sleep(1)
    command('Recorder', 'stop')
    recording = max((args.profile / 'recordings').glob('*.wav'), key=lambda p: p.stat().st_mtime_ns)
    data = recording.read_bytes()
    offset = 12
    samples = None
    while offset + 8 <= len(data):
        chunk, size = struct.unpack_from('<4sI', data, offset)
        if chunk == b'data':
            samples = np.frombuffer(data, dtype='<f4', count=size // 4, offset=offset + 8)[::2]
            break
        offset += 8 + size + size % 2
    hz, rms, sp = spectrum(samples)
    replay_bin = round(1800 * len(samples) / 48000)
    assert 350 < hz < 800 and sp[max(0, replay_bin - 2):replay_bin + 3].max() < sp.max() * .01, (hz, rms)
    results['recorder_live'] = {'peak_hz': hz, 'rms': rms, 'frames': len(samples)}
    req('/proc/audio-rewind/live', 'true')
    req('/sdr/stop')
    time.sleep(.2)
    s = status()
    for sample, frequency, epoch in [(s['head'], 7100000, s['epoch']), (0, 7100000, s['epoch'] + 1),
                                     (0, 7500000.001, s['epoch']), (0, 6699999.999, s['epoch'])]:
        seek(sample, frequency, epoch)
        assert not status()['replaying']
    for repeat in range(2):
        for mode in ['AM', 'USB', 'LSB', 'NFM', 'DSB', 'CW', 'RAW', 'WFM']:
            command('Radio', 'set_demod', '0' if mode == 'NFM' else mode)
            seek(800000, 7100000)
            time.sleep(.18)
            assert status()['replaying'], (mode, status())
            assert int(req('/proc/null_audio_sink/invalid_samples')) == 0
    command('Radio', 'set_demod', 'AM')
    req('/proc/audio-rewind/live', 'true')
    seek(status()['head'] - 1, 7100000)
    wait(lambda s: not s['replaying'])
    req('/sdr/start')
    wait(lambda s: s['head'] > 800000)
    seek(0, 7100000, s['epoch'])
    assert not status()['replaying'], 'Restart must reject old generations'

    # A real filesystem error exercises controller auto-off and live recovery.
    req('/proc/iq-replay/enabled', 'false')
    wait(lambda s: not s['ready'])
    cache = args.profile / 'cache'
    backup = args.profile / 'cache-iq-smoke-backup'
    assert cache.is_dir() and not backup.exists()
    cache.rename(backup)
    try:
        cache.write_text('Intentional storage fault in disposable QA profile')
        req('/proc/iq-replay/enabled', 'true')
        failed = wait(lambda s: not s['enabled'] and bool(s['notice']))
        assert json.loads(req('/sdr/status'))['playing']
        results['storage_failure'] = failed['notice']
    finally:
        cache.unlink()
        backup.rename(cache)
    req('/proc/iq-replay/enabled', 'true')
    wait(lambda s: s['ready'] and s['head'] > 0)
    req('/proc/iq-replay/enabled', 'false')
    assert status()['bytes'] == 0 and not list(cache.glob('iq-replay-*'))
    req('/sdr/stop')
    results['boundary_modes_isolation_recovery_pass'] = True
    args.output.write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results), flush=True)


if __name__ == '__main__':
    main()
