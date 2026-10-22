#!/usr/bin/env python3
"""Save all research outcomes, paired summaries and reproducibility metadata."""
import argparse
import collections
import hashlib
import importlib.metadata
import json
from pathlib import Path
import platform
import statistics
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    measurements, paired, provenance = [], [], {}
    for filename, suite in [('speech-results.json', 'development-a'),
                            ('speech-more-results.json', 'development-b'),
                            ('speech-timing-results.json', 'development-c'),
                            ('holdout-results.json', 'holdout'),
                            ('holdout-800k-results.json', 'holdout-800k')]:
        data = json.loads((args.data / filename).read_text())
        provenance[suite] = {k: v for k, v in data.items() if k != 'results'}
        groups = collections.defaultdict(dict)
        for row in data['results']:
            measurements.append({'suite': suite, **row})
            groups[(row['voice'], row['noise'], row['input_snr'])][row['mode']] = row
        modes = sorted({r['mode'] for r in data['results']} - {'off', 'reference'})
        for mode in modes:
            comparisons = [(g[mode], g['reference']) for g in groups.values() if mode in g]
            deltas = [a['estoi'] - b['estoi'] for a, b in comparisons]
            paired.append({'suite': suite, 'mode': mode, 'cases': len(comparisons),
                           'mean_estoi_delta': statistics.mean(deltas),
                           'worst_estoi_delta': min(deltas),
                           'mean_stoi_delta': statistics.mean(a['stoi'] - b['stoi'] for a, b in comparisons),
                           'mean_si_sdr_delta_db': statistics.mean(a['si_sdr_db'] - b['si_sdr_db'] for a, b in comparisons),
                           'median_cpu_ratio': statistics.median(a['cpu_percent'] / b['cpu_percent'] for a, b in comparisons)})
    guards = json.loads((args.data / 'guardrail-results.json').read_text())
    measurements.extend({'suite': 'guardrails', **r} for r in guards)
    benchmark = json.loads((args.data / 'benchmark-results.json').read_text())
    groups = collections.defaultdict(list)
    for row in benchmark['runs']:
        groups[(row['rate'], row['chunk'], row['mode'])].append(row['cpu_percent'])
    cpu = [{'rate': rate, 'chunk': chunk, 'mode': mode, 'median_cpu_percent': statistics.median(values)}
           for (rate, chunk, mode), values in groups.items()]
    (args.output / 'measurements.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in measurements))
    (args.output / 'benchmark.json').write_text(json.dumps(benchmark, indent=2) + '\n')
    (args.output / 'summary.json').write_text(json.dumps({'speech': paired, 'cpu': cpu,
                                                       'guardrails': guards,
                                                       'partition_tests': benchmark['partitions']}, indent=2) + '\n')
    repo = Path(__file__).resolve().parents[2]
    source_files = ['nr_lab.cpp', 'nr_lab_checks.h', 'logmmse_variants.h', 'nr_evaluate.py',
                    'nr_guardrails.py', 'nr_benchmark.py', 'nr_summarize.py', 'nr_listening.py']
    metadata = {'baseline_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
                'platform': platform.platform(), 'python': platform.python_version(),
                'packages': {name: importlib.metadata.version(name) for name in ['numpy', 'scipy', 'soundfile', 'pystoi']},
                'cpu': subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip(),
                'source_sha256': {name: sha(Path(__file__).parent / name) for name in source_files},
                'input_download': json.loads((args.data / 'speech-download.json').read_text()),
                'suites': provenance,
                'research_dependency': 'pystoi==0.4.1 (MIT), evaluation only; numpy/scipy/soundfile from host',
                'installed_app_changed': False,
                'limitations': ['One human speaker; two synthetic voices; synthetic RF/noise, no RF capture',
                                'eSTOI/STOI are reference metrics, not QSO word accuracy or listening approval',
                                'No decoder or GUI frame-rate qualification for a new production reducer',
                                'Initial pilot with an incorrectly ignored gain-cap return value was discarded']}
    (args.output / 'provenance.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps({'records': len(measurements), 'speech_comparisons': len(paired), 'cpu_conditions': len(cpu)}, indent=2))


if __name__ == '__main__':
    main()
