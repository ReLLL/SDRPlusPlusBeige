#!/usr/bin/env python3
"""Check band navigation against a running isolated QA profile."""
import argparse
import json
import time
import urllib.request


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

    def demod():
        return request('/module/Radio/command', json.dumps({'cmd': 'get_demod', 'args': ''}))

    catalog = json.loads(request('/proc/display/band-catalog'))
    assert {key: len(value) for key, value in catalog.items()} == {'HAM': 16, 'Broadcast': 17, 'Utility': 16}
    before = state()
    assert before['center_tuning'] == args.center_tuning
    results = []
    layouts = []
    mode = demod()
    for category in ['Utility', 'Broadcast', 'HAM']:
        before = state()
        request('/proc/display/band-category', category)
        assert request('/proc/display/band-category') == category
        assert state() == before, 'Changing categories must not retune or change tuning mode'
        assert demod() == mode
        time.sleep(.1)
        layout = json.loads(request('/proc/display/band-layout'))
        assert layout['category'] == category
        assert len(layout['buttons']) == len(catalog[category])
        for i, button in enumerate(layout['buttons']):
            assert button['x'] >= layout['selector_x'] + layout['selector_width']
            assert button['x'] + button['width'] <= layout['window_width'] - 8 + .1
            assert button['y'] + button['height'] <= layout['bottom'] + .1
            if not layout['below']:
                assert button['x'] + button['width'] < layout['meter_x']
            for other in layout['buttons'][:i]:
                assert (button['y'] >= other['y'] + other['height'] or
                        button['x'] >= other['x'] + other['width'])
        layouts.append(layout)
        for band in catalog[category]:
            request('/vfo/set_offset?name=Radio&offset=15000')
            request('/proc/display/band', band['label'])
            actual = state()
            center = band['lower'] + (band['upper'] - band['lower']) // 2
            assert actual['frequency'] == actual['spectrum_center'] == center, (category, band, actual)
            assert actual['view_offset'] == actual['vfo_offset'] == 0
            assert actual['center_tuning'] == actual['single_click_vfo'] == args.center_tuning
            assert request('/proc/display/band') == band['label']
            assert demod() == mode
            results.append({'category': category, 'band': band['label'], 'center': center})
    before = state()
    request('/proc/display/band-category', 'invalid')
    request('/proc/display/band', 'invalid')
    assert state() == before and request('/proc/display/band-category') == 'HAM'
    print(json.dumps({'passed': len(results), 'center_tuning': args.center_tuning,
                      'results': results, 'layouts': layouts}))


if __name__ == '__main__':
    main()
