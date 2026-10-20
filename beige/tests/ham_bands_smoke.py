#!/usr/bin/env python3
"""Exercise the running isolated QA receiver; never use a normal receiver profile."""
import argparse
import json
import urllib.request

CENTERS = {
    '160m': 1900000, '80m': 3750000, '60m': 5367500, '40m': 7150000,
    '30m': 10125000, '20m': 14175000, '17m': 18114000, '15m': 21225000,
    '12m': 24940000, '10m': 28850000, '6m': 52000000, '2m': 146000000,
    '1.25m': 223500000, '70cm': 435000000, '33cm': 915000000, '23cm': 1270000000,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--center-tuning', action='store_true')
    args = parser.parse_args()

    def request(path, body=None):
        data = body.encode() if body is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=5) as response:
            return response.read().decode()

    def state():
        return json.loads(request('/proc/display/ham-band-state'))

    assert state()['center_tuning'] == args.center_tuning
    results = []
    for label, center in CENTERS.items():
        before = state()
        request('/vfo/set_offset?name=Radio&offset=15000')
        prepared = state()
        if args.center_tuning:
            assert prepared['frequency'] != before['frequency'], 'Centered mode must follow the VFO move'
        else:
            assert prepared['vfo_offset'] != 0, 'Free mode must start with an off-center VFO'
        request('/proc/display/ham-band', label)
        actual = state()
        assert actual['frequency'] == center, (label, actual)
        assert actual['spectrum_center'] == center, (label, actual)
        assert actual['view_offset'] == 0 and actual['vfo_offset'] == 0, (label, actual)
        assert actual['center_tuning'] == args.center_tuning, (label, actual)
        assert actual['single_click_vfo'] == args.center_tuning, (label, actual)
        assert request('/proc/display/ham-band') == label
        results.append({'band': label, **actual})
    print(json.dumps({'passed': len(results), 'center_tuning': args.center_tuning, 'results': results}))


if __name__ == '__main__':
    main()
