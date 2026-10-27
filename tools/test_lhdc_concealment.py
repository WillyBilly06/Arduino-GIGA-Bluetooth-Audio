"""Actual LHDC frame/adapter/frontend observability; controlled channel seam only."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
R=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=R/'build/lhdc-concealment-native');p.add_argument('--sanitize',action='store_true');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 cc=shutil.which('clang');cxx=shutil.which('clang++');assert cc and cxx,'clang and clang++ must be on PATH'
 if a.sanitize and os.name=='nt':
  dll=sorted((Path(cc).parent.parent/'lib/clang').glob('*/lib/windows'));assert dll;os.environ['PATH']=str(dll[-1])+os.pathsep+os.environ.get('PATH','')
 flags=['-O1','-g','-fwrapv','-fno-strict-aliasing','-D_CRT_SECURE_NO_WARNINGS','-I'+str(R/'src'),'-DGBR_ENABLE_SBC=0','-DGBR_ENABLE_AAC=0','-DGBR_ENABLE_AAC_HE=0','-DGBR_ENABLE_APTX=0','-DGBR_ENABLE_LDAC=0','-DGBR_ENABLE_OPUS=0']
 if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
 source=R/'src/vendor/codecs/lhdc/lhdc_dec.c';text=source.read_text();marker='lhdc_dec_ret_t lhdc_dec_decode_frame(';start=text.index(marker);end=text.index('/* Destroy a decoder instance:',start);prefix=text[:start];body=text[start:end];assert body.count('lhdc_dec_decode_channel_autosel(')==3
 # Preserve all branches under test. Only three channel calls in the public
 # frame function are redirected; parsing, bounds, mute, output and status stay real.
 text=prefix+'extern lhdc_dec_ret_t lhdc_test_channel(lhdc_decoder_t*, int, float*);\n'+body.replace('lhdc_dec_decode_channel_autosel(','lhdc_test_channel(')+text[end:]
 generated=out/'lhdc_dec_test.c';generated.write_text(text)
 # Resolve the copied source's local includes without changing its semantics.
 flags+=['-I'+str(source.parent),'-I'+str(source.parent.parent)]
 sources=[generated]+[x for x in source.parent.glob('*.c')if x!=source]
 commands=[];objects=[]
 for i,s in enumerate(sources):
  obj=out/(str(i)+'.o');cmd=[cc,'-std=c11',*flags,'-c',str(s),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,cwd=R,check=True);objects.append(str(obj))
 cpp=['tests/test_lhdc_concealment.cpp','src/codecs/Codecs.cpp','src/GigaBredr.cpp','src/Types.cpp','src/detail/Memory.cpp','src/detail/Decoder.cpp','src/detail/Media.cpp','src/detail/PacketQueue.cpp','src/detail/AudioTiming.cpp','tests/support/clock_backend_unavailable.cpp'];exe=out/('test_lhdc_concealment.exe'if os.name=='nt'else'test_lhdc_concealment');cmd=[cxx,'-std=c++14',*flags,*[str(R/x)for x in cpp],*objects,'-o',str(exe)];commands.append(cmd);subprocess.run(cmd,cwd=R,check=True)
 results={}
 for case in ('vendor','adapter','frontend'):
  r=subprocess.run([str(exe),case],cwd=R,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(out/(case+'.log')).write_text(r.stdout);results[case]={'exit_code':r.returncode,'output':r.stdout}
 inputs={str(x.relative_to(R)) if x.is_relative_to(R)else str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in sources[1:]+[source]+[R/x for x in cpp]}
 (out/'receipt.json').write_text(json.dumps({'scope':'Controlled channel outcomes; actual public frame branches, adapter and frontend. No encoded PCM correctness claim.','channel_call_replacements':3,'original_vendor_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'generated_vendor_sha256':hashlib.sha256(generated.read_bytes()).hexdigest(),'sanitizers':a.sanitize,'inputs':inputs,'commands':commands,'results':results},indent=2)+'\n')
 print(json.dumps(results,indent=2));assert all(x['exit_code']==0 for x in results.values())
if __name__=='__main__':main()
