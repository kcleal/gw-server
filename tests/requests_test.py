#!/usr/bin/env python3
"""Black-box contract test. Start gw-server with GW's test.fa/test.bam first."""
import argparse
import concurrent.futures
import json
import struct
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8766')
    args = parser.parse_args()

    def request(path, body=None, expected=200):
        req = urllib.request.Request(args.url + path, data=None if body is None else json.dumps(body).encode(),
                                     headers={} if body is None else {'Content-Type': 'application/json'})
        try:
            response = urllib.request.urlopen(req, timeout=60)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            data = response.read()
            assert response.status == expected, (path, body, response.status, data[:500])
            return data, response.headers

    def state():
        return json.loads(request('/api/state')[0])

    def action(kind, value='', **kwargs):
        return json.loads(request('/api/action', dict(revision=state()['revision'], type=kind, value=value, **kwargs))[0])

    def frame(s):
        data, headers = request('/api/frame?revision=' + str(s['revision']))
        assert data.startswith(b'\x89PNG\r\n\x1a\n')
        assert struct.unpack('>II', data[16:24]) == (s['width'], s['height'])
        assert int(headers['X-GW-Revision']) == s['revision']
        return data

    assert json.loads(request('/health')[0])['status'] == 'ok'
    initial = state()
    original = frame(initial)
    assert original == frame(initial), 'Unchanged frames should use the PNG cache'
    s = action('navigate', 'chr1:9900-10300')
    assert s['region'] == 'chr1:9900-10300'
    frame(s)
    request('/api/frame?revision=' + str(initial['revision']), expected=409)
    request('/api/action', {'revision': initial['revision'], 'type': 'navigate', 'value': 'chr1:1-100'}, 409)
    for region in ['missing:1-100', 'chr1:200-100', 'chr1:0-10', 'chr1:1-999999', 'garbage']:
        before = state()
        request('/api/action', {'revision': before['revision'], 'type': 'navigate', 'value': region}, 400)
        assert state() == before, 'Rejected navigation mutated the view'
    for command in ['load /tmp/file.bam', 'save /tmp/leak', 'quit', 'rm bam0']:
        before = state()
        request('/api/action', {'revision': before['revision'], 'type': 'command', 'value': command}, 400)
        assert state() == before
    request('/api/action', {'revision': state()['revision'], 'type': 'resize', 'width': 100000, 'height': 100000}, 400)
    theme_frames = []
    for command in ['theme igv', 'theme slate', 'sort strand', 'sort none', 'link sv', 'link none', 'ylim 40', 'cov 200', 'mm', 'mm', 'ins', 'ins', 'soft-clips', 'soft-clips', 'mods', 'mods']:
        result = action('command', command)
        if command.startswith('theme '): assert result['theme'] == command.split()[1]
        image = frame(result)
        if command.startswith('theme '): theme_frames.append(image)
    assert theme_frames[0] != theme_frames[1], 'Theme command did not change rendered pixels'
    s = action('resize', width=720, height=360)
    frame(s)
    # Physical pixels and logical drawing scale must be distinct on HiDPI screens.
    s = action('resize', width=1440, height=720, pixel_ratio=2)
    assert s['pixel_ratio'] == 2
    retina = frame(s)
    normal = frame(action('resize', width=1440, height=720, pixel_ratio=1))
    assert retina != normal, 'Pixel ratio did not scale server drawing'
    frame(action('resize', width=1279, height=719, pixel_ratio=1.5))
    s = action('settings', 'font_size 14\nylim 25\n')
    assert 'ylim 25' in s['settings']
    frame(s)
    for invalid in ['font_size 9999', 'filename 1', 'ylim nan', 'ylim 25 broken']:
        before = state()
        request('/api/action', {'revision':before['revision'],'type':'settings','value':invalid},400)
        assert state() == before
    s = action('resize', width=720, height=360)
    # Exactly one competing mutation with the same expected revision may win.
    body = {'revision': s['revision'], 'type': 'navigate', 'value': 'chr1:10000-10400'}
    def competing(_):
        try:
            with urllib.request.urlopen(urllib.request.Request(args.url + '/api/action', data=json.dumps(body).encode(), headers={'Content-Type': 'application/json'}), timeout=60) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        assert sorted(pool.map(competing, range(2))) == [200, 409]
    frame(state())
    request('/api/action', {}, 400)
    request('/api/action', {'revision': state()['revision'], 'type': 'bogus'}, 400)
    print('PASS: health, navigation, commands, PNGs, cached frames, dimensions, validation and stale/concurrent requests')


if __name__ == '__main__':
    main()
