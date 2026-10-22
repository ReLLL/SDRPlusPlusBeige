#!/usr/bin/env python3
"""Offline, referenced speech-quality evaluation; never opens a receiver profile.

Requires numpy/scipy/soundfile and optional research dependency pystoi==0.4.1.
Human speech source: Open Speech Repository, american/OSR_us_000_0010_8k.wav.
The second development voice and holdout use local macOS speech synthesis.
Source attribution required by the Open Speech Repository conditions of use.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

import numpy as np
import soundfile as sf
from scipy import signal
from pystoi import stoi

MODES = ['reference', 'cap', 'fast', 'smooth', 'bias', 'bootstrap', 'tracker', 'blend95', 'blend85']
SOURCE = 'https://www.voiptroubleshooter.com/open_speech/american.html'


def resample(data, source, target):
    divisor = math.gcd(source, target)
    return signal.resample_poly(data, target // divisor, source // divisor)


def demod(iq, rate, offset=16000):
    t = np.arange(len(iq)) / rate
    audio = resample(iq * np.exp(-2j * np.pi * offset * t), rate, 8000).real
    return signal.sosfiltfilt(signal.butter(6, [300, 2700], btype='bandpass', fs=8000, output='sos'), audio)


def speech(path, seconds, start=0):
    audio, fs = sf.read(path)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    audio = resample(audio, fs, 8000)
    audio = signal.sosfiltfilt(signal.butter(6, [300, 2700], btype='bandpass', fs=8000, output='sos'), audio)
    active = np.flatnonzero(np.abs(audio) > np.max(np.abs(audio)) * 0.02)
    first = max(0, active[0] - 80) + int(start * 8000)
    audio = audio[first:first + int(seconds * 8000)]
    audio = np.pad(audio, (0, max(0, int(seconds * 8000) - len(audio))))
    return audio / np.sqrt(np.mean(audio**2)) * 0.05


def fixture(audio, rate, kind, snr, seed):
    carrier = 16000
    desired = resample(signal.hilbert(audio), 8000, rate)
    t = np.arange(len(desired)) / rate
    desired *= np.exp(2j * np.pi * carrier * t)
    rng = np.random.default_rng(seed)
    noise = rng.normal(size=len(t)) + 1j * rng.normal(size=len(t))
    if kind == 'colored':
        shaped = signal.lfilter([0.05], [1, -0.95], noise)
        noise = shaped * np.exp(2j * np.pi * carrier * t) + noise * 0.08
    elif kind == 'step':
        noise *= np.where(t < t[-1] / 2, 0.35, 2.0)
    elif kind == 'impulses':
        spikes = np.zeros(len(t), dtype=complex)
        indexes = rng.integers(0, len(t), int(t[-1] * 12))
        spikes[indexes] = (rng.normal(size=len(indexes)) + 1j * rng.normal(size=len(indexes))) * 250
        noise += spikes
    if kind == 'fading':
        desired *= 0.55 + 0.45 * np.cos(2 * np.pi * 0.7 * t)
    clean = demod(desired, rate)
    noise_audio = demod(noise, rate)
    noise *= np.sqrt(np.mean(clean**2) / np.mean(noise_audio**2)) / 10**(snr / 20)
    if kind == 'adjacent':
        noise += 1.0 * np.exp(2j * np.pi * (carrier + 4500) * t)
    elif kind == 'wander':
        drift = np.cumsum(rng.normal(0, 0.4, len(t)))
        drift = signal.sosfilt(signal.butter(2, 5, fs=rate, output='sos'), drift)
        drift *= 180 / max(np.std(drift), 1e-10)
        phase = np.cumsum(2 * np.pi * (carrier + 1400 + drift) / rate)
        noise += 0.15 * np.exp(1j * phase)
    # A common input gain avoids clipping without changing any SNR.
    gain = min(1, 0.85 / np.max(np.abs(desired + noise)))
    return ((desired + noise) * gain).astype('<c8'), (desired * gain).astype('<c8')


def metrics(clean, output):
    n = min(len(clean), len(output))
    clean, output = clean[3200:n - 160], output[3200:n - 160]
    assert len(clean) > 16000
    assert np.isfinite(output).all()
    alpha = np.dot(output, clean) / max(np.dot(clean, clean), 1e-30)
    error = output - alpha * clean
    sisdr = 10 * np.log10(max(alpha**2 * np.dot(clean, clean), 1e-30) / max(np.dot(error, error), 1e-30))
    return {'si_sdr_db': float(sisdr), 'stoi': float(stoi(clean, output, 8000, extended=False)),
            'estoi': float(stoi(clean, output, 8000, extended=True)),
            'wanted_level_db': float(20 * np.log10(max(abs(alpha), 1e-15)))}


def run(runner, data, rate, mode, root, apple=True, chunk=None):
    input_path, output_path = root / 'input.cf32', root / 'output.cf32'
    data.tofile(input_path)
    args = [str(runner), str(input_path), str(output_path), str(rate), mode, str(int(apple))]
    if chunk:
        args.append(str(chunk))
    completed = subprocess.run(args, capture_output=True, text=True, check=True)
    meta = json.loads(next(v for v in reversed(completed.stdout.splitlines()) if v.startswith('{')))
    output = np.fromfile(output_path, dtype='<c8')
    assert len(output) == meta['samples']
    return output, meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--rate', type=int, default=204800)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--modes', nargs='+', default=MODES)
    parser.add_argument('--holdout', action='store_true')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--chunk', type=int)
    args = parser.parse_args()
    args.runner = args.runner.resolve()
    root = args.data.resolve()
    results = []
    cases = [('white', -5), ('white', 0), ('white', 10), ('white', 30),
             ('step', 0), ('colored', 0), ('fading', 0), ('impulses', 0),
             ('adjacent', 0), ('wander', 0)]
    if args.holdout:
        cases = [('white', -3), ('white', 5), ('step', 5), ('colored', 5), ('fading', 5)]
    sources = [('female-human', 'OSR_us_000_0010_8k.wav'), ('male-synthetic', 'speech-male.aiff')]
    if args.holdout:
        sources = [('female-human-heldout', 'OSR_us_000_0010_8k.wav'), ('female-synthetic-heldout', 'speech.aiff')]
    for voice, filename in sources:
        start = (14 if voice == 'female-human-heldout' else 3) if args.holdout else 0
        clean_audio = speech(root / filename, args.seconds, start)
        for index, (kind, snr) in enumerate(cases):
            mixed, wanted = fixture(clean_audio, args.rate, kind, snr, 104729 + index + (100 if args.holdout else 0))
            clean = demod(wanted, args.rate)
            off = demod(mixed, args.rate)
            common = {'voice': voice, 'noise': kind, 'input_snr': snr}
            results.append({**common, 'mode': 'off', **metrics(clean, off)})
            for mode in args.modes:
                output, meta = run(args.runner, mixed, args.rate, mode, root, chunk=args.chunk)
                if mode == 'reference':
                    clone, _ = run(args.runner, mixed, args.rate, 'clone', root, chunk=args.chunk)
                    assert np.array_equal(output, clone), 'Experimental no-change path differs from production'
                record = {**common, **meta, **metrics(clean, demod(output, args.rate))}
                results.append(record)
                print(json.dumps(record), flush=True)
                if voice == 'female-human' and kind == 'white' and snr == 0:
                    # Common gain across previews. No individual loudness fitting.
                    sf.write(root / f'preview-{mode}.wav', demod(output, args.rate), 8000, subtype='PCM_16')
            if voice == 'female-human' and kind == 'white' and snr == 0:
                sf.write(root / 'preview-clean.wav', clean, 8000, subtype='PCM_16')
                sf.write(root / 'preview-off.wav', off, 8000, subtype='PCM_16')
    provenance = {'speech_source': SOURCE, 'rate': args.rate, 'seconds': args.seconds,
                  'holdout': args.holdout, 'modes': args.modes,
                  'chunk': args.chunk or args.rate // 200,
                  'speech_sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for _, name in sources},
                  'results': results}
    output = args.output or root / ('holdout-results.json' if args.holdout else 'speech-results.json')
    output.write_text(json.dumps(provenance, indent=2) + '\n')
    print('RESULTS', output, flush=True)


if __name__ == '__main__':
    main()
