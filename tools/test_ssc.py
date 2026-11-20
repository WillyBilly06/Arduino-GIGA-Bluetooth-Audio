"""Native Samsung SSC adapter checks; no phone, M7 timing or acoustic claim.

Builds A2DP RTP packets (12-byte header, frames back to back) from pinned frames made by
Samsung's own encoder (tests/fixtures/ssc), decodes them through Codecs.cpp/Media.cpp and
compares the int32 PCM bit-exactly with the standalone upstream decoder: always against the
pinned digests, and live when --reference-root names the upstream ssc_decoder folder.
"""
from pathlib import Path
import argparse,base64,hashlib,json,os,shutil,struct,subprocess,time
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=ROOT/'tests/fixtures/ssc'
REFERENCE_MAIN=r'''#include <stdio.h>
#include <stdlib.h>
#include "ssc_decoder.h"
int main(int argc,char**argv){
 FILE*fi=fopen(argv[1],"rb"),*fo=fopen(argv[2],"wb");ssc_decoder*d=ssc_decoder_create(atoi(argv[3]));
 static unsigned char f[4096];static int32_t pcm[2*SSC_FRAME_SAMPLES];unsigned char h[2];int n=0;
 if(argc!=4||!fi||!fo||!d)return 1;
 while(fread(h,1,2,fi)==2){int len=h[0]|h[1]<<8;if(len>4096||fread(f,1,len,fi)!=(size_t)len)return 1;
  if(ssc_decode_s32(d,f,len,pcm)!=SSC_FRAME_SAMPLES)return 1;fwrite(pcm,4,2*SSC_FRAME_SAMPLES,fo);n++;}
 ssc_decoder_destroy(d);fclose(fi);fclose(fo);printf("SSC_REFERENCE frames=%d\n",n);return 0;}
'''
def text(path):return path.read_bytes().replace(b'\r\n',b'\n') # Checkout line endings do not change the pin.
def records(path):return [base64.b64decode(line,validate=True)for line in text(path).split(b'\n')if line]
def container(items):return b''.join(struct.pack('<H',len(x))+x for x in items)
def build(case,sources):
    # Frame k of the stream is frame k of its source: one signal, bitrate chosen per packet.
    packets=[];frames=[];k=0;ts=0x1000
    for i in range(case['packets']):
        chosen=[sources[s][k+j] for j,s in enumerate(case['pattern'][i%len(case['pattern'])])];k+=len(chosen)
        packets.append(struct.pack('>BBHII',0x80,0x60,(0x4321+i)&0xffff,ts,0x55534321)+b''.join(chosen))
        ts+=864*len(chosen);frames+=chosen
    return packets,frames
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'build/ssc-tests');p.add_argument('--cc');p.add_argument('--cxx')
    p.add_argument('--asan',action='store_true');p.add_argument('--sanitize',action='store_true')
    p.add_argument('--reference-root',type=Path,help='upstream samsung-ssc-decoder/ssc_decoder for a live bit-exact comparison')
    a=p.parse_args();out=a.output.resolve()
    if out==ROOT or any(ROOT/n==out or ROOT/n in out.parents for n in ('src','tests','tools','examples','docs')):p.error('output must be outside source directories')
    out.mkdir(parents=True,exist_ok=True)
    cc=a.cc or shutil.which('clang');cx=a.cxx or shutil.which('clang++');assert cc and cx
    env=os.environ.copy()
    if a.asan:
        rt=Path(subprocess.check_output([cc,'-print-runtime-dir'],text=True).strip())
        env['PATH']=str(rt)+os.pathsep+str(rt.parent/'windows')+os.pathsep+env.get('PATH','')
    # No FMA contraction: the pinned PCM is the plain IEEE single-precision result.
    # Codecs.cpp helpers used only by the disabled codecs are unused in this SSC-only build.
    common=['-O2','-g','-ffp-contract=off','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-D_CRT_SECURE_NO_WARNINGS']
    if a.asan:common+=['-fsanitize=address','-fno-omit-frame-pointer']
    if a.sanitize:common+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=undefined']
    flags=[*common,'-Wno-unused-function','-DGBR_HOST_TEST=1','-I'+str(ROOT/'src')]+['-DGBR_ENABLE_'+n+'='+str(int(n=='SSC'))for n in ('SBC','AAC','AAC_HE','APTX','LDAC','LHDC','OPUS','SSC')]
    manifest=json.loads((FIXTURE/'manifest.json').read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
    sources={}
    for name,pin in manifest['files'].items():
        data=text(FIXTURE/name);assert len(data)==pin['bytes']and sha(data)==pin['sha256'],name
        sources[name]=records(FIXTURE/name);assert len(sources[name])==pin['frames']and sorted({len(f)for f in sources[name]})==pin['frame_bytes'],name
    units=[*sorted((ROOT/'src/vendor/codecs/ssc').glob('*.c')),ROOT/'src/codecs/Codecs.cpp',ROOT/'src/detail/Memory.cpp',ROOT/'src/detail/Media.cpp',ROOT/'tests/test_ssc_codec.cpp']
    rec=dict(status='RUNNING',scope=manifest['scope'],asan=a.asan,ubsan=a.sanitize,ubsan_exclusions=['vptr']if a.sanitize else[],
             live_reference=bool(a.reference_root),source_sha256={str(s.relative_to(ROOT)):sha(s.read_bytes())for s in units},steps=[],cases=[])
    def save():(out/'receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
    def run(label,cmd):
        cmd=list(map(str,cmd));r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=600)
        (out/(label+'.log')).write_text(r.stdout+r.stderr);rec['steps'].append(dict(label=label,command=cmd,returncode=r.returncode));save()
        assert r.returncode==0,(label,(r.stdout+r.stderr)[-3000:])
        if r.stdout.strip():print(r.stdout.strip(),flush=True)
    started=time.monotonic();save()
    try:
        objects=[]
        for i,s in enumerate(units):
            o=out/(str(i)+'-'+s.stem+'.o');objects.append(o)
            run('compile-'+s.stem,[cc if s.suffix=='.c'else cx,*flags,'-std=c99'if s.suffix=='.c'else'-std=c++17','-c',s,'-o',o])
        exe=out/('test_ssc_codec.exe'if os.name=='nt'else'test_ssc_codec');run('link',[cx,*flags,*objects,'-o',exe])
        reference=None
        if a.reference_root:
            root=a.reference_root.resolve();main_c=out/'ssc_reference_main.c';main_c.write_text(REFERENCE_MAIN)
            reference=out/('ssc_reference.exe'if os.name=='nt'else'ssc_reference')
            run('reference-build',[cc,*common,'-std=c99','-I'+str(root),main_c,*sorted(root.glob('*.c')),'-o',reference,*([]if os.name=='nt'else['-lm'])])
        run('memory',[exe,'memory'])
        for case in manifest['cases']:
            name=case['name'];packets,frames=build(case,sources)
            (out/(name+'.packets')).write_bytes(container(packets));(out/(name+'.frames')).write_bytes(container(frames))
            pcm=out/(name+'.s32le');run('decode-'+name,[exe,'decode',case['rate'],case['bits'],out/(name+'.packets'),pcm,len(frames)])
            got=pcm.read_bytes();assert len(got)==len(frames)*864*2*4
            if reference:
                ref=out/(name+'.reference.s32le');run('reference-'+name,[reference,out/(name+'.frames'),ref,case['rate']])
                assert ref.read_bytes()==got,(name,'library PCM differs from the standalone decoder')
            assert sha(got)==case['reference_s32_sha256'],(name,'PCM differs from the pinned standalone-decoder digest',sha(got))
            if case.get('malformed'):run('malformed-'+name,[exe,'malformed',case['rate'],out/(name+'.packets')])
            sizes=sorted({len(x)for x in frames})
            rec['cases'].append(dict(name=name,rate=case['rate'],bits=case['bits'],packets=len(packets),frames=len(frames),frame_bytes=sizes,
                                     bit_exact_live=bool(reference),output_sha256=sha(got)))
            print('SSC_CASE',name,'rate='+str(case['rate']),'packets='+str(len(packets)),'frames='+str(len(frames)),'frame_bytes='+str(sizes),
                  'bit_exact='+('live+pinned'if reference else'pinned'),flush=True)
        assert all(sha((ROOT/n).read_bytes())==v for n,v in rec['source_sha256'].items())
        rec.update(status='PASS_NATIVE_SSC_ADAPTER',seconds=round(time.monotonic()-started,2))
    except BaseException:
        rec['status']='FAILED_SSC_TEST';raise
    finally:save()
    print('PASS SSC adapter: '+str(len(manifest['cases']))+' Samsung-encoder streams bit-exact, malformed/truncated frames rejected, no decode-time allocation; no phone/M7/acoustic qualification')
if __name__=='__main__':main()
