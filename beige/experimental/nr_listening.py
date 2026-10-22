#!/usr/bin/env python3
"""Prepare level-matched listening examples; no effect on scored measurements."""
import argparse
import json
from pathlib import Path

import numpy as np
import soundfile as sf

from nr_evaluate import demod, fixture, run, speech


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rate = 204800
    mixed, wanted = fixture(speech(args.data / 'OSR_us_000_0010_8k.wav', 8), rate, 'white', 0, 104730)
    clips = {'clean': demod(wanted, rate), 'noisy': demod(mixed, rate)}
    for mode in ['reference', 'framewiener', 'balanced']:
        output, _ = run(args.runner.resolve(), mixed, rate, mode, args.data.resolve())
        clips[mode] = demod(output, rate)
    n = min(len(clip) for clip in clips.values())
    clean = clips['clean'][:n]
    projections = {}
    for mode, clip in clips.items():
        clip = clip[:n]
        a, b = clean[3200:-160], clip[3200:-160]
        projection = float(np.dot(a, b) / np.dot(a, a))
        projections[mode] = projection
        clips[mode] = clip / projection
    # Equal wanted-speech level, then ONE common headroom gain. Extra residual
    # noise must not make a denoiser seem better just because it sounds louder.
    headroom = 0.85 / max(np.max(np.abs(clip)) for clip in clips.values())
    for mode, clip in clips.items():
        sf.write(args.output / f'{mode}.wav', clip * headroom, 8000, subtype='PCM_16')
    comparison = np.concatenate([np.concatenate([clips[mode] * headroom, np.zeros(4000)])
                                for mode in ['noisy', 'reference', 'framewiener', 'balanced']])
    sf.write(args.output / 'comparison.wav', comparison, 8000, subtype='PCM_16')
    metadata = {'source': 'Open Speech Repository', 'case': 'female-human white 0 dB',
                'order': ['noisy', 'reference', 'framewiener', 'balanced'],
                'gap_seconds': 0.5, 'seconds_per_clip': n / 8000,
                'wanted_projection_before_matching': projections,
                'common_headroom_gain': float(headroom),
                'normalization': 'Equal projected wanted-speech level; listening only, not scored metrics'}
    (args.output / 'listening.json').write_text(json.dumps(metadata, indent=2) + '\n')


if __name__ == '__main__':
    main()
