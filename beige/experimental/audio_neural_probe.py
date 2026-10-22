#!/usr/bin/env python3
"""Evaluate upstream streaming GTCRN separately from the installed receiver."""
import argparse
import json
from pathlib import Path
import time

import numpy as np
import onnxruntime as ort
import soundfile as sf

from audio_nr_evaluate import RATE, fixture, metrics, resample, speech


class GTCRN:
    def __init__(self, model, coreml=False):
        options = ort.SessionOptions()
        options.intra_op_num_threads = options.inter_op_num_threads = 1
        options.add_session_config_entry('session.intra_op.allow_spinning', '0')
        providers = ['CPUExecutionProvider']
        if coreml:
            providers.insert(0, ('CoreMLExecutionProvider', {'MLComputeUnits': 'ALL',
                                                           'RequireStaticInputShapes': '1'}))
        self.session = ort.InferenceSession(str(model), sess_options=options, providers=providers)

    def enhance(self, audio):
        source = resample(audio, RATE, 16000).astype('float32')
        padded = np.pad(source, (256, 512))
        window = np.sqrt(.5 - .5 * np.cos(2 * np.pi * np.arange(512) / 512)).astype('float32')
        result, weights = np.zeros(len(padded)), np.zeros(len(padded))
        cache = [np.zeros(shape, dtype='float32') for shape in
                 [(2, 1, 16, 16, 33), (2, 3, 1, 1, 16), (2, 1, 33, 16)]]
        times = []
        cpu = time.process_time()
        for offset in range(0, len(padded) - 511, 256):
            spectrum = np.fft.rfft(padded[offset:offset + 512] * window)
            tensor = np.stack([spectrum.real, spectrum.imag], axis=-1).astype('float32')[None, :, None, :]
            start = time.perf_counter()
            enhanced, *cache = self.session.run(None, dict(zip(
                ['mix', 'conv_cache', 'tra_cache', 'inter_cache'], [tensor, *cache])))
            times.append(time.perf_counter() - start)
            values = enhanced[0, :, 0]
            result[offset:offset + 512] += np.fft.irfft(values[:, 0] + 1j * values[:, 1], n=512) * window
            weights[offset:offset + 512] += window**2
        cpu = time.process_time() - cpu
        result /= np.maximum(weights, 1e-10)
        result = result[256:256 + len(source)]
        result = resample(result, 16000, RATE)[:len(audio)]
        return result, {'prototype_cpu_percent': cpu / (len(audio) / RATE) * 100,
                        'inference_ms_median': float(np.median(times) * 1000),
                        'inference_ms_p95': float(np.percentile(times, 95) * 1000),
                        'providers': self.session.get_providers()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--coreml-only', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    engine = GTCRN(args.model, args.coreml_only)
    if args.coreml_only:
        noisy = np.fromfile(args.output / 'input.f32', dtype='<f4')[:RATE * 2]
        wet, timing = engine.enhance(noisy)
        np.save(args.output / 'coreml.npy', wet)
        (args.output / 'coreml.json').write_text(json.dumps(timing, indent=2) + '\n')
        print(timing)
        return
    voices = [('human-a', args.data / 'OSR_us_000_0010_8k.wav', 0),
              ('human-b', args.data / 'OSR_us_000_0010_8k.wav', 16),
              ('tts-male', args.data / 'speech-male.aiff', 0)]
    kinds = [('white', 0), ('white', -5), ('colored', 0), ('step', 0),
             ('impulse', 0), ('wander', 0), ('fading', 0), ('clean', 40)]
    rows = []
    for voice, path, start in voices:
        original = speech(path, start)
        for index, (kind, snr) in enumerate(kinds):
            clean, noisy = fixture(original, kind, snr, 817 + index)
            wet, timing = engine.enhance(noisy)
            case = []
            for mix in [1., .75, .5]:
                output = mix * wet + (1 - mix) * noisy[:len(wet)]
                row = {'voice': voice, 'noise': kind, 'snr_db': snr, 'mix': mix,
                       **metrics(clean, output), 'timing': timing}
                rows.append(row)
                case.append(row['estoi'])
                if voice == 'human-b' and kind == 'white' and snr == 0:
                    sf.write(args.output / f'gtcrn-{mix}.wav', output, RATE)
            print(json.dumps({'voice': voice, 'noise': kind, 'snr': snr, 'estoi_full_75_50': case}), flush=True)
    summary = {}
    for mix in [1., .75, .5]:
        subset = [row for row in rows if row['mix'] == mix and row['noise'] != 'clean']
        summary[str(mix)] = {key: float(np.mean([row[key] for row in subset]))
                             for key in ['estoi', 'stoi', 'si_sdr_db', 'wanted_level_db']}
        summary[str(mix)]['prototype_cpu_percent_median'] = float(np.median([row['timing']['prototype_cpu_percent'] for row in subset]))
    result = {'summary_excluding_clean': summary, 'measurements': rows,
              'runtime': ort.__version__, 'model': str(args.model),
              'limits': 'Python/NumPy/ORT streaming prototype; no native receiver or live listening qualification'}
    (args.output / 'gtcrn-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
