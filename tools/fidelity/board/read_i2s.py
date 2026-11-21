"""Host side of I2sProbe: trigger captures, decode Philips I2S from raw pad samples."""
import serial, struct, sys, time, os
port = sys.argv[1] if len(sys.argv) > 1 else 'COM8'; out = sys.argv[2] if len(sys.argv) > 2 else '.'
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
L = lambda n: (n ^ 0xA53C96) & 0xFFFFFF
R = lambda n: ((n * 0x1F3) + 0x3C5A) & 0xFFFFFF
summary = {}
def decode(rate, n, cycles, first, last, data, core):
    secs = cycles / core; fs = n / secs
    clk = [b & 1 for b in data]; din = [(b >> 1) & 1 for b in data]; lr = [(b >> 2) & 1 for b in data]
    rise = [i for i in range(1, n) if clk[i] and not clk[i - 1]]
    bits = [(din[i], lr[i]) for i in rise]
    lrt = [j for j in range(1, len(bits)) if bits[j][1] != bits[j - 1][1]]
    span = (rise[-1] - rise[0]) / fs if len(rise) > 1 else 0
    bclk_hz = (len(rise) - 1) / span if span else 0
    lr_hz = (lrt[-1] - lrt[0]) and (len(lrt) - 1) / 2 / ((rise[lrt[-1]] - rise[lrt[0]]) / fs) if len(lrt) > 2 else 0
    # Bits per LRCK half-period (slot width) between transitions.
    widths = sorted(set(lrt[k + 1] - lrt[k] for k in range(len(lrt) - 1)))
    words = []
    for j in lrt:
        if j + 1 + 32 <= len(bits):
            w = 0
            for b in bits[j + 1:j + 33]: w = (w << 1) | b[0]
            words.append((bits[j][1], w))
    frames = ok = bad = low = cont = 0; prev = None; nums = []
    for k in range(len(words) - 1):
        (c0, w0), (c1, w1) = words[k], words[k + 1]
        if c0 != 0 or c1 != 1: continue
        frames += 1
        if (w0 & 255) or (w1 & 255): low += 1
        num = (w0 >> 8) ^ 0xA53C96
        if R(num) == (w1 >> 8): ok += 1; nums.append(num)
        else: bad += 1
        if prev is not None and num == prev + 1: cont += 1
        prev = num
    inrange = sum(1 for x in nums if first - 20 * rate // 1000 - 64 <= x < last)
    return dict(sample_mhz=fs / 1e6, samples_per_bit=fs / bclk_hz if bclk_hz else 0, bclk_hz=bclk_hz, bclk_per_fs=bclk_hz / rate,
                lrck_hz=lr_hz, slot_bits=widths, frames=frames, pattern_ok=ok, pattern_bad=bad, nonzero_low_byte=low,
                consecutive=cont, in_written_range=inrange)
def decodeI(rate, n, cycles, data, core):
    fs = n / (cycles / core)
    clk = [b & 1 for b in data]; din = [(b >> 1) & 1 for b in data]
    rise = [i for i in range(1, n) if clk[i] and not clk[i - 1]]
    bits = [din[i] for i in rise]
    gaps = [rise[k + 1] - rise[k] for k in range(len(rise) - 1)]
    nominal = fs / (64 * rate); missed = sum(1 for g in gaps if g > 1.5 * nominal)
    def word(o): 
        w = 0
        for b in bits[o:o + 32]: w = (w << 1) | b
        return w
    best = (0, 0, 0)
    for off in range(64):  # frame alignment from the pattern itself
        ok = tot = 0
        for o in range(off, len(bits) - 64, 64):
            tot += 1; w0, w1 = word(o), word(o + 32)
            if not (w0 & 255) and not (w1 & 255) and R((w0 >> 8) ^ 0xA53C96) == (w1 >> 8): ok += 1
        if ok > best[0]: best = (ok, tot, off)
    span = (rise[-1] - rise[0]) / fs if len(rise) > 1 else 0
    return dict(sample_mhz=fs / 1e6, spb=nominal, bclk_hz=(len(rise) - 1) / span if span else 0, missed_edges=missed, ok=best[0], frames=best[1])
core = 480000000
os.makedirs(out, exist_ok=True)
while True:
    tag = rd(1); n = struct.unpack('<I', rd(4))[0]; d = rd(n) if n else b''
    if tag == b'S':
        t = d.decode(); print(t)
        if 'core_hz=' in t: core = int(t.split('core_hz=')[1])
    elif tag == b'K':
        rate, cnt, cycles, first, last = struct.unpack('<5I', d[:20]); raw = d[20:]
        open(os.path.join(out, 'cap_%d_%d.bin' % (rate, sum(1 for k in summary if k[0] == rate))), 'wb').write(d)
        summary[(rate, len([k for k in summary if k[0] == rate]))] = (cnt, cycles, first, last, raw)
    elif tag == b'Z': break
for (rate, k), (cnt, cycles, first, last, raw) in sorted(summary.items()):
    if rate & 0x40000000:
        if not cycles: print('rate=%6d MDMA cap=%d transfer error' % (rate & 0xffffff, k)); continue
        r = decodeI(rate & 0xffffff, cnt, cycles, raw, core)
        print('rate=%6d MDMA       cap=%d sample=%.1fMHz spb=%.2f bclk=%.0f (%.2f x fs) missed_edges=%d frames_matching_pattern=%d/%d'
              % (rate & 0xffffff, k, r['sample_mhz'], r['spb'], r['bclk_hz'], r['bclk_hz'] / (rate & 0xffffff), r['missed_edges'], r['ok'], r['frames']))
        continue
    if rate & 0x80000000:
        r = decodeI(rate & 0x7fffffff, cnt, cycles, raw, core)
        print('rate=%6d GPIOI-only cap=%d sample=%.1fMHz spb=%.2f bclk=%.0f (%.2f x fs) missed_edges=%d frames_matching_pattern=%d/%d'
              % (rate & 0x7fffffff, k, r['sample_mhz'], r['spb'], r['bclk_hz'], r['bclk_hz'] / (rate & 0x7fffffff), r['missed_edges'], r['ok'], r['frames']))
        continue
    r = decode(rate, cnt, cycles, first, last, raw, core)
    print('rate=%6d cap=%d sample=%.1fMHz spb=%.2f bclk=%.0f (%.2f x fs) lrck=%.1f slots=%s frames=%d ok=%d bad=%d lowbyte!=0:%d consecutive=%d in_range=%d'
          % (rate, k, r['sample_mhz'], r['samples_per_bit'], r['bclk_hz'], r['bclk_per_fs'], r['lrck_hz'], r['slot_bits'], r['frames'],
             r['pattern_ok'], r['pattern_bad'], r['nonzero_low_byte'], r['consecutive'], r['in_written_range']))
