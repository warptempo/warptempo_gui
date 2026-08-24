#!/usr/bin/env python3
"""Write the spike's bundled audition WAV: 44100 Hz, stereo, 16-bit PCM.

Deliberately NOT a product format exercise -- the spike carries its own minimal
reader (android/spike/src/spike_wav.h) precisely so src/audio_io stays untouched.

The two channels are given DIFFERENT pitches (L 440 Hz, R 660 Hz) so the ear
alone settles channel identity and interleave order on the device; a raised-
cosine envelope keeps the ends click-free, and a 3 s length is long enough to
hear the end arrive (which is what proves the callback's AAUDIO_CALLBACK_RESULT_STOP
path, not just that sound came out).

Byte-identical on every run: no randomness, no timestamps.
"""

import math
import struct
import sys

RATE = 44100
SECONDS = 3.0
LEFT_HZ = 440.0
RIGHT_HZ = 660.0
AMPLITUDE = 0.35
FADE_SECONDS = 0.02


def main(path: str) -> int:
    n = int(RATE * SECONDS)
    fade = max(1, int(RATE * FADE_SECONDS))
    frames = bytearray()
    for i in range(n):
        env = 1.0
        if i < fade:
            env = 0.5 - 0.5 * math.cos(math.pi * i / fade)
        elif i >= n - fade:
            j = n - 1 - i
            env = 0.5 - 0.5 * math.cos(math.pi * j / fade)
        t = i / RATE
        left = AMPLITUDE * env * math.sin(2.0 * math.pi * LEFT_HZ * t)
        right = AMPLITUDE * env * math.sin(2.0 * math.pi * RIGHT_HZ * t)
        frames += struct.pack("<hh",
                              int(round(left * 32767.0)),
                              int(round(right * 32767.0)))

    data = bytes(frames)
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16)
    header += b"data" + struct.pack("<I", len(data))
    with open(path, "wb") as f:
        f.write(header)
        f.write(data)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr.write("usage: gen_wav.py <out.wav>\n")
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
