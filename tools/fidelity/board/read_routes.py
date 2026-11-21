"""Host side of I2sRoutes: capture, then equivalent-time fold per route (BCLK/DIN/LRCK, MCLK harmonic)."""
import serial, struct, sys, time, math, pickle
port = sys.argv[1] if len(sys.argv) > 1 else 'COM8'
for i in range(60):
    try: s = serial.Serial(port, 115200, timeout=30); break
    except Exception: time.sleep(1)
else: sys.exit('no port')
time.sleep(1); s.reset_input_buffer(); s.write(b'g'); s.flush()
def rd(n):
    b = bytearray()
    while len(b) < n:
        c = s.read(n - len(b))
        if not c: raise SystemExit('timeout')
        b += c
    return bytes(b)
caps = []
while True:
    tag = rd(1); n = struct.unpack('<I', rd(4))[0]; d = rd(n) if n else b''
    if tag == b'S': print(d.decode())
    elif tag == b'K': caps.append((struct.unpack('<4I', d[:16]), d[16:]))
    elif tag == b'Z': break
pickle.dump(caps, open('route_caps.pkl', 'wb'))
EXPECT = [(0xA53C96 << 8 >> (31 - i)) & 1 for i in range(32)] + [(0x5A3C69 << 8 >> (31 - i)) & 1 for i in range(32)]
W = 20
def fit(xs, ys):
    m = len(xs); mx = sum(xs) / m; my = sum(ys) / m
    p = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs); return p, my - p * mx
for (route, rate, n, cycles), raw in caps:
    fs = n / (cycles / 480e6)
    lr = [(b >> 2) & 1 for b in raw]; falls = [i for i in range(1, n) if lr[i - 1] and not lr[i]]
    if len(falls) < 12: print('route=%2d rate=%6d only %d LRCK edges' % (route, rate, len(falls))); continue
    Pg, ag = fit(list(range(len(falls))), falls)
    BINS = 4096 if n >= 16000 else 1024
    acc = [[0, 0, 0, 0] for _ in range(BINS)]
    for j in range(len(falls) - 1):
        lo, hi = max(0, j - W), min(len(falls), j + W + 1); P, a = fit(list(range(lo, hi)), falls[lo:hi])
        for i in range(falls[j], falls[j + 1]):
            q = acc[int((((i - a) / P - j) % 1.0) * BINS) % BINS]; b = raw[i]
            q[0] += 1; q[1] += b & 1; q[2] += (b >> 1) & 1; q[3] += (b >> 3) & 1
    def fold(c): return [q[c] / q[0] if q[0] else 0.5 for q in acc]
    def spectrum(x):
        m = sum(x) / BINS; ac = [v - m for v in x]; tot = sum(v * v for v in ac) or 1e-9
        pw = {}
        for h in list(range(1, 80)) + list(range(240, 272)):
            c = sum(ac[k] * math.cos(2 * math.pi * h * k / BINS) for k in range(0, BINS, 2)); s_ = sum(ac[k] * math.sin(2 * math.pi * h * k / BINS) for k in range(0, BINS, 2))
            pw[h] = (c * c + s_ * s_, math.atan2(s_, c))
        return pw, tot
    clk = fold(1); din = fold(2); mck = fold(3)
    pw, _ = spectrum(clk); peak = max(pw, key=lambda h: pw[h][0]); phi = pw[64][1]
    bits = []
    for t in range(64):
        rise = ((phi - math.pi / 2) / (2 * math.pi) + t) / 64
        v = [din[int(((rise + d / 64.0) % 1.0) * BINS)] for d in (0.15, 0.25, 0.35)]; bits.append(1 if sum(v) / 3 > 0.5 else 0)
    best = max(range(64), key=lambda o: sum(bits[(o + i) % 64] == EXPECT[i] for i in range(64)))
    match = sum(bits[(best + i) % 64] == EXPECT[i] for i in range(64))
    contrast = sum(abs(d - 0.5) * 2 for d in din) / BINS
    mtxt = ''
    if any(q[3] for q in acc):
        mp, _ = spectrum(mck); mpeak = max(mp, key=lambda h: mp[h][0]); mtxt = ' mclk_peak_harmonic=%d (%.3f MHz)' % (mpeak, mpeak * fs / Pg / 1e6) + ' (%d fs)' % mpeak
    print('route=%2d rate=%6d sample=%.1fMHz lrck=%.1fHz bclk_peak_harmonic=%d din_contrast=%.2f bits_matching=%d/64 offset=%d%s'
          % (route, rate, fs / 1e6, fs / Pg, peak, contrast, match, best, mtxt))
