"""Offline equivalent-time decode with local (per-frame) phase fits."""
import pickle, sys, math, os
caps = pickle.load(open(sys.argv[1], 'rb')); W = int(sys.argv[2]) if len(sys.argv) > 2 else 8
EXPECT = [(0xA53C96 << 8 >> (31 - i)) & 1 for i in range(32)] + [(0x5A3C69 << 8 >> (31 - i)) & 1 for i in range(32)]
BINS = 2048
def fit(xs, ys):
    m = len(xs); mx = sum(xs) / m; my = sum(ys) / m
    p = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs); return p, my - p * mx
summary = {}
for k, ((rate, n, cycles), raw) in enumerate(caps):
    lr = [(b >> 2) & 1 for b in raw]
    falls = [i for i in range(1, n) if lr[i - 1] and not lr[i]]
    if len(falls) < 2 * W + 2: continue
    counts = [falls[j + 1] - falls[j] for j in range(len(falls) - 1)]
    acc = [[0, 0, 0] for _ in range(BINS)]
    for j in range(len(falls) - 1):
        lo, hi = max(0, j - W), min(len(falls), j + W + 1)
        P, a = fit(list(range(lo, hi)), falls[lo:hi])
        for i in range(falls[j], falls[j + 1]):
            ph = ((i - a) / P - j) % 1.0; q = acc[int(ph * BINS) % BINS]
            q[0] += 1; q[1] += raw[i] & 1; q[2] += (raw[i] >> 1) & 1
    clk = [q[1] / q[0] if q[0] else 0.5 for q in acc]; din = [q[2] / q[0] if q[0] else 0.5 for q in acc]
    mean = sum(clk) / BINS; ac = [c - mean for c in clk]; tot = sum(x * x for x in ac)
    def harm(h):
        c = sum(ac[b] * math.cos(2 * math.pi * h * b / BINS) for b in range(BINS)); s = sum(ac[b] * math.sin(2 * math.pi * h * b / BINS) for b in range(BINS))
        return c, s
    spec = {h: sum(v * v for v in harm(h)) for h in range(1, 200)}
    peak = max(spec, key=spec.get); c64, s64 = harm(64)
    # fundamental-only share of AC power (a square wave puts ~81% in its fundamental)
    share = 2 * spec[64] / BINS / tot if tot else 0
    phi = math.atan2(s64, c64)  # clk ~ cos(2pi*64*b/BINS - phi): rising (upward zero crossing) at phase phi - pi/2
    bits = []
    for t in range(64):
        rise = ((phi - math.pi / 2) / (2 * math.pi) + t) / 64  # in frames
        v = [din[int(((rise + d / 64.0) % 1.0) * BINS)] for d in (0.15, 0.25, 0.35)]  # just after the rising edge
        bits.append(1 if sum(v) / 3 > 0.5 else 0)
    best = max(range(64), key=lambda o: sum(bits[(o + i) % 64] == EXPECT[i] for i in range(64)))
    match = sum(bits[(best + i) % 64] == EXPECT[i] for i in range(64))
    contrast = sum(abs(d - 0.5) * 2 for d in din) / BINS
    summary.setdefault(rate, []).append(match)
    print('rate=%6d cap=%2d frames=%3d samples/frame %d..%d  bclk_peak_harmonic=%d fundamental_share=%.2f din_contrast=%.2f bits_matching=%d/64 offset=%d'
          % (rate, k, len(falls) - 1, min(counts), max(counts), peak, share, contrast, match, best))
for r, v in sorted(summary.items()): print('rate=%d captures=%d fully_matching=%d' % (r, len(v), sum(1 for x in v if x == 64)))
