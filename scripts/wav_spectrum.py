#!/usr/bin/env python3
"""wav_spectrum.py A.wav [B.wav ...] [--start S] [--dur D] [--band LO HI]

Band energy of Dolphin's audio dumps, for judging an output-stage resampler by
measurement: what a 44100 -> 48000 conversion does to the top of the spectrum.

Zero-order hold, linear and cubic interpolation of the same 44100 Hz material differ in
two ways that show up here and nowhere in wav_compare.py's level figures:
  * roll-off: hold attenuates 16-20 kHz content by a fraction of a dB, linear by a little
    more (a triangle's response), cubic least;
  * images: the copies of the spectrum that the conversion leaves around multiples of the
    input rate fold into 20-24 kHz at this ratio, and hold leaves them 20-40 dB louder than
    the interpolating modes.
So for each file this prints the energy in a few fixed bands relative to the total, from
one FFT over the same time window, so files from identical input scripts line up. With
several files the differences against the first are printed as well.

Needs numpy (present on this machine); the wave module does the reading."""
import os
import sys, wave, math

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliutil import need, parse_opts, req_file, req_float  # noqa: E402
import numpy as np

BANDS = [(20, 200), (200, 2000), (2000, 8000), (8000, 16000), (16000, 20000), (20000, 24000)]


def read(path):
    # FLAC baselines (baselines/media/<id>/audio.flac) are decoded through wav_compare's helper
    import os, sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from wav_compare import decode_if_needed
    path, temp = decode_if_needed(path)
    try:
        return _read(path)
    finally:
        if temp:
            os.unlink(path)


def _read(path):
    with wave.open(path, "rb") as w:
        ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(w.getnframes())
    if width != 2:
        sys.exit("%s: only 16-bit PCM handled" % path)
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x / 32768.0, rate


def band_db(x, rate, bands):
    n = len(x)
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(x * win)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    total = spec.sum() or 1e-30
    out = []
    for lo, hi in bands:
        m = (freqs >= lo) & (freqs < hi)
        e = spec[m].sum()
        out.append(10 * math.log10(e / total) if e > 0 else -200.0)
    return out


def main(argv):
    need(['prog'] + argv, 2, "wav_spectrum.py FILE [FILE...] [--start S] [--dur S] [--band LO HI]")
    # --band takes TWO values, unlike every other flag: lift it out first.
    band, rest = None, []
    i = 0
    while i < len(argv):
        if argv[i] == '--band':
            if i + 2 >= len(argv):
                sys.exit("--band: needs LO HI")
            band = (argv[i + 1], argv[i + 2])
            i += 3
        else:
            rest.append(argv[i])
            i += 1
    files, opts = parse_opts(['prog'] + rest, valued=('--start', '--dur'))
    start = req_float(opts.get('--start', 10.0), '--start')
    dur = req_float(opts.get('--dur', 20.0), '--dur')
    bands = BANDS
    if band is not None:
        try:
            lo, hi = float(band[0]), float(band[1])
        except ValueError:
            sys.exit(f"--band: need LO HI numbers, got {' '.join(band)!r}")
        bands = [(lo, hi)]
    if not files:
        sys.exit(__doc__)
    files = [req_file(f, 'wav') for f in files]
    rows = []
    for f in files:
        x, rate = read(f)
        a, b = int(start * rate), int((start + dur) * rate)
        if b > len(x):
            b = len(x)
        if a >= len(x) or not len(x):
            sys.exit(f"{f}: --start {start}s is past its {len(x) / rate:.1f}s duration")
        seg = x[a:b]
        rms = 20 * math.log10(math.sqrt((seg ** 2).mean()) + 1e-12)
        rows.append((f, rate, len(x) / rate, rms, band_db(seg, rate, bands)))
    hdr = "%-40s %6s %7s %8s " % ("file", "rate", "dur_s", "rms_dB") + " ".join("%9s" % ("%d-%dk" % (lo / 1000, hi / 1000)) for lo, hi in bands)
    print(hdr)
    for f, rate, d, rms, bd in rows:
        print("%-40s %6d %7.1f %8.1f " % (f[-40:], rate, d, rms) + " ".join("%9.1f" % v for v in bd))
    if len(rows) > 1:
        print("\ndifference against the first file (dB; positive = more energy in that band):")
        for f, rate, d, rms, bd in rows[1:]:
            print("%-40s %6s %7s %8.1f " % (f[-40:], "", "", rms - rows[0][3]) + " ".join("%9.1f" % (v - r) for v, r in zip(bd, rows[0][4])))


if __name__ == "__main__":
    main(sys.argv[1:])
