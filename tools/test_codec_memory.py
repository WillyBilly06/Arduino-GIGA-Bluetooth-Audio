"""Portable real SBC/Opus allocation/storage regressions; no hardware or encoder.

The optional source root lets an isolated test overlay read a frozen production
tree without modifying it. The default runs from the containing source export.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--source-root',type=Path,default=ROOT);p.add_argument('--output',type=Path,default=ROOT/'build/codec-memory-tests');p.add_argument('--sanitize',action='store_true');p.add_argument('--asan',action='store_true');a=p.parse_args();source=a.source_root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 cc=shutil.which('clang');cxx=shutil.which('clang++');assert cc and cxx,'clang and clang++ must be on PATH'
 env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
 if a.asan:
  runtime=Path(subprocess.check_output([cc,'-print-runtime-dir'],text=True).strip());env['PATH']=str(runtime)+os.pathsep+str(runtime.parent/'windows')+os.pathsep+env.get('PATH','')
 flags=['-O1','-g','-fwrapv','-fno-strict-aliasing','-D_CRT_SECURE_NO_WARNINGS','-DGBR_HOST_TEST=1','-I'+str(source/'src')]
 if a.sanitize:flags+=['-fsanitize=undefined','-fsanitize-trap=undefined','-fno-sanitize=shift,signed-integer-overflow']
 if a.asan:flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 inputs={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (source/'src').rglob('*') if p.is_file()};logs=[];start=time.monotonic()
 (out/'source-before.json').write_text(json.dumps(inputs,indent=2)+'\n')
 def run(cmd):
  cmd=list(map(str,cmd));r=subprocess.run(cmd,cwd=ROOT,env=env,text=True,capture_output=True);print(r.stdout+r.stderr,end='',flush=True);logs.append(dict(command=cmd,exit_code=r.returncode,output=r.stdout+r.stderr));(out/'commands.json').write_text(json.dumps(logs,indent=2)+'\n');assert r.returncode==0,(r.returncode,cmd[0])
 groups={}
 for codec in ('sbc','opus'):
  selected=['-DGBR_ENABLE_'+name.upper()+'='+str(int(name==codec)) for name in ('sbc','aac','aac_he','aptx','ldac','lhdc','opus')];current=flags+selected;objects=[]
  sources=sorted((source/'src/vendor/codecs'/codec).rglob('*.c'))
  if codec=='opus':sources=[s for s in sources if s.name!='CNG.c']+[ROOT/'tests/test_opus_cng_storage.c']
  for index,s in enumerate(sources):
   obj=out/(codec+'-'+str(index)+'.o');run([cc,'-std=c11',*current,'-c',s,'-o',obj]);objects.append(obj)
  if codec=='sbc':
   obj=out/'sbc-adapter.o';run([cxx,'-std=c++14',*current,'-c',source/'src/codecs/Codecs.cpp','-o',obj]);objects.append(obj)
   exe=out/'test_sbc_memory.exe';run([cxx,'-std=c++14',*current,ROOT/'tests/test_sbc_memory.cpp',*objects,'-o',exe]);run([exe])
   exe=out/'test_sbc_memory_frontend.exe';deps=['detail/Memory.cpp','detail/Decoder.cpp','detail/Media.cpp','detail/PacketQueue.cpp','detail/AudioTiming.cpp']
   run([cxx,'-std=c++14',*current,*(['-pthread'] if os.name!='nt' else []),ROOT/'tests/test_sbc_memory_frontend.cpp',*[source/'src'/s for s in deps],*objects,'-o',exe]);run([exe])
  else:
   exe=out/'test_opus_memory.exe';run([cxx,'-std=c++14',*current,ROOT/'tests/test_opus_memory.cpp',*objects,'-o',exe]);run([exe])
  groups[codec]=len(sources)
 assert all(hashlib.sha256((source/path).read_bytes()).hexdigest()==want for path,want in inputs.items()),'Production source changed during tests'
 tests={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'tools/test_codec_memory.py',*sorted((ROOT/'tests').glob('test_*memory*.cpp')),ROOT/'tests/test_opus_cng_storage.c',ROOT/'tests/fixtures/opus_memory_packets.h',ROOT/'tests/fixtures/codec_memory_provenance.json']}
 receipt=dict(scope='Actual SBC/Opus memory/bounds/state tests with generated or owned encoded inputs; no independent four-band PCM/performance/interoperability claim',status='PASS',asan=a.asan,ubsan_trap=a.sanitize,legacy_ubsan_exclusions=['shift','signed-integer-overflow'] if a.sanitize else [],source_files_verified_unchanged=len(inputs),source_sha256=inputs,test_sources_sha256=tests,compiled_c_units=groups,elapsed_seconds=time.monotonic()-start)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('PASS codec memory regression',json.dumps(groups),round(receipt['elapsed_seconds'],2),flush=True)
if __name__=='__main__':main()
