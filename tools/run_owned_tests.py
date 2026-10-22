"""Portable synthetic receiver/owned-host checks. No hardware or media fixtures."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,json
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--asan',action='store_true');a=p.parse_args();cc=shutil.which('clang');cxx=shutil.which('clang++')
 if not cc or not cxx:p.error('clang and clang++ must be on PATH')
 out=ROOT/'build/owned-tests';out.mkdir(parents=True,exist_ok=True);env=os.environ.copy();flags=['-Wall','-Wextra','-Werror','-g','-Isrc','-Isrc/owned/core']
 if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=all']
 if a.asan:
  flags+=['-fsanitize=address','-fno-omit-frame-pointer'];rt=Path(subprocess.check_output([cc,'-print-runtime-dir'],text=True).strip());env['PATH']=str(rt)+os.pathsep+str(rt.parent/'windows')+os.pathsep+env.get('PATH','')
 logs=[]
 def run(cmd):
  cmd=list(map(str,cmd));r=subprocess.run(cmd,cwd=ROOT,env=env,text=True,capture_output=True);print(r.stdout+r.stderr,end='',flush=True);logs.append(dict(command=cmd,exit=r.returncode,stdout=r.stdout,stderr=r.stderr));(out/'receipt.json').write_text(json.dumps({'synthetic_only':True,'logs':logs},indent=2)+'\n');r.check_returncode()
 objects=[]
 for s in sorted((ROOT/'src/owned/core').glob('*.c')):
  obj=out/(s.stem+'.o');run([cc,'-std=c11',*flags,'-c',s,'-o',obj]);objects.append(obj)
 actor=['src/owned/OwnedHost.cpp','src/owned/ClassicProfiles.cpp','src/owned/LeProfiles.cpp','src/platform/RadioProtocol.cpp']
 for n in ['test_single_hnc','test_single_signal','test_ble_audio_qos','test_le_connection_preference','test_le_transport','test_le_control']:
  exe=out/(n+'.exe');run([cc,'-std=c11',*flags,'tests/owned_core/'+n+'.c',*objects,'-o',exe]);run([exe])
 units=['test_owned_classic','test_owned_ble','test_owned_host','test_owned_host_review','test_owned_host_bootstrap','test_owned_host_ble_large','test_owned_host_service','test_owned_host_single_hnc','test_owned_host_hnc','test_owned_host_benign','test_owned_host_le_requests','test_owned_host_signaling','test_owned_host_ble_commands','test_owned_host_ble_unsubscribe','test_owned_host_config_reject','test_owned_host_abort','test_owned_host_scheduling','test_owned_host_classic_combined','test_owned_host_mixed_att','test_uart_tx_mailbox','test_ble_audio_qos','test_ble_connection_preference']
 for n in units:
  sources=actor[1:] if n in ('test_owned_host_scheduling','test_owned_host_classic_combined') else actor
  extra=['-DGBR_QOS_CANDIDATE=1'] if n=='test_ble_audio_qos' else []
  arguments=['all'] if n in ('test_ble_audio_qos','test_ble_connection_preference') else []
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*extra,'-Itests','tests/'+n+'.cpp',*sources,*objects,'-o',exe]);run([exe,*arguments])
 frontend=['tests/empty_registry.cpp','src/GigaBredr.cpp','src/Types.cpp','src/detail/Memory.cpp','src/detail/Decoder.cpp','src/detail/Media.cpp','src/detail/PacketQueue.cpp','src/detail/AudioTiming.cpp']
 portable={'test_audio_timing':['src/detail/AudioTiming.cpp'],'test_rtp_discontinuity':['src/detail/AudioTiming.cpp'],'test_receiver':frontend,'test_receiver_lifetime':frontend,'test_i2s':[],'test_i2s_thread':[],'test_i2s_priming':[],'test_i2s_pins':[],'test_i2s_routes':[],'test_i2s_recovery':[],'test_pcm_transition':[],'test_i2s_runtime':['src/platform/GigaI2s.cpp']}
 for n,sources in portable.items():
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/'+n+'.cpp',*sources,'-o',exe]);run([exe])
 # This test includes the real GigaBredr.cpp itself and mocks the radio/I2S
 # boundary. Do not link a second GigaBredr.cpp or the actor implementations.
 exe=out/'test_ble_nonblocking.exe'
 facade=[source for source in frontend if source!='src/GigaBredr.cpp']
 run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_ble_nonblocking.cpp',*facade,'-o',exe]);run([exe])
 exe=out/'test_ble_origin.exe';run([cxx,'-std=c++17',*flags,'-Itests/support/receiver_public','tests/test_ble_origin.cpp',*frontend,'src/owned/LeProfiles.cpp',*objects,'-o',exe])
 for case in ['actual_loop_reconnect','copy_retry','stale_generation','byte_value_semantics','disconnect_after_admission','stopped']:run([exe,case])
 exe=out/'test_i2s_pins_legacy.exe';run([cxx,'-std=c++17',*flags,'-DGIGA_BREDR_I2S_WS_PIN=69','tests/test_i2s_pins.cpp','-o',exe]);run([exe])
 exe=out/'test_i2s_routes_legacy.exe';run([cxx,'-std=c++17',*flags,'-DGIGA_BREDR_I2S_WS_PIN=69','tests/test_i2s_routes.cpp','-o',exe]);run([exe])
 run([sys.executable,'tests/test_owned_wake.py','--cc',cc,'--cxx',cxx,*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_codec_memory.py',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 # This child always enables ASan/UBSan and keeps its receipt under this run.
 run([sys.executable,'tools/test_audio_scheduling.py','--compiler',cxx,'--cc',cc,'--output',out/'audio-scheduling'])
 print('PASS public synthetic receiver, owned host, HCI/ATT lifecycle, PCM recovery and wake checks; no physical qualification')
if __name__=='__main__':main()
