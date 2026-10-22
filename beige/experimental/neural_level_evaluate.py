#!/usr/bin/env python3
"""Compare actual before/after neural wrappers at ordinary and radio AGC levels."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

import numpy as np
from audio_nr_evaluate import fixture, speech
from neural_quality_probe import score


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    data = Path('/tmp/beige-nr-research-20261022')
    model = root / 'build-beige/neural-dependency/gtcrn.onnx'
    voices = [('human-a', data/'OSR_us_000_0010_8k.wav', 0),
              ('human-b', data/'OSR_us_000_0010_8k.wav', 16),
              ('tts-male', data/'speech-male.aiff', 0),
              ('tts-heldout', data/'speech.aiff', 0)]
    cases = [('white', 0), ('white', -5), ('colored', 0), ('step', 0),
             ('impulse', 0), ('wander', 0), ('fading', 0), ('clean', 40)]
    rows = []
    for voice, path, start in voices:
        for index, (kind, snr) in enumerate(cases):
            clean, noisy = fixture(speech(path, start), kind, snr, 817+index)
            for gain in [1., 32.]:
                reference = clean*gain
                (noisy*gain).astype('<f4').tofile(args.output/'input.f32')
                for method, runner in [('before', args.before), ('after', args.after), ('nr2', args.before)]:
                    completed = subprocess.run([str(runner.resolve()), 'omlsa' if method=='nr2' else 'gtcrn',
                        str((args.output/'input.f32').resolve()), str((args.output/'wet.f32').resolve()),
                        str(root/'root/res'), '1984', '1', '-', str(model)],
                        check=True, capture_output=True, text=True)
                    timing = json.loads(next(line for line in completed.stdout.splitlines() if line.startswith('{')))
                    wet = np.fromfile(args.output/'wet.f32', dtype='<f4')
                    rows.append({'voice': voice, 'noise': kind, 'snr_db': snr, 'gain': gain,
                                 'method': method, **score(reference, wet), 'timing': timing})
                print(json.dumps({'voice':voice, 'noise':kind, 'gain':gain,
                    'level_db':{r['method']:round(r['wanted_level_db'],2) for r in rows[-3:]}}), flush=True)
    summary = {}
    for gain in [1.,32.]:
        summary[str(gain)] = {}
        for method in ['before','after','nr2']:
            subset = [r for r in rows if r['gain']==gain and r['method']==method and r['noise']!='clean']
            summary[str(gain)][method] = {k:statistics.mean(r[k] for r in subset)
                for k in ['estoi','si_sdr_db','wanted_level_db','pause_noise_db_matched']}
            summary[str(gain)][method]['cpu_percent_median'] = statistics.median(r['timing']['one_core_percent'] for r in subset)
    (args.output/'results.json').write_text(json.dumps({'summary':summary,'rows':rows},indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    main()
