#!/usr/bin/env python3
"""Offline, level-matched neural NR gain and spectral-suppression experiments."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from audio_nr_evaluate import RATE, align, fixture, metrics, resample, speech
from audio_neural_probe import GTCRN


def variants(engine, audio):
    source = resample(audio, RATE, 16000).astype('float32')
    padded = np.pad(source, (256, 512))
    window = np.sqrt(.5 - .5 * np.cos(2 * np.pi * np.arange(512) / 512))
    results = {}
    for target in [0., .1, .2]:
        cache = [np.zeros(s, dtype='float32') for s in
                 [(2, 1, 16, 16, 33), (2, 3, 1, 1, 16), (2, 1, 33, 16)]]
        outputs = {power: np.zeros(len(padded)) for power in [1., 1.5, 2.]}
        level = 0.
        for offset in range(0, len(padded) - 511, 256):
            block = padded[offset:offset + 512]
            rms = np.sqrt(np.mean(block**2))
            level = max(rms, .98 * level)
            scale = min(1., target / max(level, 1e-6)) if target else 1.
            spectrum = np.fft.rfft(block * window)
            x = spectrum * scale
            tensor = np.stack([x.real, x.imag], axis=-1).astype('float32')[None, :, None, :]
            enhanced, *cache = engine.session.run(None, dict(zip(
                ['mix', 'conv_cache', 'tra_cache', 'inter_cache'], [tensor, *cache])))
            y = (enhanced[0, :, 0, 0] + 1j * enhanced[0, :, 0, 1]) / scale
            mask = np.clip(abs(y) / np.maximum(abs(spectrum), 1e-10), .001, 1.)
            for power, output in outputs.items():
                z = y * mask**(power - 1)
                output[offset:offset + 512] += np.fft.irfft(z, n=512) * window
        for power, output in outputs.items():
            key = f'cap{target}-{power}'
            results[key] = resample(output[256:256 + len(source)], 16000, RATE)[:len(audio)]
    return results


def score(clean, wet):
    result = metrics(clean, wet)
    _, aligned, _ = align(clean, wet)
    # Compare residual pauses after matching wanted speech level, not raw loudness.
    noise = np.sqrt(np.mean(aligned[int(.4*RATE):int(.85*RATE)]**2))
    result['pause_noise_db_matched'] = float(20 * np.log10(max(noise, 1e-15)) - result['wanted_level_db'])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    data = Path('/tmp/beige-nr-research-20261022')
    model = root / 'build-beige/neural-dependency/gtcrn.onnx'
    engine = GTCRN(model)
    rows = []
    voices = [('human-a', data/'OSR_us_000_0010_8k.wav', 0),
              ('human-b', data/'OSR_us_000_0010_8k.wav', 16),
              ('tts-male', data/'speech-male.aiff', 0)]
    cases = [('white', 0), ('white', -5), ('colored', 0), ('step', 0),
             ('impulse', 0), ('wander', 0), ('fading', 0), ('clean', 40)]
    if args.pilot:
        voices, cases = voices[:1], cases[:1]
    for voice, path, start in voices:
        for index, (kind, snr) in enumerate(cases):
            clean, noisy = fixture(speech(path, start), kind, snr, 817+index)
            for gain in [1., 32.]:
                reference, source = clean*gain, noisy*gain
                outputs = variants(engine, source)
                source.astype('<f4').tofile(args.output/'input.f32')
                for method in ['gtcrn', 'omlsa']:
                    subprocess.run([str(args.runner.resolve()), method,
                        str((args.output/'input.f32').resolve()), str((args.output/'wet.f32').resolve()),
                        str(root/'root/res'), '1984', '1', '-', str(model)],
                        check=True, capture_output=True, text=True)
                    outputs[method] = np.fromfile(args.output/'wet.f32', dtype='<f4')
                for method, output in outputs.items():
                    rows.append({'voice': voice, 'noise': kind, 'snr_db': snr,
                                 'gain': gain, 'method': method, **score(reference, output)})
                summary = {r['method']: round(r['wanted_level_db'], 2) for r in rows[-len(outputs):]}
                print(json.dumps({'voice':voice, 'noise':kind, 'gain':gain, 'speech_gain_db':summary}), flush=True)
    (args.output/'results.json').write_text(json.dumps(rows, indent=2)+'\n')


if __name__ == '__main__':
    main()
