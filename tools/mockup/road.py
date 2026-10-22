#!/usr/bin/env python3
"""THE TABLET'S COLOR ROAD for captures and mocks (measured on the architect's glass
2026-10-22; the record is tools/mockup/README.md).

THE RULE: THE iCCP TAG SAYS WHAT THE BYTES ARE. His viewer, Fossify Gallery, honors a
PNG's Display-P3 tag: a TAGGED file's bytes are shown as Display-P3 coordinates, an
UNTAGGED file's as sRGB. The app's window is a Display-P3 layer, so with `true_colors=on`
its bytes -- and a screencap's, which carries the Skia Display-P3 iCCP -- are P3; with
`true_colors=off` they are the authored sRGB bytes under the same tag. Hence:
  * a screencap taken with true_colors=on is right AS IT IS (the tag kept);
  * a screencap taken with true_colors=off is right only with its tag STRIPPED -- tagged,
    the Gallery reads its sRGB bytes as P3, oversaturated (the "bright orange" of
    2026-10-22, the "wrong in every region" of 2026-10-21);
  * a color inserted into a TAGGED mock goes through the app's transform (srgb_to_p3,
    src/gui/display_transform.h's math byte for byte); into an UNTAGGED mock as the sRGB
    byte itself. A P3 byte in an untagged file reads pale.

    road.py capture <name> [--device D]   screencap the tablet to /sdcard/Download/<name>.png
                                          (the media scanner told) and ./<name>.png, reading
                                          the app's true_colors key through run-as: on -> the
                                          tag kept (P3 bytes), off -> the tag stripped (sRGB)
    road.py srgb <in.png> <out.png> [--bytes srgb|p3]
                                          an untagged sRGB file from any file (a tagged one
                                          converted P3 -> sRGB, clipped to the sRGB gamut)
    road.py p3 <in.png> <out.png> [--bytes srgb|p3]
                                          a tagged P3 file from any file (an untagged one
                                          converted sRGB -> P3, tagged with the app's profile)
    --bytes names the input's bytes where its tag lies: A RAW true_colors=off SCREENCAP is
    the one such file (sRGB bytes under the device's tag) -- `--bytes srgb`. Without it the
    tag is believed.
    road.py check <png...>                each file's kind (tagged or not) and sample means

As a library: srgb_to_p3(arr) and p3_to_srgb(arr) on uint8 H x W x 3 arrays; TAG the
520-byte Display-P3 profile every tablet capture carries (display_p3.icc beside this file).
Verified 2026-10-22: srgb_to_p3 of a true_colors=off capture equals the true_colors=on
capture of the same screen on 3,302,322 of 3,317,760 pixels, the rest antialiased edges
within one step (the app blends converted colors; a converted blend differs by rounding).
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TAG = open(os.path.join(HERE, 'display_p3.icc'), 'rb').read()
DEVICE_DEFAULT = '192.168.1.87:5555'
APP = 'com.warptempo.gui'
CONFIG = 'files/warptempo_gui/config'
DOWNLOAD = '/sdcard/Download'


# ---- the app's transform (display_transform.h: decode, one matrix, the decision levels)
def _rgb_to_xyz(rx, ry, gx, gy, bx, by, wx, wy):
    p = np.array([[rx / ry, gx / gy, bx / by], [1.0, 1.0, 1.0],
                  [(1 - rx - ry) / ry, (1 - gx - gy) / gy, (1 - bx - by) / by]])
    w = np.array([wx / wy, 1.0, (1 - wx - wy) / wy])
    s = np.linalg.inv(p) @ w
    return p * s[None, :]


_SRGB = _rgb_to_xyz(0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.3290)
_P3 = _rgb_to_xyz(0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290)
SRGB_TO_P3 = np.linalg.inv(_P3) @ _SRGB
P3_TO_SRGB = np.linalg.inv(_SRGB) @ _P3


def _decode(e):
    return np.where(e <= 0.04045, e / 12.92, ((e + 0.055) / 1.055) ** 2.4)


_DECODE = _decode(np.arange(256) / 255.0)
_LEVELS = _decode((np.arange(255) + 0.5) / 255.0)   # byte k | k+1 boundaries


def _transform(arr, m):
    a = np.asarray(arr)
    assert a.dtype == np.uint8 and a.ndim == 3 and a.shape[2] == 3, 'uint8 H x W x 3'
    lin = _DECODE[a.astype(int)] @ m.T
    return np.searchsorted(_LEVELS, lin, side='right').astype(np.uint8)   # == upper_bound


def srgb_to_p3(arr):
    """The app's p3_word_from_srgb on every pixel."""
    return _transform(arr, SRGB_TO_P3)


