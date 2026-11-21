"""Equivalent-time decode of I2sFold captures: fold by LRCK-derived frame phase."""
import serial, struct, sys, time
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
    elif tag == b'K': caps.append((struct.unpack('<3I', d[:12]), d[12:]))
    elif tag == b'Z': break
import pickle, os
pickle.dump(caps, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fold_caps.pkl'), 'wb'))
if len(sys.argv) > 2 and sys.argv[2] == 'save': sys.exit(0)
EXPECT = [(0xA53C96 << 8 >> (31 - i)) & 1 for i in range(32)] + [(0x5A3C69 << 8 >> (31 - i)) & 1 for i in range(32)]
BINS = 1024
for k, ((rate, n, cycles), raw) in enumerate(caps):
    fs = n / (cycles / 480e6)
    lr = [(b >> 2) & 1 for b in raw]
    falls = [i for i in range(1, n) if lr[i - 1] and not lr[i]]
    if len(falls) < 8: print('rate=%6d cap=%d only %d LRCK edges' % (rate, k, len(falls))); continue
    # Least-squares frame period and offset from LRCK falling edges (frame starts).
    m = len(falls); idx = list(range(m)); mx = sum(idx) / m; my = sum(falls) / m
    P = sum((x - mx) * (y - my) for x, y in zip(idx, falls)) / sum((x - mx) ** 2 for x in idx); a = my - P * mx
    resid = max(abs(f - (a + P * j)) for j, f in enumerate(falls))
    acc = [[0, 0, 0, 0] for _ in range(BINS)]  # count, bclk, din, lrck
    for i, b in enumerate(raw):
        ph = ((i - a) / P) % 1.0; q = acc[int(ph * BINS)]
        q[0] += 1; q[1] += b & 1; q[2] += (b >> 1) & 1; q[3] += (b >> 2) & 1
    clk = [q[1] / q[0] if q[0] else 0 for q in acc]; din = [q[2] / q[0] if q[0] else 0 for q in acc]
    # BCLK cycles per frame: rising crossings of the folded clock (threshold 0.5).
    rises = [j for j in range(BINS) if clk[j - 1] < 0.5 <= clk[j]]
    clean = sum(1 for c in clk if c < 0.1 or c > 0.9) / BINS
    # Data bit at each folded rising edge: average DIN over the next 1/4 bit period (data is stable after the edge).
    w = max(1, BINS // 64 // 4); bits = []
    for r in rises:
        v = sum(din[(r + t) % BINS] for t in range(1, w + 1)) / w; bits.append(1 if v > 0.5 else 0)
    # Philips: MSB of the left word is one BCLK after the LRCK fall (frame phase 0).
    first = min(range(len(rises)), key=lambda j: rises[j]); order = bits[first:] + bits[:first]
    best = max(range(64), key=lambda o: sum(order[(o + i) % 64] == EXPECT[i] for i in range(64)) if len(order) == 64 else 0)
    match = sum(order[(best + i) % 64] == EXPECT[i] for i in range(64)) if len(order) == 64 else 0
    print('rate=%6d cap=%d sample=%.1fMHz samples/frame=%.4f (fs_meas=%.1f Hz) edge_resid=%.2f smp  bclk_cycles/frame=%d clock_bins_clean=%.0f%%  bits_matching=%d/64 at offset %d'
          % (rate, k, fs / 1e6, P, fs / P, resid, len(rises), 100 * clean, match, best))
