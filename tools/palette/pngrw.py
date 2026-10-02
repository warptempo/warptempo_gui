#!/usr/bin/env python3
# tools/palette/pngrw.py — PNG read and write, numpy + zlib only (no PIL, no ImageMagick).
#   read_rgb(path) -> HxWx3 uint8: an 8-bit, non-interlaced greyscale, grey + alpha, RGB or RGBA PNG (every PNG the
#       tool reads is a tablet screencap or its own output); the five row filters undone, alpha dropped (not composited),
#       grey copied to the three channels, the pixel bytes untouched: no colour profile is applied.
#   write_png(path, arr, extra_chunks=()) -> an 8-bit RGB PNG whose chunk list is IHDR, [extra chunks verbatim, in
#       order], IDAT, IEND and nothing else (filter 0 on every row, zlib level 9).
#   chunks(path), chunk_from_png(path, name) -> the chunk list / the raw payload of one chunk.
import struct, zlib
import numpy as np

def chunks(path):
    f = open(path, 'rb').read(); p = 8; out = []
    if f[:8] != b'\x89PNG\r\n\x1a\n': raise ValueError(f'{path}: not a PNG')
    while p < len(f):
        ln, = struct.unpack('>I', f[p:p + 4]); name = f[p + 4:p + 8]; out.append((name, f[p + 8:p + 8 + ln])); p += 12 + ln
    return out

def chunk_from_png(path, name):
    for n, d in chunks(path):
        if n == name: return d
    raise KeyError(name)

_CHANNELS = {0: 1, 4: 2, 2: 3, 6: 4}       # colour type -> samples per pixel (grey, grey + alpha, RGB, RGBA)

def _unfilter(raw, h, w, bpp):
    """Undo the per-row filters (0 None, 1 Sub, 2 Up, 3 Average, 4 Paeth) -> h x (w * bpp) uint8."""
    stride = w * bpp; rows = np.frombuffer(raw, np.uint8).reshape(h, stride + 1)
    out = np.zeros((h, stride), np.uint8); prior = np.zeros(stride, np.uint8)
    for y in range(h):
        ft = rows[y, 0]; line = rows[y, 1:]
        if ft == 0: cur = line.copy()
        elif ft == 1:          # Sub: a running sum per byte position modulo bpp
            cur = np.cumsum(line.reshape(w, bpp), axis=0, dtype=np.uint8).reshape(stride)
        elif ft == 2: cur = line + prior
        elif ft in (3, 4):     # Average, Paeth: each byte depends on its reconstructed left neighbour
            cur = bytearray(line.tobytes()); up = prior.tobytes()
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0; b = up[i]
                if ft == 3: pred = (a + b) >> 1
                else:
                    c = up[i - bpp] if i >= bpp else 0
                    p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                cur[i] = (cur[i] + pred) & 255
            cur = np.frombuffer(bytes(cur), np.uint8)
        else: raise ValueError(f'PNG filter type {ft}')
        out[y] = cur; prior = out[y]
    return out

def read_rgb(path):
    ch = chunks(path); ihdr = next(d for n, d in ch if n == b'IHDR')
    w, h, depth, ctype, comp, filt, interlace = struct.unpack('>IIBBBBB', ihdr)
    if depth != 8 or ctype not in _CHANNELS or interlace != 0 or comp != 0 or filt != 0:
        raise ValueError(f'{path}: read_rgb takes 8-bit non-interlaced grey / RGB / RGBA PNGs '
                         f'(depth {depth}, colour type {ctype}, interlace {interlace})')
    bpp = _CHANNELS[ctype]
    px = _unfilter(zlib.decompress(b''.join(d for n, d in ch if n == b'IDAT')), h, w, bpp).reshape(h, w, bpp)
    rgb = px[..., :3] if bpp >= 3 else np.repeat(px[..., :1], 3, axis=2)
    return np.ascontiguousarray(rgb)

def _chunk(name, data):
    c = struct.pack('>I', len(data)) + name + data
    return c + struct.pack('>I', zlib.crc32(name + data) & 0xffffffff)

def write_png(path, arr, extra_chunks=()):
    arr = np.ascontiguousarray(arr, dtype=np.uint8); h, w, ch = arr.shape; assert ch == 3
    rows = np.concatenate([np.zeros((h, 1), np.uint8), arr.reshape(h, w * 3)], axis=1)  # filter 0 per row
    idat = zlib.compress(rows.tobytes(), 9)
    out = b'\x89PNG\r\n\x1a\n' + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    for name, data in extra_chunks: out += _chunk(name, data)
    out += _chunk(b'IDAT', idat) + _chunk(b'IEND', b'')
    open(path, 'wb').write(out)
