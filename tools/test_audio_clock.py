"""Portable actual clock/frontend/register-model regressions, no device/encoder.

The controller and frontend tests use their active clock model. The legacy
unavailable-clock boundary is intentionally not linked into these tests.
Register/counter tests extract current production functions without edits.
Native masks/registers are models, not ARM execution or physical audio proof.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,time
from test_audio_scheduling import compiler
ROOT=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def extract(text,signature):
 start=text.index(signature);i=text.index('{',start);depth=0;state='code'
 while i<len(text):
  c=text[i];pair=text[i:i+2]
  if state=='line':
   if c=='\n':state='code'
  elif state=='block':
   if pair=='*/':state='code';i+=1
  elif state in ('"',"'"):
   if c=='\\':i+=1
   elif c==state:state='code'
  elif pair=='//':state='line';i+=1
  elif pair=='/*':state='block';i+=1
  elif c in ('"',"'"):state=c
  elif c=='{':depth+=1
  elif c=='}':
   depth-=1
   if not depth:return text[start:i+1],text.count('\n',0,start)+1,text.count('\n',0,i)+1
  i+=1
 raise ValueError('Unterminated production function '+signature)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'build/audio-clock-tests');p.add_argument('--compiler');p.add_argument('--cc');a=p.parse_args();out=a.output.resolve()
 if out==ROOT or any(ROOT/x==out or ROOT/x in out.parents for x in ['src','tests','tools','examples','docs']):p.error('output must be outside source directories')
 out.mkdir(parents=True,exist_ok=True);cxx=compiler(a.compiler or os.environ.get('CXX'),'clang++');cc=compiler(a.cc or os.environ.get('CC'),'clang',cxx);env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
 rt=Path(subprocess.check_output([str(cc),'-print-runtime-dir'],text=True).strip());env['PATH']=str(rt)+os.pathsep+str(rt.parent/'windows')+os.pathsep+env.get('PATH','')
 flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize=vptr','-fno-omit-frame-pointer','-I'+str(ROOT/'src'),'-I'+str(out)]
 names=['test_audio_clock_control','test_media_retained','test_clock_frontend','test_clock_trim','test_source_counters','test_clock_release','test_clock_priority'];testfiles=[ROOT/'tests'/(n+'.cpp')for n in names];testfiles += [ROOT/'tests/support/clock_backend_contract.h',*sorted((ROOT/'tests/support/audio_scheduler').glob('*.h')),Path(__file__),ROOT/'tools/test_audio_scheduling.py']
 inputs={p.relative_to(ROOT).as_posix():digest(p)for p in [*sorted(p for p in(ROOT/'src').rglob('*')if p.is_file()),*testfiles]};(out/'source-inputs.json').write_text(json.dumps(inputs,indent=2));steps=[];extractions=[];start=time.monotonic();passed=[];source=ROOT/'src/platform/GigaI2s.cpp';text=source.read_text()
 def function(signature):
  body,first,last=extract(text,signature);extractions.append(dict(signature=signature,source_sha256=digest(source),first_line=first,last_line=last,body_sha256=hashlib.sha256(body.encode()).hexdigest()));return body
 (out/'source-functions.inc').write_text('\n'.join(function(s)for s in ['void refill(unsigned half)','size_t i2sWrite(const int32_t* pcm,size_t frames)','OutputSourceSnapshot i2sSourceSnapshot()'])+'\n')
 (out/'release-function.inc').write_text(function('void restoreClock()')+'\n')
 methods=[function(s)for s in ['bool criticalSupported()','uint32_t basepri()','void raiseBasepri(uint32_t value)','void restoreBasepri(uint32_t value)','void syncMask()','uint32_t enterCritical()','void exitCritical(uint32_t prior)']]
 (out/'priority-functions.inc').write_text(function('bool clockPrioritySupported(bool stopped=false)')+'\nstruct ClockTrimIo{\n'+'\n'.join(methods)+'\n};\n'+function('struct ClockSelectiveGuard')+';\n')
 def persist(status):
  (out/'commands.json').write_text(json.dumps(steps,indent=2));(out/'receipt.json').write_text(json.dumps(dict(status=status,scope=__doc__,asan=True,ubsan=True,passed_cases=passed,source_inputs_sha256=inputs,verbatim_extractions=extractions,seconds=time.monotonic()-start),indent=2))
 def run(label,cmd):
  cmd=list(map(str,cmd));r=subprocess.run(cmd,cwd=ROOT,env=env,capture_output=True,text=True,timeout=90);steps.append(dict(label=label,command=cmd,exit_code=r.returncode,output=r.stdout+r.stderr));persist('RUNNING');print(r.stdout+r.stderr,end='',flush=True)
  if r.returncode:raise RuntimeError(label+' failed')
 frontend=[ROOT/'src'/s for s in ['detail/Memory.cpp','detail/Media.cpp','detail/Decoder.cpp','detail/PacketQueue.cpp','detail/AudioTiming.cpp','Types.cpp']]
 try:
  for name in names:
   selected=frontend if name=='test_clock_frontend'else [ROOT/'src/detail/Memory.cpp',ROOT/'src/detail/Media.cpp',ROOT/'src/Types.cpp']if name=='test_media_retained'else [ROOT/'src/detail/AudioTiming.cpp']if name=='test_source_counters'else []
   extra=['-DARDUINO=1','-I'+str(ROOT/'tests/support/audio_scheduler'),*(['-pthread']if os.name!='nt'else[])]if name=='test_clock_frontend'else []
   exe=out/(name+('.exe'if os.name=='nt'else''));run(name+'-compile',[cxx,*flags,*extra,ROOT/'tests'/(name+'.cpp'),*selected,'-o',exe])
   for case in (['uart','nested','contract','duration','guard']if name=='test_clock_priority'else ['']):run(name+(':'+case if case else''),[exe,*([case]if case else[])]);passed.append(name+(':'+case if case else''))
  if any(digest(ROOT/p)!=h for p,h in inputs.items()):raise RuntimeError('Compilation/source input changed')
  persist('PASS_PORTABLE_ACTUAL_AUDIO_CLOCK_MODELS')
 except BaseException:
  persist('FAIL');raise
 print('PASS actual scalar clock, active frontend, retained fragments, sourceP/Z, RCC ownership and selective-mask native models; no physical qualification')
if __name__=='__main__':main()
