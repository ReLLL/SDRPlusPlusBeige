#!/usr/bin/env python3
"""Offline clarity comparison with 1.3.24 parity at native floating-point precision."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess

import numpy as np
from audio_nr_evaluate import RATE, align, fixture, speech
from neural_quality_probe import score


def quiet_correlation(clean, wet):
    reference, output, _ = align(clean, wet)
    n = min(len(reference), len(output), 8*RATE)
    n = (n-RATE)//768*768
    a = reference[RATE:RATE+n].reshape(-1,768)
    b = output[RATE:RATE+n].reshape(-1,768)
    power = np.mean(a*a, axis=1)
    active = power > power.max()*.001
    quiet = active & (power <= np.percentile(power[active],40))
    correlation = np.sum(a*b,axis=1)/np.maximum(np.sqrt(np.sum(a*a,axis=1)*np.sum(b*b,axis=1)),1e-20)
    return float(np.mean(correlation[quiet]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pilot', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    root = Path(__file__).resolve().parents[2]
    data = Path('/tmp/beige-nr-research-20261022')
    model = root/'build-beige/neural-dependency/gtcrn.onnx'
    voices = [('human-a',data/'OSR_us_000_0010_8k.wav',0),
              ('human-b',data/'OSR_us_000_0010_8k.wav',16),
              ('tts-male',data/'speech-male.aiff',0),('tts-other',data/'speech.aiff',0)]
    cases = [('white',0),('white',-5),('colored',0),('step',0),
             ('impulse',0),('wander',0),('fading',0),('clean',40)]
    if args.pilot:
        voices, cases = voices[::3], [cases[i] for i in [0,5,7]]
    rows = []
    parity = []
    for voice,path,start in voices:
        for index,(kind,snr) in enumerate(cases):
            clean,noisy = fixture(speech(path,start),kind,snr,817+index)
            for gain in ([32.] if args.pilot else [1.,32.]):
                clean_scaled = clean*gain
                (noisy*gain).astype('<f4').tofile(args.output/'input.f32')
                baseline = None
                for name,options,runner in [('baseline',0,args.baseline),('off',0,args.runner),
                        ('detail',1,args.runner),('smooth',2,args.runner),('both',3,args.runner),('lift',4,args.runner)]:
                    destination = args.output/(name+'.f32')
                    completed = subprocess.run([str(runner.resolve()),'gtcrn',str((args.output/'input.f32').resolve()),
                        str(destination.resolve()),str(root/'root/res'),'1984','1','-',str(model),str(options)],
                        check=True,capture_output=True,text=True)
                    output = np.fromfile(destination,dtype='<f4')
                    timing = json.loads(next(line for line in completed.stdout.splitlines() if line.startswith('{')))
                    if name == 'baseline':
                        baseline = output.copy()
                    if name == 'off':
                        error = output-baseline
                        relative = float(np.linalg.norm(error)/max(np.linalg.norm(baseline),1e-20))
                        peak_error = float(np.max(abs(error))/max(np.max(abs(baseline)),1e-20))
                        # The unchanged 1.3.24 binary also varies at ~1e-6 between runs.
                        assert relative < 1e-5 and peak_error < 2e-5, 'Default changed from 1.3.24'
                        parity.append({'relative_rms':relative,'relative_peak_error':peak_error})
                    rows.append({'voice':voice,'noise':kind,'snr_db':snr,'gain':gain,'method':name,
                        **score(clean_scaled,output),'quiet_frame_correlation':quiet_correlation(clean_scaled,output),
                        'cpu_percent':timing['one_core_percent']})
                print(json.dumps({'voice':voice,'noise':kind,'gain':gain,
                    'estoi':{r['method']:round(r['estoi'],4) for r in rows[-6:]}}),flush=True)
    summary = {}
    for name in ['baseline','off','detail','smooth','both','lift']:
        subset = [r for r in rows if r['method']==name and r['noise']!='clean']
        summary[name] = {key:statistics.mean(r[key] for r in subset) for key in
            ['estoi','si_sdr_db','wanted_level_db','pause_noise_db_matched','quiet_frame_correlation','cpu_percent']}
    (args.output/'results.json').write_text(json.dumps({'summary':summary,'rows':rows,
        'baseline_parity':parity,'limitations':'Controlled speech; one human speaker and synthetic voices. Quiet-frame correlation is a proxy, not phoneme recognition or live listening acceptance.'},indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    main()
