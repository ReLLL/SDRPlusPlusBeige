#!/usr/bin/env python3
"""Offline audio NR comparison on controlled radio-bandwidth speech/noise.

Uses the production Audio NR2 wrapper, plus optional upstream RNNoise.
No receiver/profile access. Metrics are proxies, not human listening approval.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np
from scipy import signal
import soundfile as sf
from pystoi import stoi


RATE = 48000


def resample(audio, rate, target=RATE):
    divisor = math.gcd(rate, target)
    return signal.resample_poly(audio, target // divisor, rate // divisor)


def bandpass(audio, low=300, high=2700):
    return signal.sosfiltfilt(signal.butter(6, [low, high], btype='bandpass', fs=RATE, output='sos'), audio)


def speech(path, start=0):
    audio, rate = sf.read(path)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    audio = bandpass(resample(audio, rate))
    active = np.flatnonzero(np.abs(audio) > np.max(np.abs(audio)) * .02)
    first = max(0, active[0] - 480) + int(start * RATE)
    audio = audio[first:first + 8 * RATE]
    audio = np.pad(audio, (0, max(0, 8 * RATE - len(audio))))
    audio *= .035 / np.sqrt(np.mean(audio**2))
    return np.pad(audio, (RATE, RATE // 2))


def fixture(clean, kind, snr, seed):
    rng = np.random.default_rng(seed)
    t = np.arange(len(clean)) / RATE
    noise = rng.normal(size=len(clean))
    if kind == 'colored':
        noise = signal.lfilter([.05], [1, -.95], noise)
    if kind == 'step':
        noise *= np.where(t < 4, .35, 2)
    if kind == 'impulse':
        indexes = rng.integers(0, len(t), 100)
        noise[indexes] += rng.normal(0, 100, len(indexes))
    noise = bandpass(noise)
    rms = np.sqrt(np.mean(clean[RATE:-RATE // 2]**2))
    noise *= rms / np.sqrt(np.mean(noise**2)) / 10**(snr / 20)
    if kind == 'wander':
        hz = 1400 + 180 * np.sin(2 * np.pi * 1.13 * t)
        noise += rms * 2 * np.sin(np.cumsum(2 * np.pi * hz / RATE))
    if kind == 'clean':
        noise *= 0
    if kind == 'fading':
        clean = clean * (.55 + .45 * np.cos(2 * np.pi * .7 * t))
    noisy = clean + noise
    headroom = min(1., .8 / np.max(np.abs(noisy)))
    return clean * headroom, noisy * headroom


def align(clean, output):
    size = min(len(clean), len(output))
    # Integer-delay compensation only; do not equalize spectral distortion.
    corr = signal.correlate(output[:size], clean[:size], mode='full', method='fft')
    center, bound = size - 1, 7200
    delay = int(np.argmax(np.abs(corr[center - bound:center + bound + 1])) - bound)
    if delay >= 0:
        output = output[delay:]
    else:
        clean = clean[-delay:]
    n = min(len(clean), len(output))
    return clean[:n], output[:n], delay


def metrics(clean, output):
    clean, output, delay = align(clean, output)
    # Omit the startup/padding and score equal speech duration in each case.
    clean, output = clean[RATE:8 * RATE], output[RATE:8 * RATE]
    assert np.isfinite(output).all() and len(output) > 6 * RATE
    alpha = np.dot(clean, output) / np.dot(clean, clean)
    error = output - alpha * clean
    sisdr = 10 * np.log10(max(alpha**2 * np.dot(clean, clean), 1e-30) / max(np.dot(error, error), 1e-30))
    reference, enhanced = resample(clean, RATE, 8000), resample(output, RATE, 8000)
    return {'estoi': float(stoi(reference, enhanced, 8000, extended=True)),
            'stoi': float(stoi(reference, enhanced, 8000, extended=False)),
            'si_sdr_db': float(sisdr), 'delay_samples': delay,
            'wanted_level_db': float(20 * np.log10(max(abs(alpha), 1e-15)))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--rnnoise', type=Path, required=True)
    parser.add_argument('--gtcrn', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    voices = [('human-a', args.data / 'OSR_us_000_0010_8k.wav', 0),
              ('human-b', args.data / 'OSR_us_000_0010_8k.wav', 16),
              ('tts-male', args.data / 'speech-male.aiff', 0)]
    kinds = [('white', 0), ('white', -5), ('colored', 0), ('step', 0),
             ('impulse', 0), ('wander', 0), ('fading', 0), ('clean', 40)]
    rows = []
    methods = ['omlsa', 'rnnoise'] + (['gtcrn'] if args.gtcrn else [])
    for voice, path, start in voices:
        original = speech(path, start)
        for index, (kind, snr) in enumerate(kinds):
            clean, noisy = fixture(original, kind, snr, 817 + index)
            base = {'voice': voice, 'noise': kind, 'snr_db': snr}
            rows.append({**base, 'method': 'off', **metrics(clean, noisy)})
            source = args.output / 'input.f32'
            noisy.astype('<f4').tofile(source)
            clips = {'clean': clean, 'off': noisy}
            for method in methods:
                destination = args.output / f'{method}.f32'
                command = [str(args.runner.resolve()), method, str(source.resolve()),
                           str(destination.resolve()), str(args.resources.resolve()),
                           '1984', '1', str(args.rnnoise.resolve())]
                if method == 'gtcrn':
                    command.append(str(args.gtcrn.resolve()))
                completed = subprocess.run(command, text=True, capture_output=True, check=True)
                timing = json.loads(next(line for line in reversed(completed.stdout.splitlines()) if line.startswith('{')))
                wet = np.fromfile(destination, dtype='<f4')
                rows.append({**base, 'method': method, **metrics(clean, wet), 'timing': timing})
                clips[method] = wet
            print(json.dumps({'case': base, 'estoi': {row['method']: row['estoi'] for row in rows[-len(methods)-1:]}}), flush=True)
            if voice == 'human-b' and kind in ['white', 'wander'] and snr == 0:
                aligned = {name: align(clean, value)[1] if name not in ['clean', 'off'] else value for name, value in clips.items()}
                n = min(len(value) for value in aligned.values())
                scaled = {}
                for name, value in aligned.items():
                    alpha = np.dot(clean[RATE:n], value[RATE:n]) / np.dot(clean[RATE:n], clean[RATE:n])
                    scaled[name] = value[:n] / alpha
                headroom = .85 / max(np.max(np.abs(value)) for value in scaled.values())
                for name, value in scaled.items():
                    sf.write(args.output / f'{kind}-{name}.wav', value * headroom, RATE, subtype='PCM_16')
    summary = {}
    for method in ['off', *methods]:
        subset = [row for row in rows if row['method'] == method and row['noise'] != 'clean']
        summary[method] = {metric: float(np.mean([row[metric] for row in subset]))
                           for metric in ['estoi', 'stoi', 'si_sdr_db', 'wanted_level_db']}
        if method != 'off':
            summary[method]['one_core_percent_median'] = float(np.median([row['timing']['one_core_percent'] for row in subset]))
    result = {'sample_rate': RATE, 'cases': 24, 'speech_source': 'Open Speech Repository plus macOS synthesized male voice',
              'limitations': ['One human speaker in two excerpts and one synthetic voice',
                              'Controlled 300-2700 Hz audio, no off-air recordings',
                              'eSTOI/STOI and SI-SDR are proxies, not human listening approval',
                              'Single-run isolated CPU timings; no GUI/plugin measurement',
                              'DeepFilterNet not included in these measurements'],
              'summary_excluding_clean': summary, 'measurements': rows}
    (args.output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
