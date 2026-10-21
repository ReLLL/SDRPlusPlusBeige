#!/usr/bin/env python3
"""Exercise a connected RTL-SDR using an isolated QA profile with HTTP enabled."""
import argparse
import json
import time
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()

    def request(path, body=None):
        data = body.encode() if body is not None else None
        with urllib.request.urlopen(urllib.request.Request(args.url + path, data=data), timeout=10) as response:
            return response.read().decode()

    def device():
        return json.loads(request('/proc/source/RTL-SDR/device'))

    request('/sdr/stop')
    initial = device()
    assert initial['count'] > 0 and initial['gains'] > 0 and initial['selected'], initial
    for cycle in range(10):
        before = device()
        request('/proc/source/RTL-SDR/device', 'refresh')
        refreshed = device()
        assert refreshed['refreshes'] == before['refreshes'] + 1, refreshed
        assert refreshed['selected'] == initial['selected'] and refreshed['gains'] > 0, refreshed
        request('/sdr/start')
        assert device()['running'], device()
        # A refresh request must not replace a handle owned by the read thread.
        request('/proc/source/RTL-SDR/device', 'refresh')
        active = device()
        assert active['running'] and active['refreshes'] == refreshed['refreshes'], active
        time.sleep(0.15)
        request('/sdr/stop')
        assert not device()['running'], device()
    print(json.dumps({'passed_cycles': 10, 'final': device()}))


if __name__ == '__main__':
    main()
