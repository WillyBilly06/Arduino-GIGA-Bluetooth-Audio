"""Actual Arduino owner/adapter wake regression; native RTOS/UART boundaries."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',default=os.environ.get('CC','clang'))
    ap.add_argument('--cxx',default=os.environ.get('CXX','clang++'))
    ap.add_argument('--sanitize',action='store_true')
    ap.add_argument('--asan',action='store_true')
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[1]
    out=root/'build'/'owned-wake';out.mkdir(parents=True,exist_ok=True)
    flags=['-Wall','-Wextra','-Werror','-DARDUINO=1','-DGIGA_BREDR_USE_BTSTACK=0',
           '-I'+str(root/'src'),'-I'+str(root/'tests/support/owned_radio')]
    if args.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=all']
    if args.asan:flags+=['-fsanitize=address','-fno-omit-frame-pointer']
    env=os.environ.copy()
    if args.asan:
        runtime=Path(subprocess.check_output([args.cc,'-print-runtime-dir'],text=True).strip())
        env['PATH']=os.pathsep.join([str(runtime),str(runtime.parent/'windows'),env.get('PATH','')])
    records=[]
    def run(command):
        command=list(map(str,command))
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
    actor=['src/owned/OwnedHost.cpp','src/owned/ClassicProfiles.cpp','src/owned/LeProfiles.cpp','src/platform/RadioProtocol.cpp']
    for name in ['test_owned_radio_wake','test_receiver_wake_lifetime']:
        extra=[*[root/n for n in actor],*objects]if name=='test_owned_radio_wake'else[]
        exe=out/(name+('.exe'if os.name=='nt'else''))
        result=run([args.cxx,'-std=c++17',*flags,root/'tests'/(name+'.cpp'),*[root/n for n in frontend],*extra,'-o',exe])
        if result:return result
        result=run([exe])
        if result:return result
    return 0

if __name__=='__main__':raise SystemExit(main())
