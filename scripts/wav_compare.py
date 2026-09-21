#!/usr/bin/env python3
"""wav_compare.py A.wav [B.wav] [--window-ms N]

Summarise, and optionally compare, the audio Dolphin dumps with
`-C Dolphin.DSP.DumpAudio=True` (User/Dump/Audio/*_dspdump.wav). Used to check an output
driver change without listening: whether anything came out at all, how loud, whether it
clips, whether it drops to silence in the middle, and how two runs of the same input script
differ.

Reports per file: format, duration, peak and RMS in dBFS, the fraction of windows that are
silent, the longest unbroken silence, and (stereo) how often the right channel equals the
left at a shift of -1, 0 and +1 frames: dual-mono content should peak at 0, and a peak at
+1 or -1 means an output stage put the channels one sample apart (case study 9). With two files it also prints the difference in
those figures, and, when the two have the same length, a sample-by-sample correlation --
which is the useful number when the same script was run through two different drivers.

Standard library only (the wave and audioop modules), so it runs anywhere Python does."""
import sys, wave, math, struct, array


def decode_if_needed(path):
    """Baselines are kept as FLAC (baselines/media/<id>/audio.flac). The wave module
    reads WAV only, so anything else is decoded with ffmpeg into a temporary WAV first;
    the caller gets the path to read and a flag saying whether to delete it afterwards."""
    if path.lower().endswith(".wav"):
        return path, False
    import shutil, subprocess, tempfile
    ff = shutil.which("ffmpeg") or r"C:\Program Files (x86)fmpeginfmpeg.exe"
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False); tmp.close()
    r = subprocess.run([ff, "-y", "-loglevel", "error", "-i", path, "-c:a", "pcm_s16le", tmp.name])
    if r.returncode != 0:
        sys.exit("could not decode %s with ffmpeg" % path)
    return tmp.name, True


def read_wav(path):
    path, temp = decode_if_needed(path)
    try:
        return _read_wav(path)
    finally:
        if temp:
            import os
            os.unlink(path)


def _read_wav(path):
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

    # Short runs of EXACT zeros inside the sound: between 1 ms and 1 s, on the first
    # channel. Music has none; a stream that momentarily runs dry does. The Wii's AESND
    # mixes in 96-frame (2 ms) blocks, so a voice that misses a buffer hand-over leaves
    # a 2 ms hole, and hundreds of those per minute is crackle that the window statistics
    # above cannot see (their windows are 100 ms and a hole is -inf only for 2 ms).
    ch, rate = w["ch"], max(1, w["rate"])
    lo, hi = rate // 1000, rate
    gaps = gap_frames = run = 0
    first = last = -1
    for i in range(0, len(s), ch):
        if s[i] != 0:
            last = i
            if first < 0:
                first = i
    i = first if first >= 0 else 0
    while 0 <= first and i <= last:
        if s[i] == 0:
            run += 1
        else:
            if lo <= run < hi:
                gaps += 1
                gap_frames += run
            run = 0
        i += ch
    return {
        "gaps": gaps, "gap_ms": gap_frames * 1000 // rate,
        "seconds": w["frames"] / float(w["rate"]) if w["rate"] else 0.0,
        "peak_db": dbfs(peak), "rms_db": dbfs(rms),
        "clipped": sum(1 for v in s if abs(v) >= 32767),
        "silent_frac": silent / windows,
        "longest_silence_ms": longest * window_ms,
    }


def stereo_skew(w, floor=64):
    """For a stereo dump: how often the right channel equals the left channel shifted by
    -1, 0 or +1 frame, over the frames where either channel is above the silence floor.
    Most PSX audio here is dual-mono (CD-XA speech, many SFX), so on a healthy path the
    shift-0 fraction is high and the others are low. A path that interleaves L/R wrongly
    (the SDL ring's writer used to start at index 1, so every frame straddled two source
    frames) shows up as the +1 (right lags) or -1 (right leads) fraction winning instead.
    Returns {shift: fraction} or None for a non-stereo file."""
    if w["ch"] != 2:
        return None
    s = w["samples"]
    n = w["frames"]
    hits = {-1: 0, 0: 0, 1: 0}
    seen = 0
    for i in range(1, n - 1):
        l = s[2 * i]
        r = s[2 * i + 1]
        if abs(l) < floor and abs(r) < floor:
            continue
        seen += 1
        if r == l:
            hits[0] += 1
        if r == s[2 * (i - 1)]:          # R[n] == L[n-1]: right lags by one frame
            hits[1] += 1
        if r == s[2 * (i + 1)]:          # R[n] == L[n+1]: right leads by one frame
            hits[-1] += 1
    if seen == 0:
        return None
    return {k: v / seen for k, v in hits.items()}


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
    sk = stereo_skew(w)
    if sk is not None:
        best = max(sk, key=sk.get)
        # Genuinely stereo content scores low at every shift; only call it a skew when a
        # shifted match clearly beats the unshifted one on a good share of the frames.
        skewed = best != 0 and sk[best] > 0.2 and sk[best] > 2 * sk[0]
        flag = "   <-- CHANNEL SKEW: the output stage interleaves L/R wrongly" if skewed else ""
        print(f"  R==L at shift -1/0/+1: {sk[-1]:.2f} / {sk[0]:.2f} / {sk[1]:.2f}{flag}")
    print(f"  short zero gaps (1 ms..1 s, inside the sound) {st['gaps']}   totalling {st['gap_ms']} ms")


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
