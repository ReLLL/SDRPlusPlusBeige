#!/usr/bin/env python3
"""Offline CW, generic BPSK and AM guardrails for the NR research variants."""
import argparse
import json
from pathlib import Path

import numpy as np
from scipy import signal

from nr_evaluate import metrics, resample, run, speech


def power(data):
    return np.mean(np.abs(data)**2)


def equivalent_snr(clean, output):
    n = min(len(clean), len(output))
    a, b = clean[5000:n], output[5000:n]
    projection = np.vdot(a, b) / np.vdot(a, a)
    error = power(b - projection * a)
    return {'equivalent_snr_db': float(10 * np.log10(max(abs(projection)**2 * power(a), 1e-30) / max(error, 1e-30))),
            'wanted_level_db': float(20 * np.log10(max(abs(projection), 1e-15)))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--rate', type=int, default=204800)
    parser.add_argument('--modes', nargs='+', default=['reference', 'fast', 'bootstrap', 'tracker', 'blend95'])
    args = parser.parse_args()
    root, runner, rate = args.data.resolve(), args.runner.resolve(), args.rate
    rows = []
    rng = np.random.default_rng(65537)
    seconds = 8
    t = np.arange(seconds * rate) / rate
    carrier = np.exp(2j * np.pi * 16000 * t)
    for kind in ['cw', 'bpsk100', 'am']:
        white = rng.normal(size=len(t)) + 1j * rng.normal(size=len(t))
        if kind == 'cw':
            keys = np.array([1, 0, 1, 1, 1, 0, 1, 0, 0, 0] * 20)
            envelope = keys[(t / 0.06).astype(int) % len(keys)].astype(float)
            envelope = signal.lfilter(signal.firwin(2049, 150, fs=rate), [1], envelope)
            wanted = carrier * np.exp(2j * np.pi * 700 * t) * envelope * 0.05
            mixed = wanted + white * 0.2
        elif kind == 'bpsk100':
            samples_per_symbol = rate // 100
            assert samples_per_symbol * 100 == rate
            bits = rng.integers(0, 2, seconds * 100)
            wanted = np.repeat(2 * bits - 1, samples_per_symbol) * carrier * 0.02
            # Rectangular BPSK, Eb/N0=2 dB before any NR. Matched rectangular
            # integration is optimal here; lower BER is not expected from NR.
            variance = 0.02**2 * samples_per_symbol / 10**(2 / 10)
            mixed = wanted + white * np.sqrt(variance / 2)
        else:
            voice = speech(root / 'OSR_us_000_0010_8k.wav', seconds)
            voice = resample(voice, 8000, rate)
            wanted = (0.2 + voice) * carrier
            mixed = wanted + white * 0.15
        scale = min(1, 0.85 / np.max(np.abs(mixed)))
        mixed, wanted = (mixed * scale).astype('<c8'), (wanted * scale).astype('<c8')
        outputs = [('off', mixed, {})]
        for mode in args.modes:
            output, meta = run(runner, mixed, rate, mode, root)
            outputs.append((mode, output, meta))
        common_length = min(len(value) for _, value, _ in outputs)
        for mode, output, meta in outputs:
            output = output[:common_length]
            record = {'signal': kind, 'mode': mode, **meta}
            if kind == 'cw':
                # Compare the received CW channel, not the rejected wideband.
                def cw_channel(iq):
                    down = resample(iq * np.exp(-2j * np.pi * 16700 * t[:len(iq)]), rate, 8000)
                    return signal.sosfiltfilt(signal.butter(4, 100, fs=8000, output='sos'), down)
                record.update(equivalent_snr(cw_channel(wanted), cw_channel(output)))
            elif kind == 'bpsk100':
                n = len(output) // samples_per_symbol
                baseband = output[:n * samples_per_symbol] * np.conj(carrier[:n * samples_per_symbol])
                soft = baseband.reshape(n, samples_per_symbol).mean(axis=1).real
                decoded = soft > 0
                # Skip startup; do not optimize decision threshold per mode.
                record.update({'bits': n - 100, 'errors': int(np.count_nonzero(decoded[100:] != bits[100:n])),
                               'ber': float(np.mean(decoded[100:] != bits[100:n]))})
            else:
                def am_audio(iq):
                    down = resample(iq * np.exp(-2j * np.pi * 16000 * t[:len(iq)]), rate, 8000)
                    envelope = np.abs(down)
                    return signal.sosfiltfilt(signal.butter(6, [300, 2700], btype='bandpass', fs=8000, output='sos'), envelope)
                record.update(metrics(am_audio(wanted), am_audio(output)))
            rows.append(record)
            print(json.dumps(record), flush=True)
    (root / 'guardrail-results.json').write_text(json.dumps(rows, indent=2) + '\n')


if __name__ == '__main__':
    main()
