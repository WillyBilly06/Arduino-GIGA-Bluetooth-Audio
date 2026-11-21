"""DIN-only check of route captures: fold DIN by LRCK frame phase, read 64 bit cells, best fractional shift."""
import pickle, sys, collections
caps = pickle.load(open(sys.argv[1], 'rb')); W = 20; BINS = 1024
EXPECT = [(0xA53C96 << 8 >> (31 - i)) & 1 for i in range(32)] + [(0x5A3C69 << 8 >> (31 - i)) & 1 for i in range(32)]
def fit(xs, ys):
    m = len(xs); mx = sum(xs) / m; my = sum(ys) / m
    p = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs); return p, my - p * mx
res = collections.defaultdict(list)
for (route, rate, n, cycles), raw in caps:
    lr = [(b >> 2) & 1 for b in raw]; falls = [i for i in range(1, n) if lr[i - 1] and not lr[i]]
    if len(falls) < 12: res[(route, rate)].append(None); continue
    acc = [[0, 0] for _ in range(BINS)]
    for j in range(len(falls) - 1):
        lo, hi = max(0, j - W), min(len(falls), j + W + 1); P, a = fit(list(range(lo, hi)), falls[lo:hi])
        for i in range(falls[j], falls[j + 1]):
            q = acc[int((((i - a) / P - j) % 1.0) * BINS) % BINS]; q[0] += 1; q[1] += (raw[i] >> 1) & 1
    din = [q[1] / q[0] if q[0] else 0.5 for q in acc]
    best = 0
    for s in range(64):  # shift in 1/32 bit steps over two bits (Philips delay is ~1 bit)
        d = s / 32.0
        bits = [1 if din[int((((k + d) / 64.0) % 1.0) * BINS)] > 0.5 else 0 for k in range(64)]
        best = max(best, sum(b == e for b, e in zip(bits, EXPECT)))
    res[(route, rate)].append(best)
for k, v in sorted(res.items()): print('route %2d %6d DIN-only exact: %d/%d  %s' % (k[0], k[1], sum(1 for x in v if x == 64), len(v), v))