def p3_to_srgb(arr):
    """The app's srgb_word_from_p3 (clips outside the sRGB gamut, as it does)."""
    return _transform(arr, P3_TO_SRGB)


# ---- files
def load(path):
    im = Image.open(path)
    return np.asarray(im.convert('RGB')), im.info.get('icc_profile')


def save(path, arr, tagged):
    im = Image.fromarray(np.ascontiguousarray(arr), 'RGB')
    if tagged:
        im.save(path, icc_profile=TAG)
    else:
        im.save(path)


def kind(icc):
    return 'tagged (Display-P3 bytes)' if icc else 'untagged (sRGB bytes)'


def _bytes_are_p3(icc, override):
    if override is None:
        return bool(icc)
    assert override in ('srgb', 'p3'), '--bytes srgb|p3'
    return override == 'p3'


def to_srgb_file(src, dst, override=None):
    a, icc = load(src)
    p3 = _bytes_are_p3(icc, override)
    save(dst, p3_to_srgb(a) if p3 else a, tagged=False)
    print('%s: %s bytes -> %s untagged (sRGB)' % (src, 'P3' if p3 else 'sRGB', dst))


def to_p3_file(src, dst, override=None):
    a, icc = load(src)
    p3 = _bytes_are_p3(icc, override)
    save(dst, a if p3 else srgb_to_p3(a), tagged=True)
    print('%s: %s bytes -> %s tagged (P3)' % (src, 'P3' if p3 else 'sRGB', dst))


def check(paths):
    for p in paths:
        a, icc = load(p)
        h, w = a.shape[:2]
        rows = [(int(h * f), int(w * g)) for f, g in ((0.1, 0.5), (0.5, 0.5), (0.95, 0.15))]
        means = ' '.join('%02X%02X%02X' % tuple(a[y:y + 8, x:x + 8].reshape(-1, 3).mean(0).round().astype(int))
                         for y, x in rows)
        print('%-40s %dx%d %-28s samples %s' % (os.path.basename(p), w, h, kind(icc), means))


# ---- the tablet
def adb(device, *args, binary=False):
    r = subprocess.run(['adb', '-s', device, *args], capture_output=True, check=True)
    return r.stdout if binary else r.stdout.decode(errors='replace')


def capture(name, device):
    cfg = adb(device, 'shell', 'run-as', APP, 'cat', CONFIG)
    on = [l for l in cfg.splitlines() if l.startswith('true_colors=')]
    assert on, 'no true_colors line in the tablet config'
    tagged = on[0] == 'true_colors=on'
    raw = adb(device, 'exec-out', 'screencap', '-p', binary=True)
    tmp = name + '.raw.png'
    with open(tmp, 'wb') as f:
        f.write(raw)
    a, icc = load(tmp)
    os.remove(tmp)
    assert icc == TAG, 'the screencap carries another profile than display_p3.icc'
    local = name + '.png'
    save(local, a, tagged=tagged)
    remote = '%s/%s.png' % (DOWNLOAD, name)
    adb(device, 'push', local, remote)
    adb(device, 'shell', 'touch', remote)
    adb(device, 'shell', 'am', 'broadcast', '-a', 'android.intent.action.MEDIA_SCANNER_SCAN_FILE',
        '-d', 'file://' + remote)
    print('%s: %s, %s -> %s and %s' % (name, on[0], kind(tagged and TAG), local, remote))


def main(argv):
    if len(argv) >= 2 and argv[0] == 'capture':
        device = DEVICE_DEFAULT
        if '--device' in argv:
            device = argv[argv.index('--device') + 1]
        capture(argv[1], device)
    elif len(argv) in (3, 5) and argv[0] in ('srgb', 'p3'):
        override = argv[4] if len(argv) == 5 and argv[3] == '--bytes' else None
        if len(argv) == 5 and override is None:
            sys.exit(__doc__)
        (to_srgb_file if argv[0] == 'srgb' else to_p3_file)(argv[1], argv[2], override)
    elif len(argv) >= 2 and argv[0] == 'check':
        check(argv[1:])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
