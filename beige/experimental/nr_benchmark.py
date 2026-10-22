#!/usr/bin/env python3
"""Paired offline CPU and packet-size measurements, with no GUI processes."""
import argparse
import json
from pathlib import Path
import statistics

import numpy as np

from nr_evaluate import fixture, run, speech


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    args = parser.parse_args()
    root, runner = args.data.resolve(), args.runner.resolve()
    audio = speech(root / 'OSR_us_000_0010_8k.wav', 4)
    rows = []
    for rate, chunk in [(204800, 1024), (800000, 4000), (800000, 1984)]:
        mixed, _ = fixture(audio, rate, 'white', 0, 131071)
        modes = ['reference', 'wiener', 'framewise', 'framewiener', 'balanced']
        for repeat in range(3):
            order = modes if repeat % 2 == 0 else list(reversed(modes))
            for mode in order:
                _, meta = run(runner, mixed, rate, mode, root, chunk=chunk)
                record = {**meta, 'chunk': chunk, 'repeat': repeat}
                rows.append(record)
                print(json.dumps(record), flush=True)
        for mode in modes:
            cpu = [v['cpu_percent'] for v in rows if v['rate'] == rate and v['chunk'] == chunk and v['mode'] == mode]
            print('MEDIAN', rate, chunk, mode, statistics.median(cpu), flush=True)
    # Exact packet-independence of the candidate, compared with the original.
    mixed, _ = fixture(audio, 204800, 'step', 0, 131071)
    partitions = []
    for mode in ['reference', 'framewise', 'framewiener']:
        a, _ = run(runner, mixed, 204800, mode, root, chunk=317)
        b, _ = run(runner, mixed, 204800, mode, root, chunk=4096)
        assert len(a) == len(b)
        difference = float(np.max(np.abs(a - b)))
        relative = float(np.sqrt(np.mean(np.abs(a - b)**2) / np.mean(np.abs(a)**2)))
        record = {'mode': mode, 'max_absolute_error': difference, 'relative_rms_error': relative}
        partitions.append(record)
        if mode != 'reference':
            assert difference < 1e-6
        print('PARTITIONS', json.dumps(record), flush=True)
    (root / 'benchmark-results.json').write_text(json.dumps({'runs': rows, 'partitions': partitions}, indent=2) + '\n')


if __name__ == '__main__':
    main()
