#!/usr/bin/env python3
"""wav_compare.py A.wav [B.wav] [--window-ms N]

Summarise, and optionally compare, the audio Dolphin dumps with
`-C Dolphin.DSP.DumpAudio=True` (User/Dump/Audio/*_dspdump.wav). Used to check an output
driver change without listening: whether anything came out at all, how loud, whether it
clips, whether it drops to silence in the middle, and how two runs of the same input script
differ.

Reports per file: format, duration, peak and RMS in dBFS, the fraction of windows that are
silent, and the longest unbroken silence. With two files it also prints the difference in
those figures, and, when the two have the same length, a sample-by-sample correlation --
which is the useful number when the same script was run through two different drivers.

Standard library only (the wave and audioop modules), so it runs anywhere Python does."""
import sys, wave, math, struct, array


def read_wav(path):
    """Dolphin writes the WAV header when it opens the file and only corrects the length on a
    clean shutdown. An unattended run ends with a kill, so the header's frame count is a
    placeholder far larger than the data. Trust the bytes actually present, not the header."""
    with wave.open(path, "rb") as w:
        ch, width, rate = w.getnchannels(), w.getsampwidth(), w.getframerate()
        claimed = w.getnframes()
        raw = w.readframes(claimed)
    if width != 2:
        sys.exit(f"{path}: expected 16-bit samples, got {width * 8}-bit")
    a = array.array("h")
    a.frombytes(raw[:len(raw) - len(raw) % (2 * ch)])
    if sys.byteorder == "big":
        a.byteswap()
    frames = len(a) // ch
    return {"path": path, "ch": ch, "rate": rate, "frames": frames,
            "truncated": frames < claimed, "samples": a}


def dbfs(x):
    return -math.inf if x <= 0 else 20.0 * math.log10(x / 32768.0)


def stats(w, window_ms):
    s = w["samples"]
    peak = max((abs(v) for v in s), default=0)
    total = sum(float(v) * v for v in s)
    rms = math.sqrt(total / len(s)) if s else 0.0

    step = max(1, (w["rate"] * w["ch"] * window_ms) // 1000)
    silent = runs = longest = 0
    for i in range(0, len(s), step):
        chunk = s[i:i + step]
        loud = max((abs(v) for v in chunk), default=0)
        if loud < 64:                      # ~ -54 dBFS, below anything audible here
            silent += 1
            runs += 1
            longest = max(longest, runs)
        else:
            runs = 0
    windows = max(1, (len(s) + step - 1) // step)
    return {
        "seconds": w["frames"] / float(w["rate"]) if w["rate"] else 0.0,
        "peak_db": dbfs(peak), "rms_db": dbfs(rms),
        "clipped": sum(1 for v in s if abs(v) >= 32767),
        "silent_frac": silent / windows,
        "longest_silence_ms": longest * window_ms,
    }


def correlate(a, b):
    """Pearson correlation over the overlapping part; 1.0 means identical waveforms."""
    n = min(len(a), len(b))
    if n == 0:
        return float("nan")
    sa = sb = saa = sbb = sab = 0.0
    for i in range(n):
        x, y = a[i], b[i]
        sa += x; sb += y; saa += x * x; sbb += y * y; sab += x * y
    da = saa - sa * sa / n
    db = sbb - sb * sb / n
    if da <= 0 or db <= 0:
        return float("nan")
    return (sab - sa * sb / n) / math.sqrt(da * db)


def show(name, w, st):
    print(f"{name}: {w['ch']}ch {w['rate']}Hz  {st['seconds']:.1f}s")
    print(f"  peak {st['peak_db']:.1f} dBFS   rms {st['rms_db']:.1f} dBFS   clipped {st['clipped']}")
    print(f"  silent windows {st['silent_frac'] * 100:.0f}%   longest silence {st['longest_silence_ms']} ms")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    window = int(sys.argv[sys.argv.index("--window-ms") + 1]) if "--window-ms" in sys.argv else 100
    if not args:
        sys.exit(__doc__)

    a = read_wav(args[0]); sa = stats(a, window)
    show("A " + args[0], a, sa)
    if len(args) < 2:
        return

    b = read_wav(args[1]); sb = stats(b, window)
    print()
    show("B " + args[1], b, sb)
    print()
    print(f"delta: rms {sb['rms_db'] - sa['rms_db']:+.1f} dB   peak "
          f"{sb['peak_db'] - sa['peak_db']:+.1f} dB   length "
          f"{sb['seconds'] - sa['seconds']:+.1f}s")
    if a["rate"] == b["rate"] and a["ch"] == b["ch"]:
        print(f"correlation over the overlap: {correlate(a['samples'], b['samples']):.4f}")
        print("  (only meaningful for two dumps that are sample-aligned. Two separate"
              " emulator runs never are, so expect ~0 there and judge them by the levels"
              " and the silence structure instead.)")
    else:
        print("different formats, not correlating")


if __name__ == "__main__":
    main()
