"""Actual Arduino owner/adapter wake regression; native RTOS/UART boundaries."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from portable_sources import ACTOR_SOURCES, voice_extra_cpp, voice_sbc_sources, unavailable_voice_boundary, unique

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default=os.environ.get('CC','clang'))
    ap.add_argument('--cxx',default=os.environ.get('CXX','clang++'))
    ap.add_argument('--sanitize',action='store_true')
    ap.add_argument('--asan',action='store_true')
    ap.add_argument('--plan',action='store_true',help='record recipes; execute no compiler/test')
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    out=(args.output or root/'build'/'owned-wake').resolve();out.mkdir(parents=True,exist_ok=True)
    flags=['-Wall','-Wextra','-Werror','-DARDUINO=1','-DGIGA_BREDR_USE_BTSTACK=0',
           '-I'+str(root/'src'),'-I'+str(root/'tests/support/owned_radio')]
    if args.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=all']
    if args.asan:flags+=['-fsanitize=address','-fno-omit-frame-pointer']
    env=os.environ.copy()
    if args.asan and not args.plan:
        runtime=Path(subprocess.check_output([args.cc,'-print-runtime-dir'],text=True).strip())
        env['PATH']=os.pathsep.join([str(runtime),str(runtime.parent/'windows'),env.get('PATH','')])
    records=[]
    def run(command):
        command=list(map(str,command))
        if args.plan:
            records.append({'command':command,'executed':False,'exit_code':None})
            (out/'receipt.json').write_text(json.dumps({'status':'PLAN_ONLY_NO_COMMANDS_EXECUTED','scope':__doc__,'records':records,'runtime_or_sanitizers_qualified':False},indent=2)+'\n')
            return 0
        p=subprocess.run(command,cwd=root,env=env,text=True,capture_output=True)
        print(p.stdout+p.stderr,end='',flush=True)
        records.append({'command':command,'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr})
        (out/'receipt.json').write_text(json.dumps({'scope':__doc__,'records':records},indent=2)+'\n')
        return p.returncode
    objects=[]
    for source in sorted((root/'src/owned/core').glob('*.c')):
        obj=out/(source.stem+'.obj')
        result=run([args.cc,'-std=c11',*flags,'-c',source,'-o',obj])
        if result:return result
        objects.append(obj)
    frontend=['tests/empty_registry.cpp','src/Types.cpp','src/detail/Memory.cpp',
              'src/detail/Decoder.cpp','src/detail/Media.cpp','src/detail/PacketQueue.cpp','src/detail/AudioTiming.cpp','tests/support/clock_backend_unavailable.cpp']
    sbc_objects=[]
    for index,source in enumerate(voice_sbc_sources(root)):
        obj=out/('voice-sbc-'+str(index)+'.o')
        result=run([args.cc,'-std=c11',*flags,'-c',source,'-o',obj])
        if result:return result
        sbc_objects.append(obj)
    actor=ACTOR_SOURCES
    for name in ['test_owned_radio_wake','test_receiver_wake_lifetime']:
        # OwnedRadio supplies its real radioVoice functions; only the legacy
        # frontend mock gets an explicitly unavailable voice boundary.
        extra=[*[root/n for n in actor],*objects,*sbc_objects]if name=='test_owned_radio_wake'else[*unavailable_voice_boundary(root),*objects,*sbc_objects]
        exe=out/(name+('.exe'if os.name=='nt'else''))
        sources=unique([*[root/n for n in frontend],*voice_extra_cpp(root),*extra])
        result=run([args.cxx,'-std=c++17',*flags,root/'tests'/(name+'.cpp'),*sources,'-o',exe])
        if result:return result
        result=run([exe])
        if result:return result
    if args.plan:print('PLAN_ONLY owned wake; no compiler/test/sanitizer execution')
    return 0

if __name__=='__main__':raise SystemExit(main())
