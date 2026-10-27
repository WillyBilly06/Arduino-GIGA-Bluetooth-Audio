"""Compare successful same-target baseline/candidate fixture stdout receipts.

This script does not execute binaries. The caller must separately establish both
target process return codes and bind stdout files to the provided ELF hashes.
"""
from pathlib import Path
import argparse,hashlib,json,re
p=argparse.ArgumentParser();p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
def parse(path):
    text=path.read_text();assert text.rstrip().endswith('AAC_CHANNEL_STORAGE PASS')
    assert 'TAIL_FLUSH_CHANNEL_GUARDS PASS' in text and 'ALLOCATION_MATRIX configs=66' in text
    rows=[]
    for line in text.splitlines():
        if line.startswith('ALLOC '):rows.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)})
    assert len(rows)==66
    expected={(he,ch,rate,t) for he in (0,1) for ch in (1,2) for rate in (8000,16000,24000,32000,44100,48000) for t in (0,1,2) if not (he and rate==8000)}
    assert {(r['he'],r['ch'],r['rate'],r['transport']) for r in rows}==expected
    pcm=[line for line in text.splitlines() if line.startswith('PCM ')]
    assert len(pcm)==8
    assert [re.search(r'label=(\S+)',line)[1] for line in pcm]==['lc-adts','lc-raw','lc-latm','he-mono-adts','he-mono-raw','he-stereo-adts','he-stereo-raw','he-stereo-latm']
    return rows,pcm
b,bp=parse(a.baseline);c,cp=parse(a.candidate);assert bp==cp,'PCM/status/consumed/frame digest mismatch'
for x,y in zip(b,c):
    for k in ['he','ch','rate','transport','blocks','calls']:assert x[k]==y[k],(k,x,y)
    expected=960 if x['he'] and x['ch']==1 else 0
    assert x['payload']-y['payload']==expected,(x,y)
    assert x['raw']-y['raw']==expected,(x,y)
receipt=dict(status='SAME_TARGET_PAIRED_STDOUT_PASS_REQUIRES_EXTERNAL_EXIT_AND_ELF_BINDING',configs=66,pcm_streams=bp,heap_scope='Tracked codec requested payload and guarded provider raw blocks; not total MCU SRAM',mono_he_payload_delta=960,stereo_and_lc_delta=0,allocation_block_count_unchanged=True,stdio_sha256={str(n):hashlib.sha256(n.read_bytes()).hexdigest() for n in [a.baseline,a.candidate]})
a.output.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
