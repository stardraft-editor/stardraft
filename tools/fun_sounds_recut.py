"""Cut the edit feedback takes (docs/dev/notes/edit-feedback.md) again from the Freesound HQ previews, with their natural decay.

Usage: python -I recut.py <folder with <id>.wav (mono 44.1 kHz)> <out folder>
Each take: from 4 ms before the rise to where the envelope has died to 2 % of its peak (or the next hit), at most
MAXLEN s; 2 ms fade-in, 60 ms fade-out; mono 11,025 Hz, 64 levels, 8-bit.
"""
import sys, wave
from pathlib import Path
import numpy as np
from scipy.signal import resample_poly, butter, sosfilt

src, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
SR = 44100

KINDS = {  # kind: (sources, max length s, semitones, darker)
    "splash": ([763619], 0.45, 0, False),
    "gloop": ([207142], 0.40, 0, False),
    "brush": ([543508], 0.32, 0, False),
    "crunch": ([512243], 0.40, 0, False),
    "rock": ([336023], 0.32, 0, False),
    "clank": ([558254], 0.45, 0, False),
    "thunk": ([215162, 449955, 342535], 0.40, -2, True),
    "plop": ([336495, 653910, 536766], 0.30, 0, False),
    "poof": ([208111, 714257, 471204], 0.45, 0, False),
    "void": ([381853, 243400], 0.50, -5, True),
}


def load(i):
    with wave.open(str(src / f"{i}.wav")) as w:
        return np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float64) / 32768


def envelope(x):
    win = int(0.005 * SR)
    return np.sqrt(np.convolve(x * x, np.ones(win) / win, mode="same"))


def moments(x, n, maxlen):
    """The n loudest hits: (start, end) each, apart from each other."""
    env = envelope(x)
    win = int(0.03 * SR)
    slow = np.sqrt(np.convolve(x * x, np.ones(win) / win, mode="same"))
    taken, used = [], np.zeros(len(x), bool)
    order = np.argsort(env)[::-1]
    for p in order:
        if len(taken) >= n or env[p] < env.max() * 0.15:
            break
        if used[p]:
            continue
        peak = env[p]
        s = p
        while s > 0 and env[s - 1] > peak * 0.1 and p - s < int(0.08 * SR):
            s -= 1
        s = max(0, s - int(0.004 * SR))
        # the decay on a slower envelope (a click's spike is no measure of the body after it): to 1.5 % of its peak,
        # at least MINLEN (or the recording's end)
        e = p
        limit = min(len(x), s + int(maxlen * SR))
        minEnd = min(limit, s + int(min(0.25, maxlen) * SR))
        speak = slow[max(0, p - int(0.03 * SR)):p + int(0.03 * SR)].max()
        while e < limit and (e < minEnd or slow[e] > speak * 0.015):
            # the next hit: a rise back to more than 60 % of this one, well after this one began
            if e - p > int(0.15 * SR) and env[e] > peak * 0.6 and env[e] > env[e - int(0.01 * SR)] * 1.5:
                break
            e += 1
        if e - s < int(0.08 * SR) or used[s:e].any():
            used[max(0, p - 200):p + 200] = True
            continue
        used[s:e] = True
        taken.append((s, e))
    return taken


def shift(x, semis):
    if not semis:
        return x
    r = 2 ** (semis / 12)  # (faster: shorter and higher)
    idx = np.arange(0, len(x) - 1, r)
    return np.interp(idx, np.arange(len(x)), x)


def lowpass(x, hz):
    return sosfilt(butter(2, hz, fs=SR, output="sos"), x)


def finish(x):
    x = x / max(1e-9, np.abs(x).max()) * 0.9
    fi, fo = int(0.002 * SR), min(len(x) // 2, int(0.06 * SR))
    x[:fi] *= np.linspace(0, 1, fi)
    x[-fo:] *= np.linspace(1, 0, fo) ** 2
    y = resample_poly(x, 1, 4)
    y = np.round(y * 31) / 31  # 64 levels
    return np.clip(np.round(y * 127 + 128), 0, 255).astype(np.uint8)


for kind, (ids, maxlen, semis, dark) in KINDS.items():
    cuts = []
    per = max(1, -(-6 // len(ids)))
    for i in ids:
        x = load(i)
        for s, e in moments(x, per, maxlen):
            cuts.append(x[s:e])
    cuts = cuts[:6]
    base = list(cuts)
    k = 0
    while len(cuts) < 6:  # variations: 2 or 3 semitones up or down, or darker
        b = base[k % len(base)]
        v = [lambda y: shift(y, 2), lambda y: shift(y, -3), lambda y: lowpass(y, 2500), lambda y: shift(y, -2),
             lambda y: shift(y, 3)][k % 5]
        cuts.append(v(b))
        k += 1
    for n, c in enumerate(cuts, 1):
        c = shift(c, semis)
        if dark:
            c = lowpass(c, 1800)
        data = finish(c)
        with wave.open(str(out / f"{kind}{n}.wav"), "wb") as w:
            w.setnchannels(1), w.setsampwidth(1), w.setframerate(11025), w.writeframes(data.tobytes())
        print(f"{kind}{n}: {len(data) / 11025 * 1000:.0f} ms")
