import serial, struct, sys, time, os
out = sys.argv[1]; port = sys.argv[2] if len(sys.argv) > 2 else 'COM8'
for i in range(60):
    try: s = serial.Serial(port, 115200, timeout=30); break
    except Exception as e: time.sleep(1)
else: sys.exit('no port')
time.sleep(1); s.reset_input_buffer(); s.write(b'g'); s.flush()
def rd(n):
    b = bytearray()
    while len(b) < n:
        c = s.read(n - len(b))
        if not c: raise SystemExit('timeout after %d/%d' % (len(b), n))
        b += c
    return bytes(b)
f = None; name = None; t0 = time.time()
while True:
    tag = rd(1); n = struct.unpack('<I', rd(4))[0]; d = rd(n) if n else b''
    if tag == b'C': name = d.decode(); f = open(os.path.join(out, name + '.board'), 'wb')
    elif tag == b'D': f.write(d)
    elif tag == b'E':
        e, fr, us = struct.unpack('<III', d); f.close(); print('%-42s errors=%d frames=%d decode_ms=%.1f' % (name, e, fr, us / 1000))
    elif tag == b'Z': break
    else: sys.exit('bad tag %r' % tag)
print('done in %.1f s' % (time.time() - t0))
