"""Real AAC diagnostic ADTS frontend regression; fake radio/I2S, no hardware.

--plan emits compiler and runtime recipes without executing any command. The
isolated Codecs.cpp object enables AAC only; other product defaults are not
changed. Sanitizer results apply only if explicitly requested and executed.
"""
from pathlib import Path
import argparse,json,os,shutil,subprocess
from portable_sources import VOICE_FRONTEND_SOURCES,voice_extra_c
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc');p.add_argument('--cxx');p.add_argument('--output',type=Path,default=ROOT/'build/adts-retained');p.add_argument('--plan',action='store_true');p.add_argument('--sanitize',action='store_true');p.add_argument('--asan',action='store_true');a=p.parse_args()
 cc=a.cc or shutil.which('clang');cxx=a.cxx or shutil.which('clang++')
 if not cc or not cxx:p.error('clang and clang++ required')
 out=a.output.resolve()
 if out==ROOT or any(ROOT/x==out or ROOT/x in out.parents for x in ('src','tests','tools','examples','docs')):p.error('output must be outside source directories')
 out.mkdir(parents=True,exist_ok=True)
 flags=['-O1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-but-set-variable','-DARDUINO=1','-Isrc','-Itests','-Itests/support/audio_scheduler']
 if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=all']
 if a.asan:flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 records=[]
 def run(args):
  cmd=list(map(str,args));row=dict(command=cmd,executed=False,exit=None);records.append(row)
  if a.plan:return
  result=subprocess.run(cmd,cwd=ROOT,text=True,capture_output=True,timeout=120);row.update(executed=True,exit=result.returncode,stdout=result.stdout,stderr=result.stderr);print(result.stdout+result.stderr,end='',flush=True);result.check_returncode()
 cfiles=[*sorted((ROOT/'src/vendor/codecs/aac').glob('*.c')),*voice_extra_c(ROOT)];objects=[]
 for i,source in enumerate(cfiles):
  obj=out/('c%02d.o'%i);run([cc,'-std=c11',*flags,*(['-Wno-error']if 'aac'in source.parts else[]),'-c',source,'-o',obj]);objects.append(obj)
 codec=out/'actual-aac-codecs.o';isolation=['-DGBR_ENABLE_SBC=0','-DGBR_ENABLE_APTX=0','-DGBR_ENABLE_LDAC=0','-DGBR_ENABLE_LHDC=0','-DGBR_ENABLE_OPUS=0','-DcodecOperations=realCodecOperations','-DcompiledCodecs=realCompiledCodecs']
 run([cxx,'-std=c++17',*flags,'-Wno-error',*isolation,'-c','src/codecs/Codecs.cpp','-o',codec]);objects.append(codec)
 front=['src/detail/Memory.cpp','src/detail/Media.cpp','src/detail/Decoder.cpp','src/detail/PacketQueue.cpp','src/detail/AudioTiming.cpp','src/Types.cpp',*VOICE_FRONTEND_SOURCES,'tests/support/voice_radio_unavailable.cpp']
 exe=out/('adts-retained-prefix.exe'if os.name=='nt'else'adts-retained-prefix');run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_adts_retained_prefix.cpp',*front,*objects,'-o',exe]);run([exe])
 value=dict(status='PLAN_ONLY_NO_COMMANDS_EXECUTED'if a.plan else'PASS_REAL_ADTS_FRONTEND_FAKE_RADIO_I2S',records=records,runtime_or_sanitizers_qualified=not a.plan,actual_aac_decoder=True,explicit_diagnostic_ADTS=True,physical_a2dp_or_voice_qualified=False,isolated_non_AAC_codec_operations_disabled=True)
 (out/'receipt.json').write_text(json.dumps(value,indent=2)+'\n');print('PLAN_ONLY ADTS frontend; no commands executed'if a.plan else'PASS diagnostic ADTS frontend; no hardware qualification')
if __name__=='__main__':main()
