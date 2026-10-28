"""Portable synthetic receiver/owned-host checks; no hardware or media fixtures.

Default compiles/runs native tests on a permitted host. --plan records all
top-level compiler/runtime/child commands without executing any command.
A plan, or compile-only consumption of it, is never a runtime/sanitizer pass.
"""
from pathlib import Path
import argparse,os,shutil,subprocess,sys,json
from portable_sources import ACTOR_SOURCES,VOICE_FRONTEND_SOURCES,frontend_sources,voice_sbc_sources,unique
ROOT=Path(__file__).resolve().parents[1]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--sanitize',action='store_true');p.add_argument('--asan',action='store_true')
 p.add_argument('--cc');p.add_argument('--cxx');p.add_argument('--output',type=Path,default=ROOT/'build/owned-tests')
 p.add_argument('--plan',type=Path,help='emit top-level plan only; run no compiler, executable or child')
 a=p.parse_args();cc=a.cc or shutil.which('clang');cxx=a.cxx or shutil.which('clang++')
 if not cc or not cxx:p.error('clang/clang++ must be on PATH or supplied by --cc/--cxx')
 out=a.output.resolve()
 if out==ROOT or any(ROOT/x==out or ROOT/x in out.parents for x in ('src','tests','tools','examples','docs')):p.error('output must be outside source directories')
 out.mkdir(parents=True,exist_ok=True);env=os.environ.copy()
 flags=['-Wall','-Wextra','-Werror','-g','-Isrc','-Isrc/owned/core']
 if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=all']
 if a.asan:
  flags+=['-fsanitize=address','-fno-omit-frame-pointer']
  if not a.plan:
   rt=Path(subprocess.check_output([cc,'-print-runtime-dir'],text=True).strip());env['PATH']=str(rt)+os.pathsep+str(rt.parent/'windows')+os.pathsep+env.get('PATH','')
 logs=[]
 def run(cmd,case=None):
  cmd=list(map(str,cmd))
  kind='child-suite'if cmd[0]==sys.executable and len(cmd)>1 and cmd[1].endswith('.py')else'compile'if cmd[0]in(str(cc),str(cxx))else'project-runtime'
  if a.plan:
   row=dict(command=cmd,kind=kind,executed=False,exit=None)
   if case:row['case']=case
   logs.append(row);return
  r=subprocess.run(cmd,cwd=ROOT,env=env,text=True,capture_output=True);print(r.stdout+r.stderr,end='',flush=True)
  logs.append(dict(command=cmd,kind=kind,executed=True,exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
  (out/'receipt.json').write_text(json.dumps(dict(synthetic_only=True,status='RUNNING_NATIVE_TESTS',logs=logs),indent=2)+'\n')
  r.check_returncode()
 run([cxx,'-std=c++17',*flags,'-c','tests/test_owned_uart_baud_policy.cpp','-o',out/'uart-baud-policy.o'])
 objects=[];sbc_objects=[]
 for collection,sources in [(objects,sorted((ROOT/'src/owned/core').glob('*.c'))),(sbc_objects,voice_sbc_sources(ROOT))]:
  for s in sources:
   obj=out/('c'+str(len(objects)+len(sbc_objects))+'.o');run([cc,'-std=c11',*flags,'-c',s,'-o',obj]);collection.append(obj)
 voice_objects=[*objects,*sbc_objects]
 for n in ['test_rfcomm','test_hfp_control','test_profile_expiry','test_voice_profiles']:
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,'-DGIGA_BREDR_PORTABLE_TEST_MAIN=1','tests/'+n+'.cpp','src/owned/VoiceProfiles.cpp','src/owned/HfpControl.cpp','src/owned/HspControl.cpp',*objects,'-o',exe],n);run([exe],n)
 actor=list(ACTOR_SOURCES)
 for n in ['test_single_hnc','test_single_signal','test_ble_audio_qos','test_le_connection_preference','test_le_transport','test_le_control','test_sco_owner','test_sco_setup','test_sco_byte_pacing']:
  exe=out/(n+'.exe');extra=[]
  core_entry={'test_sco_owner':'scoOwner','test_sco_setup':'scoSetup','test_sco_byte_pacing':'scoBytePacing'}
  if n in core_entry:
   stem=core_entry[n];wrapper=out/(n+'_main.c')
   wrapper.write_text('#include <stdio.h>\nunsigned '+stem+'Selftest(void);\nunsigned '+stem+'Checks(void);\nint main(void){unsigned r='+stem+'Selftest();printf("'+n+' failure=%u checks=%u\\n",r,'+stem+'Checks());return (int)r;}\n')
   extra=[wrapper]
  run([cc,'-std=c11',*flags,'tests/owned_core/'+n+'.c',*extra,*objects,'-o',exe],n);run([exe],n)
 units=['test_owned_voice','test_avrcp_controls','test_avrcp_channel_retirement','test_avrcp_owner_retirement','test_avrcp_stale_disconnect','test_owned_classic','test_owned_ble','test_owned_host','test_owned_host_review','test_owned_host_bootstrap','test_owned_host_ble_large','test_owned_host_service','test_owned_host_single_hnc','test_owned_host_hnc','test_owned_host_benign','test_owned_host_le_requests','test_owned_host_signaling','test_owned_host_ble_commands','test_owned_host_ble_unsubscribe','test_owned_host_config_reject','test_owned_host_abort','test_owned_host_scheduling','test_owned_host_classic_combined','test_owned_host_mixed_att','test_uart_tx_mailbox','test_ble_audio_qos','test_ble_connection_preference','test_owned_ble_lifecycle','test_owned_voice_transport']
 for n in units:
  sources=actor[1:]if n in ('test_owned_host_scheduling','test_owned_host_classic_combined')else actor
  extra=['-DGBR_QOS_CANDIDATE=1']if n=='test_ble_audio_qos'else[]
  arguments=['all']if n in ('test_ble_audio_qos','test_ble_connection_preference')else[]
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*extra,'-Itests','tests/'+n+'.cpp',*sources,*objects,'-o',exe],n);run([exe,*arguments],n)
 # The exact pending-user/codec wrapper regression has its own main and uses
 # the existing real host fixture through a renamed include, not a fake result.
 n='test_hf_admission';exe=out/(n+'.exe')
 run([cxx,'-std=c++17',*flags,'-Itests','tests/'+n+'.cpp','src/Types.cpp',*actor,*objects,'-o',exe],n)
 for selector in ('pending_user','pending_codec','invalid_generation'):run([exe,selector],n+':'+selector)
 frontend=frontend_sources()
 portable={'test_audio_timing':['src/detail/AudioTiming.cpp'],'test_rtp_discontinuity':['src/detail/AudioTiming.cpp'],'test_receiver':frontend,'test_receiver_lifetime':frontend,'test_i2s':[],'test_i2s_thread':[],'test_i2s_priming':[],'test_i2s_pins':[],'test_i2s_routes':[],'test_i2s_recovery':[],'test_pcm_transition':[],'test_i2s_runtime':['src/platform/GigaI2s.cpp']}
 for n,sources in portable.items():
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/'+n+'.cpp',*sources,*(voice_objects if n in ('test_receiver','test_receiver_lifetime')else objects),'-o',exe],n);run([exe],n)
 facade=frontend_sources(include_giga=False)
 exe=out/'test_ble_nonblocking.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_ble_nonblocking.cpp',*facade,*voice_objects,'-o',exe],'test_ble_nonblocking');run([exe],'test_ble_nonblocking')
 exe=out/'test_ble_origin.exe';run([cxx,'-std=c++17',*flags,'-Itests/support/receiver_public','tests/test_ble_origin.cpp',*frontend,'src/owned/LeProfiles.cpp',*voice_objects,'-o',exe],'test_ble_origin')
 for case in ['actual_loop_reconnect','copy_retry','stale_generation','byte_value_semantics','disconnect_after_admission','stopped']:run([exe,case],'test_ble_origin:'+case)
 for n in ('test_i2s_pins','test_i2s_routes'):
  exe=out/(n+'_legacy.exe');run([cxx,'-std=c++17',*flags,'-DGIGA_BREDR_I2S_WS_PIN=69','tests/'+n+'.cpp','-o',exe],n+'_legacy');run([exe],n+'_legacy')
 exe=out/'test_ble_lifecycle_facade.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_ble_lifecycle_facade.cpp',*unique([*facade,*actor]),*voice_objects,'-o',exe],'test_ble_lifecycle_facade');run([exe],'test_ble_lifecycle_facade')
 # Feature fakes declare their own voice radio; never link Unsupported here.
 real_frontend=['src/Types.cpp','src/detail/Memory.cpp','src/detail/Decoder.cpp','src/detail/Media.cpp','src/detail/PacketQueue.cpp','src/detail/AudioTiming.cpp','tests/support/clock_backend_unavailable.cpp',*VOICE_FRONTEND_SOURCES]
 for n in ['test_voice_frontend','test_voice_token_owner','test_voice_failed_begin_identity']:
  path=ROOT/'tests'/(n+'.cpp')
  if not path.is_file():continue
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),path,*real_frontend,*voice_objects,'-o',exe],n);run([exe],n)
 for n,sources,extra in [
  ('test_voice_msbc',['src/owned/VoiceMsbc.cpp','src/detail/Memory.cpp'],[]),
  ('test_voice_preemption',[*VOICE_FRONTEND_SOURCES,'src/detail/Memory.cpp'],[]),
  ('test_voice_failure_publication',[*VOICE_FRONTEND_SOURCES,'src/detail/Memory.cpp'],[]),
  ('test_voice_admission_busy',[*VOICE_FRONTEND_SOURCES,'src/detail/Memory.cpp'],[]),
  ('test_voice_input_lifetime',[],[]),
  ('test_voice_no_sbc',['src/owned/VoiceMsbc.cpp','src/owned/VoiceAudioPipeline.cpp','src/detail/Memory.cpp'],['-DGBR_ENABLE_SBC=0'])]:
  if not(ROOT/'tests'/(n+'.cpp')).is_file():continue
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*extra,'tests/'+n+'.cpp',*sources,*([]if n in ('test_voice_no_sbc','test_voice_input_lifetime')else voice_objects),'-o',exe],n);run([exe],n)
 for name,function,check_function,sources in [
  ('test_voice_worker','voiceWorkerTest','voiceWorkerChecks',[]),
  ('test_voice_dac','voiceDacTest','voiceDacChecks',[]),
  ('test_voice_pipeline','voicePipelineTest','voicePipelineChecks',[]),
  ('test_voice_input_registry','voiceInputRegistryTest','voiceInputRegistryChecks',[]),
  ('test_voice_input_padding','voiceInputPaddingTest','voiceInputPaddingChecks',[]),
  ('test_voice_rx_byte_progress','voiceRxByteProgressTest','voiceRxByteProgressChecks',[]),
  ('test_voice_sdp','voiceSdpTest','voiceSdpChecks',['src/owned/VoiceSdp.cpp'])]:
  wrapper=out/(name+'_main.cpp')
  wrapper.write_text('#include <cstdio>\nextern "C" unsigned '+function+'();\nextern "C" unsigned '+check_function+'();\nint main(){unsigned r='+function+'();std::printf("'+name+' failure=%u checks=%u\\n",r,'+check_function+'());return int(r);}\n')
  exe=out/(name+'.exe');run([cxx,'-std=c++17',*flags,'tests/'+name+'.cpp',wrapper,*VOICE_FRONTEND_SOURCES,'src/detail/Memory.cpp',*sources,*voice_objects,'-o',exe],name);run([exe],name)
 n='test_voice_rx_pacing_modes';exe=out/(n+'.exe')
 byte_sources=['src/owned/OwnedVoiceAudio.cpp','src/owned/VoiceAudioPipeline.cpp','src/owned/VoiceMsbc.cpp','src/detail/Memory.cpp']
 run([cxx,'-std=c++17',*flags,'tests/'+n+'.cpp',*byte_sources,*voice_objects,'-o',exe],n)
 for selector in ('cvsd_unfragmented','ncp_fragmented','cvsd_fragmented','msbc_fragmented'):run([exe,selector],n+':'+selector)
 run([sys.executable,'tools/test_adts_retained_prefix.py','--cc',cc,'--cxx',cxx,'--output',out/'adts-retained',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_audio_clock.py','--compiler',cxx,'--cc',cc,'--output',out/'audio-clock'])
 run([sys.executable,'tests/test_owned_wake.py','--cc',cc,'--cxx',cxx,'--output',out/'owned-wake',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_codec_memory.py','--cc',cc,'--cxx',cxx,'--output',out/'codec-memory',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_audio_scheduling.py','--compiler',cxx,'--cc',cc,'--output',out/'audio-scheduling'])
 run([sys.executable,'tools/test_owned_runner_plan.py'])
 if a.plan:
  value=dict(status='PLAN_ONLY_NO_COMMANDS_EXECUTED',root=str(ROOT),output=str(out),commands=logs,
             sanitizer_requested=dict(asan=a.asan,ubsan=a.sanitize),runtime_or_child_suites_executed=False)
  a.plan.parent.mkdir(parents=True,exist_ok=True);a.plan.write_text(json.dumps(value,indent=2)+'\n')
  (out/'receipt.json').write_text(json.dumps(value,indent=2)+'\n')
  print('PLAN_ONLY top-level compiler/runtime/child recipes; no commands executed')
 else:
  (out/'receipt.json').write_text(json.dumps(dict(status='PASS_NATIVE_SYNTHETIC_SUITES',synthetic_only=True,logs=logs),indent=2)+'\n')
  print('PASS public synthetic receiver, owned host, HCI/ATT lifecycle, PCM recovery and wake checks; no physical qualification')
if __name__=='__main__':main()
