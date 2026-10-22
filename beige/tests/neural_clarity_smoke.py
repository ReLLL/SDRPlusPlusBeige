#!/usr/bin/env python3
"""Test independent clarity switches in an isolated four-module receiver."""
import argparse
import json
from pathlib import Path
import statistics
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--expect-default', action='store_true')
    args = parser.parse_args()

    def request(path, value=None):
        data = value.encode() if value is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url+path,data=data),timeout=15) as response:
            return response.read().decode()

    modules = json.loads(request('/modules'))
    names = sorted(v['module'] for v in modules.values())
    assert names in [sorted(['file_source','radio','noise_reduction_logmmse','null_audio_sink']),
                     sorted(['rtl_sdr_source','radio','noise_reduction_logmmse','null_audio_sink'])]
    audio = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/audio/Radio/'
    controls = ['detail','syllables','voice_lift']
    if args.expect_default:
        assert all(request(audio+k)=='false' for k in controls)
    request('/module/Radio/command',json.dumps({'cmd':'set_demod','args':'USB'}))
    request('/proc/null_audio_sink/select','Radio')
    request(audio+'neural','true')
    request('/sdr/start')
    rows = []
    for mask in [0,1,2,4,3,5,6,7,0]:
        for i,key in enumerate(controls):
            request(audio+key,'true' if mask & (1<<i) else 'false')
        assert [request(audio+k)=='true' for k in controls]==[bool(mask & (1<<i)) for i in range(3)]
        time.sleep(.7)
        before = int(request('/proc/null_audio_sink/samples'))
        loads, fps = [], []
        for _ in range(8):
            assert request(audio+'neural_status')=='Ready'
            loads.append(float(request(audio+'neural_cpu')))
            fps.append(json.loads(request('/proc/display/spectrum-stats'))['ui_fps'])
            time.sleep(.15)
        assert int(request('/proc/null_audio_sink/samples'))>before
        assert int(request('/proc/null_audio_sink/invalid_samples'))==0
        assert request(audio+'nr2')=='false'
        rows.append({'mask':mask,'cpu_median':statistics.median(loads),'fps_median':statistics.median(fps)})
        print(json.dumps(rows[-1]),flush=True)
    assert all(request(audio+k)=='false' for k in controls)
    args.output.write_text(json.dumps(rows,indent=2)+'\n')


if __name__ == '__main__':
    main()
