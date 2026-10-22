#!/usr/bin/env python3
"""Compare math backends and 2D/3D drawing in an isolated File QA receiver."""
import argparse
import json
import statistics
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()

    def request(path, body=None):
        data = body.encode() if body is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=8) as response:
            return response.read().decode()

    modules = json.loads(request('/modules'))
    assert sorted(v['module'] for v in modules.values()) == [
        'file_source', 'noise_reduction_logmmse', 'null_audio_sink', 'radio'
    ], 'Use a separate QA profile'
    nr = '/proc/modules/noise_reduction_logmmse/Noise%20Reduction%20logmmse/'
    results = []
    for mode, apple in [('true', False), ('true', True), ('false', True), ('false', False), ('true', False)]:
        request('/proc/display/spectrum3d', mode)
        request(nr + 'acceleration', 'true' if apple else 'false')
        time.sleep(3)
        before = int(request('/proc/null_audio_sink/samples'))
        stats = []
        for _ in range(12):
            stats.append(json.loads(request('/proc/display/spectrum-stats')))
            time.sleep(0.25)
        samples = int(request('/proc/null_audio_sink/samples')) - before
        assert samples > 0
        assert max(v['history_rows'] for v in stats) > 0
        results.append({'mode': mode, 'automatic': apple, 'backend': request(nr + 'backend'),
                        'fps_median': statistics.median(v['ui_fps'] for v in stats),
                        'fps_min': min(v['ui_fps'] for v in stats),
                        'draw_us_median': statistics.median(v['frame_us'] for v in stats),
                        'samples': samples})
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
