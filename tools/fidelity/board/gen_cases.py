# Runs on the VM in ~/gbpar. Builds the case files, native reference outputs
# and DecoderParity/parity_data.h.
import os, subprocess, struct, shutil
H = os.path.expanduser('~/gbpar'); F = os.path.expanduser('~/gbfid2/build/fidelity'); T = os.path.expanduser('~/gbfid3/tests/lhdc_aosp')
os.chdir(H)
CODEC = {'sbc': 0, 'aac': 1, 'aptx': 2, 'aptxhd': 3, 'ldac': 4, 'lhdc': 5, 'opus': 6, 'ssc': 8}
LIMIT = 64 * 1024
cases = []
def cut(path, out):
    b = open(path, 'rb').read(); o = 0; keep = bytearray()
    while o + 2 <= len(b):
        n = struct.unpack_from('<H', b, o)[0]
        if len(keep) + 2 + n > LIMIT: break
        keep += b[o:o + 2 + n]; o += 2 + n
    open(out, 'wb').write(keep)
for tag, codec, rate, bits in [('sbc', 'sbc', 48000, 16), ('sbcxq', 'sbc', 48000, 16), ('aac', 'aac', 48000, 16), ('aptx', 'aptx', 48000, 16),
                               ('aptxhd', 'aptxhd', 48000, 24), ('ldac990', 'ldac', 96000, 24), ('opus', 'opus', 48000, 16)]:
    env = dict(os.environ); env.pop('TONES', None)
    subprocess.run([F + '/enc', tag], check=True, env=env, stdout=subprocess.DEVNULL)
    cut(tag + '.pkt', tag + '.case'); cases.append((tag, codec, rate, bits, 0))
for d in sorted(os.listdir(T)):
    if '-' in d and os.path.isdir(os.path.join(T, d)):
        r, b = map(int, d.split('-')); tag = 'lhdc' + d
        shutil.copy(os.path.join(T, d, 'encoded.bin'), tag + '.case'); cases.append((tag, 'lhdc', r, b, 1))
for name, r, b in [('uhq96_584k_one_per_packet', 96000, 24), ('ssc44_bitrate_switch_192_128_88_152', 44100, 16)]:
    tag = 'ssc_' + name; shutil.copy('/tmp/gb_ssc/' + name + '.packets', tag + '.case'); cases.append((tag, 'ssc', r, b, 0))
harness = {'sbc': 'sbc', 'aac': 'aac', 'aptx': 'aptx', 'aptxhd': 'aptx', 'ldac': 'ldac', 'opus': 'opus', 'lhdc': 'lhdc', 'ssc': 'ssc'}
os.makedirs('DecoderParity', exist_ok=True)
with open('DecoderParity/parity_data.h', 'w') as h:
    h.write('#pragma once\n#include <stdint.h>\n#include <stddef.h>\n')
    h.write('struct ParityCase { const char* name; uint8_t codec; uint32_t rate; uint8_t bits; uint8_t raw; const uint8_t* data; size_t bytes; };\n')
    total = 0
    for i, (tag, codec, rate, bits, raw) in enumerate(cases):
        r = subprocess.run(['./gbdec_' + harness[codec], codec, str(rate), str(bits), tag + '.case', tag + '.native'], capture_output=True, text=True)
        print(tag, r.stdout.strip(), r.stderr.strip()[:200])
        b = open(tag + '.case', 'rb').read(); total += len(b)
        h.write('static const uint8_t parity_%d[] = {' % i)
        for j in range(0, len(b), 32): h.write(','.join(str(x) for x in b[j:j + 32]) + ',\n')
        h.write('};\n')
    h.write('static const ParityCase parityCases[] = {\n')
    for i, (tag, codec, rate, bits, raw) in enumerate(cases):
        h.write(' {"%s",%d,%d,%d,%d,parity_%d,sizeof parity_%d},\n' % (tag, CODEC[codec], rate, bits, raw, i, i))
    h.write('};\n')
open('cases.txt', 'w').write('\n'.join('%s %s %d %d' % c[:4] for c in cases) + '\n')
print('cases', len(cases), 'embedded bytes', total)
