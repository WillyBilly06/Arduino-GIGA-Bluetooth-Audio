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
 flags=['-Wall','-Wextra','-Werror','-g','-D_CRT_SECURE_NO_WARNINGS','-Isrc','-Isrc/owned/core']
 if a.sanitize:flags+=['-fsanitize=undefined','-fno-sanitize=vptr','-fsanitize-trap=all']
 if a.asan:
  flags+=['-fsanitize=address','-fno-omit-frame-pointer']
  if not a.plan:
   rt=Path(subprocess.check_output([cc,'-print-runtime-dir'],text=True).strip());env['PATH']=str(rt)+os.pathsep+str(rt.parent/'windows')+os.pathsep+env.get('PATH','')
 logs=[]
 def run(cmd,case=None):
  cmd=list(map(str,cmd))
  # MSVC's standard library rejects keyword macros used by private-state
  # fixtures unless its standard header guard was entered beforehand.
  if os.name=='nt'and cmd[0]==str(cxx)and any(arg.startswith('-std=c++')for arg in cmd):cmd[1:1]=['-include','cstddef']
  kind='child-suite'if cmd[0]==sys.executable and len(cmd)>1 and cmd[1].endswith('.py')else'compile'if cmd[0]in(str(cc),str(cxx))else'project-runtime'
  if a.plan:
   row=dict(command=cmd,kind=kind,executed=False,exit=None)
   if case:row['case']=case
   logs.append(row);return
  r=subprocess.run(cmd,cwd=ROOT,env=env,text=True,capture_output=True);print(r.stdout+r.stderr,end='',flush=True)
  logs.append(dict(command=cmd,kind=kind,executed=True,exit=r.returncode,stdout=r.stdout,stderr=r.stderr))
  (out/'receipt.json').write_text(json.dumps(dict(synthetic_only=True,status='FAILED_NATIVE_TESTS'if r.returncode else'RUNNING_NATIVE_TESTS',logs=logs),indent=2)+'\n')
  r.check_returncode()
 run([cxx,'-std=c++17',*flags,'-c','tests/test_owned_uart_baud_policy.cpp','-o',out/'uart-baud-policy.o'])
 objects=[];sbc_objects=[]
 for collection,sources in [(objects,sorted((ROOT/'src/owned/core').glob('*.c'))),(sbc_objects,voice_sbc_sources(ROOT))]:
  for s in sources:
   obj=out/('c'+str(len(objects)+len(sbc_objects))+'.o');run([cc,'-std=c11',*flags,'-c',s,'-o',obj]);collection.append(obj)
 voice_objects=[*objects,*sbc_objects]
 # Actual shared controller and PLL-quantizer model; these are synthetic
 # rate/cohort checks, not a physical clock or decoder qualification.
 for n in ('test_clock_relearn','test_clock_drift_plant'):
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,'-O2','tests/'+n+'.cpp','-o',exe],n);run([exe],n)
 for n in ['test_rfcomm','test_hfp_control','test_profile_expiry','test_voice_profiles','test_spp','test_obex_profiles']:
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,'-DGIGA_BREDR_PORTABLE_TEST_MAIN=1','tests/'+n+'.cpp','src/owned/VoiceProfiles.cpp','src/owned/HfpControl.cpp','src/owned/HspControl.cpp',*objects,'-o',exe],n);run([exe],n)
 # HID Device endpoint: exact SDP records, descriptor layouts, channel/queue state.
 n='test_hid';exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,'tests/'+n+'.cpp',*objects,'-o',exe],n);run([exe],n)
 # Portable OBEX packet codec and SDP-client parser: the C core alone.
 n='test_obex_codec';exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,'tests/'+n+'.cpp',*objects,'-o',exe],n);run([exe],n)
 n='test_hfp_gain_error';exe=out/(n+'.exe')
 run([cxx,'-std=c++17',*flags,'tests/'+n+'.cpp','src/owned/HfpControl.cpp','src/owned/HspControl.cpp','-o',exe],n);run([exe],n)
 actor=list(ACTOR_SOURCES)
 # Real Service Changed actor and independent LeProfiles/ATT regressions.
 for n in ('test_gatt_service_changed_integrated','test_gatt_service_changed_review'):
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,'tests/'+n+'.cpp','src/owned/LeProfiles.cpp',*objects,'-o',exe],n);run([exe],n)
 n='test_actor_service_changed';exe=out/(n+'.exe')
 run([cxx,'-std=c++17',*flags,'-Itests','tests/'+n+'.cpp',*actor,*objects,'-o',exe],n);run([exe],n)
 n='test_gatt_voice_coexistence';exe=out/(n+'.exe')
 run([cxx,'-std=c++17',*flags,'-Itests','tests/'+n+'.cpp',*actor,*objects,'-o',exe],n);run([exe],n)
 for n in ['test_single_hnc','test_single_signal','test_ble_audio_qos','test_le_connection_preference','test_le_transport','test_le_control','test_sco_owner','test_sco_setup','test_sco_response_validation','test_sco_byte_pacing','test_normal_ssp_review','test_nonbond_late_auth']:
  exe=out/(n+'.exe');extra=[]
  core_entry={'test_sco_owner':'scoOwner','test_sco_setup':'scoSetup','test_sco_byte_pacing':'scoBytePacing'}
  if n in core_entry:
   stem=core_entry[n];wrapper=out/(n+'_main.c')
   wrapper.write_text('#include <stdio.h>\nunsigned '+stem+'Selftest(void);\nunsigned '+stem+'Checks(void);\nint main(void){unsigned r='+stem+'Selftest();printf("'+n+' failure=%u checks=%u\\n",r,'+stem+'Checks());return (int)r;}\n')
   extra=[wrapper]
  run([cc,'-std=c11',*flags,'tests/owned_core/'+n+'.c',*extra,*objects,'-o',exe],n);run([exe],n)
 units=['test_hsp_actor','test_spp_actor','test_hid_actor','test_obex_actor','test_owned_voice','test_avrcp_controls','test_avrcp_channel_retirement','test_avrcp_owner_retirement','test_avrcp_stale_disconnect','test_owned_classic','test_owned_ble','test_owned_host','test_classic_bond_provider_host','test_normal_ssp_host','test_owned_host_review','test_owned_host_bootstrap','test_owned_host_ble_large','test_owned_host_service','test_owned_host_single_hnc','test_owned_host_hnc','test_owned_host_benign','test_owned_host_le_requests','test_owned_host_signaling','test_owned_host_ble_commands','test_owned_host_ble_unsubscribe','test_owned_host_config_reject','test_owned_host_abort','test_owned_host_scheduling','test_owned_host_classic_combined','test_owned_host_mixed_att','test_uart_tx_mailbox','test_ble_audio_qos','test_ble_connection_preference','test_owned_ble_lifecycle','test_owned_voice_transport']
 for n in units:
  sources=actor[1:]if n in ('test_hsp_actor','test_spp_actor','test_hid_actor','test_obex_actor','test_owned_voice','test_avrcp_stale_disconnect','test_owned_host_scheduling','test_owned_host_classic_combined')else actor
  extra=['-DGBR_QOS_CANDIDATE=1']if n=='test_ble_audio_qos'else[]
  arguments=['all']if n in ('test_ble_audio_qos','test_ble_connection_preference')else[]
  exe=out/(n+'.exe');run([cxx,'-std=c++17',*flags,*extra,'-Itests','tests/'+n+'.cpp',*sources,*objects,'-o',exe],n);run([exe,*arguments],n)
 n='test_avrcp_status_reconcile';exe=out/(n+'.exe')
 run([cxx,'-std=c++17',*flags,'-Itests','tests/'+n+'.cpp','src/owned/ClassicProfiles.cpp','src/platform/RadioProtocol.cpp',*objects,'-o',exe],n);run([exe],n)
 for n in ('test_classic_bond_public_header','test_classic_bond_store_noalloc','test_receiver_profile_log'):
  exe=out/(n+'.exe');extra=['-Itests/support/profile_log'] if n=='test_receiver_profile_log' else []
  run([cxx,'-std=c++17',*flags,*extra,'tests/'+n+'.cpp','-o',exe],n);run([exe],n)
 # The three actor batches inline the real OwnedHost exactly once and retain
 # the complete target, matched-controller and wrap/ownership selectors.
 for name,args in [('test_classic_progress_batch',['all']),('test_classic_ct_batch',[]),('test_classic_review_batch',[])]:
  exe=out/(name+'.exe');run([cxx,'-std=c++17',*flags,'-Itests','tests/'+name+'.cpp','src/Types.cpp',*actor[1:],*objects,'-o',exe],name);run([exe,*args],name)
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
 exe=out/'test_avrcp_facade.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_avrcp_facade.cpp',*facade,*voice_objects,'-o',exe],'test_avrcp_facade');run([exe],'test_avrcp_facade')
 exe=out/'test_ble_nonblocking.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_ble_nonblocking.cpp',*facade,*voice_objects,'-o',exe],'test_ble_nonblocking');run([exe],'test_ble_nonblocking')
 exe=out/'test_ble_origin.exe';run([cxx,'-std=c++17',*flags,'-Itests/support/receiver_public','tests/test_ble_origin.cpp',*frontend,'src/owned/LeProfiles.cpp',*voice_objects,'-o',exe],'test_ble_origin')
 for case in ['actual_loop_reconnect','copy_retry','stale_generation','byte_value_semantics','disconnect_after_admission','stopped']:run([exe,case],'test_ble_origin:'+case)
 for n in ('test_i2s_pins','test_i2s_routes'):
  exe=out/(n+'_legacy.exe');run([cxx,'-std=c++17',*flags,'-DGIGA_BREDR_I2S_WS_PIN=69','tests/'+n+'.cpp','-o',exe],n+'_legacy');run([exe],n+'_legacy')
 exe=out/'test_ble_lifecycle_facade.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_ble_lifecycle_facade.cpp',*unique([*facade,*actor]),*voice_objects,'-o',exe],'test_ble_lifecycle_facade');run([exe],'test_ble_lifecycle_facade')
 # Public SPP facade over the real actor/RFCOMM owner (TryLock, lazy storage).
 exe=out/'test_spp_facade.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_spp_facade.cpp',*unique([*facade,*actor]),*voice_objects,'-o',exe],'test_spp_facade');run([exe],'test_spp_facade')
 # Public HID facade over the real actor (TryLock, lazy storage, poll() callback).
 exe=out/'test_hid_facade.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_hid_facade.cpp',*unique([*facade,*actor]),*voice_objects,'-o',exe],'test_hid_facade');run([exe],'test_hid_facade')
 # Public OBEX (OPP/PBAP) facade over the real actor/RFCOMM owner.
 exe=out/'test_obex_facade.exe';run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'tests/test_obex_facade.cpp',*unique([*facade,*actor]),*voice_objects,'-o',exe],'test_obex_facade');run([exe],'test_obex_facade')
 n='test_classic_bond_provider_facade';exe=out/(n+'.exe')
 run([cxx,'-std=c++17',*flags,*(['-pthread']if os.name!='nt'else[]),'-Itests','tests/'+n+'.cpp',*unique([*facade,*actor]),*voice_objects,'-o',exe],n);run([exe],n)
 # Feature fakes declare their own voice radio; never link Unsupported here.
 real_frontend=['src/Types.cpp','src/detail/Memory.cpp','src/detail/Decoder.cpp','src/detail/Media.cpp','src/detail/PacketQueue.cpp','src/detail/AudioTiming.cpp','tests/support/clock_backend_unavailable.cpp',*VOICE_FRONTEND_SOURCES]
 for n in ['test_hsp_facade','test_voice_frontend','test_voice_token_owner','test_voice_failed_begin_identity']:
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
 # Compare actual output with the frozen dev92 -fwrapv baseline, not a
 # second copy of the candidate implementation. Hashes are from dev386.
 verify='import hashlib,sys; h=hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest(); print(h); sys.exit(h!=sys.argv[2])'
 n='test_sbc_boundaries';exe=out/(n+'.exe');data=out/(n+'.bin')
 run([cc,'-std=c11',*flags,'tests/'+n+'.c','src/vendor/codecs/sbc/srce/synthesis-dct8.c','src/vendor/codecs/sbc/srce/synthesis-8-generated.c','-o',exe],n)
 run([exe,data],n);run([sys.executable,'-c',verify,data,'39859e3014bfcafe33b586ca7d712a387e7fadd932264a977e88bedacceab381'],n+':baseline-digest')
 n='test_bitreader_valid';exe=out/(n+'.exe')
 run([cc,'-std=c11',*flags,'tests/'+n+'.c','src/vendor/codecs/aac/bitstream.c','-o',exe],n);run([exe],n)
 # AAC fixtures use the actual decoder and include their numerical target
 # sources directly; omit those source objects only in the corresponding link.
 # Match the independently validated donor warning policy for legacy AAC;
 # sanitizer flags and trap behavior remain inherited from the runner.
 aac_flags=[*[f for f in flags if f!='-Werror'],'-Wno-unused-function','-Wno-unused-but-set-variable','-DARDUINO=1','-Itests']
 aac_objects={}
 for source in sorted((ROOT/'src/vendor/codecs/aac').glob('*.c')):
  obj=out/('aac-'+source.stem+'.o');run([cc,'-std=c11',*aac_flags,'-c',source,'-o',obj]);aac_objects[source.stem]=obj
 for n,excluded in [('test_huffman_fields',('huffman',)),('test_stereo_pns_numeric',('stproc','pns'))]:
  obj=out/(n+'.o');exe=out/(n+'.exe')
  run([cc,'-std=c11',*aac_flags,'-c','tests/'+n+'.c','-o',obj],n+'_object')
  run([cxx,'-std=c++17',*aac_flags,'src/detail/Memory.cpp',obj,*[p for k,p in aac_objects.items() if k not in excluded],'-o',exe],n)
  if n=='test_huffman_fields':run([exe],n)
  else:
   data=out/(n+'.bin');run([exe,data],n)
   run([sys.executable,'-c',verify,data,'ba44af81091b20fd4362f128fe7c4a24516615914f208d83b83fa1d16726bf50'],n+':baseline-digest')
 n='test_aac_valid_pcm';exe=out/(n+'.exe');data=out/(n+'.pcm')
 run([cxx,'-std=c++17',*aac_flags,'tests/'+n+'.cpp','src/detail/Memory.cpp',*aac_objects.values(),'-o',exe],n)
 run([exe,data],n);run([sys.executable,'-c',verify,data,'a35fe113ea564b82d428ff9b20710089be73361b6d7b843eeaab1e9b1013efe7'],n+':baseline-digest')
 n='test_msbc_pcm';exe=out/(n+'.exe');frames=out/'msbc-frames.bin';pcm=out/'msbc-roundtrip.pcm';boundary=out/'msbc-boundary.bin'
 run([cxx,'-std=c++17',*flags,'-Itests','tests/'+n+'.cpp','src/owned/VoiceMsbc.cpp','src/detail/Memory.cpp',*voice_objects,'-o',exe],n)
 for mode,source,target,digest in [
  ('encode',frames,'unused','59396be28fe35bea2dc5b822d6ec01b6c7c283c43583515fda4cd6b464468bb3'),
  ('decode',frames,pcm,'c83c5156ed846b2ceb9c8822e7da6f27e4128c612f1b60f2b5691f0c22f32a1f'),
  ('boundary-encode',boundary,'unused','25fa7960011de58c5a7b065bd763b8ec36a1740fcf728c87f333cbc556d6587c')]:
  run([exe,mode,source,target],n+':'+mode)
  run([sys.executable,'-c',verify,pcm if mode=='decode' else source,digest],n+':'+mode+'-baseline-digest')
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
 for selector in ('cvsd_unfragmented','ncp_fragmented','cvsd_fragmented','msbc_fragmented','disabled_no_flow'):run([exe,selector],n+':'+selector)
 run([sys.executable,'tools/test_adts_retained_prefix.py','--cc',cc,'--cxx',cxx,'--output',out/'adts-retained',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_audio_clock.py','--compiler',cxx,'--cc',cc,'--output',out/'audio-clock'])
 run([sys.executable,'tests/test_owned_wake.py','--cc',cc,'--cxx',cxx,'--output',out/'owned-wake',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_codec_memory.py','--cc',cc,'--cxx',cxx,'--output',out/'codec-memory',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_aac_allocations.py','--cc',cc,'--cxx',cxx,'--output',out/'aac-allocations',*(['--ubsan']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_audio_scheduling.py','--compiler',cxx,'--cc',cc,'--output',out/'audio-scheduling'])
 run([sys.executable,'tools/test_owned_runner_plan.py'])
 run([sys.executable,'tools/test_lhdc_aosp.py','--cc',cc,'--cxx',cxx,'--output',out/'lhdc-aosp',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
 run([sys.executable,'tools/test_ssc.py','--cc',cc,'--cxx',cxx,'--output',out/'ssc',*(['--sanitize']if a.sanitize else[]),*(['--asan']if a.asan else[])])
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
