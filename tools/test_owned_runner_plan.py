"""Offline runner behavior checks; native commands are forbidden in this suite."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))

class OwnedRunnerPlanTests(unittest.TestCase):
    def plan(self,*options):
        spec=importlib.util.spec_from_file_location('owned_runner_plan_test',ROOT/'tools/run_owned_tests.py')
        runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
        with tempfile.TemporaryDirectory(prefix='giga-runner-plan-')as temporary:
            out=Path(temporary)/'output with space';plan=Path(temporary)/'plan.json'
            argv=['run_owned_tests.py','--plan',str(plan),'--output',str(out),
                  '--cc',sys.executable,'--cxx',sys.executable,*options]
            stdout=io.StringIO();stderr=io.StringIO();exit_code=0
            # Even a regression in plan dispatch must not execute a compiler,
            # project test EXE or child suite from this offline test.
            with patch.object(sys,'argv',argv),contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr),\
                 patch('subprocess.run',side_effect=AssertionError('Plan launched a command')),\
                 patch('subprocess.check_output',side_effect=AssertionError('Plan queried a compiler')):
                try:runner.main()
                except SystemExit as error:exit_code=error.code
            self.assertEqual(exit_code,0,stderr.getvalue())
            value=json.loads(plan.read_text())
            self.assertEqual(value['status'],'PLAN_ONLY_NO_COMMANDS_EXECUTED')
            self.assertIn('PLAN_ONLY',stdout.getvalue())
            self.assertNotIn('PASS public synthetic',stdout.getvalue())
            return value

    def test_plan_never_executes_native_or_child_commands(self):
        value=self.plan()
        self.assertTrue(value['commands'])
        self.assertEqual({row['kind']for row in value['commands']},
                         {'compile','project-runtime','child-suite'})
        self.assertTrue(all(row['executed']is False for row in value['commands']))

    def test_legacy_actor_and_frontend_use_actual_voice_dependencies(self):
        value=self.plan()
        def recipe(test):
            return next(row['command']for row in value['commands']
                        if row['kind']=='compile'and any(str(arg).replace('\\','/').endswith('tests/'+test+'.cpp')for arg in row['command']))
        actor=recipe('test_owned_voice')
        self.assertIn('src/owned/OwnedVoiceAudio.cpp',actor)
        self.assertIn('src/owned/VoiceSdp.cpp',actor)
        frontend=recipe('test_ble_nonblocking')
        for source in ('VoiceMsbc.cpp','VoiceAudioPipeline.cpp','VoiceAudioWorker.cpp','VoiceDacOutput.cpp','OwnedVoiceAudio.cpp'):
            self.assertIn('src/owned/'+source,frontend)
        self.assertIn('tests/support/voice_radio_unavailable.cpp',frontend)
        self.assertNotIn('src/GigaBredr.cpp',frontend) # Included by the test itself.

    def test_combined_actor_frontend_recipe_has_no_duplicate_sources(self):
        value=self.plan()
        recipe=next(row['command']for row in value['commands']
                    if row.get('case')=='test_ble_lifecycle_facade'and row['kind']=='compile')
        sources=[argument for argument in recipe if argument.endswith(('.cpp','.c'))]
        self.assertEqual(len(sources),len(set(sources)),sources)

    def test_all_historical_children_remain_planned_and_sanitizer_plan_is_pure(self):
        value=self.plan('--asan','--sanitize')
        children=[row['command']for row in value['commands']if row['kind']=='child-suite']
        names={Path(command[1]).name for command in children}
        self.assertEqual(names,{'test_adts_retained_prefix.py','test_audio_clock.py','test_owned_wake.py',
                               'test_codec_memory.py','test_audio_scheduling.py','test_owned_runner_plan.py'})
        self.assertIn('test_voice_preemption',[row['case']for row in value['commands']if row.get('case')])

    def test_historical_children_have_pure_plan_mode_and_real_dependencies(self):
        children=[('tools/test_audio_clock.py','--compiler'),
                  ('tools/test_audio_scheduling.py','--compiler'),
                  ('tools/test_codec_memory.py','--cxx'),
                  ('tests/test_owned_wake.py','--cxx')]
        for index,(name,cxx_option)in enumerate(children):
            with self.subTest(name=name),tempfile.TemporaryDirectory(prefix='giga-child-plan-')as directory:
                spec=importlib.util.spec_from_file_location('owned_child_plan_'+str(index),ROOT/name)
                child=importlib.util.module_from_spec(spec);spec.loader.exec_module(child)
                argv=[name,'--plan','--output',directory,'--cc',sys.executable,cxx_option,sys.executable]
                stdout=io.StringIO();stderr=io.StringIO()
                with patch.object(sys,'argv',argv),contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(stderr),\
                     patch('subprocess.run',side_effect=AssertionError('Child plan launched a command')),\
                     patch('subprocess.check_output',side_effect=AssertionError('Child plan queried compiler')):
                    child.main()
                receipt=json.loads((Path(directory)/'receipt.json').read_text())
                self.assertEqual(receipt['status'],'PLAN_ONLY_NO_COMMANDS_EXECUTED')
                self.assertFalse(receipt['runtime_or_sanitizers_qualified'])
                self.assertIn('PLAN_ONLY',stdout.getvalue());self.assertNotIn('PASS',stdout.getvalue())
                rows=receipt.get('records')or json.loads((Path(directory)/'commands.json').read_text())
                self.assertTrue(rows);self.assertTrue(all(row['executed']is False for row in rows))
                commands=[row['command']for row in rows]
                legacy=[cmd for cmd in commands if any(str(arg).replace('\\','/').endswith(test)for arg in cmd for test in
                        ('test_clock_frontend.cpp','test_output_wait_lifecycle.cpp','test_sbc_memory_frontend.cpp','test_receiver_wake_lifetime.cpp'))]
                self.assertTrue(legacy)
                for cmd in legacy:
                    self.assertTrue(any(str(s).replace('\\','/').endswith('src/owned/VoiceAudioWorker.cpp')for s in cmd))
                    self.assertTrue(any(str(s).replace('\\','/').endswith('tests/support/voice_radio_unavailable.cpp')for s in cmd))
                    sources=[str(s)for s in cmd if str(s).endswith(('.cpp','.c'))]
                    self.assertEqual(len(sources),len(set(sources)))

    def test_classic_progress_batches_are_registered_with_owned_actor(self):
        value=self.plan();rows=value['commands']
        for name,args in [('test_classic_progress_batch',['all']),('test_classic_ct_batch',[]),('test_classic_review_batch',[])]:
            compile_rows=[r for r in rows if r.get('case')==name and r['kind']=='compile']
            runtime_rows=[r for r in rows if r.get('case')==name and r['kind']=='project-runtime']
            self.assertEqual(len(compile_rows),1,name);self.assertEqual(len(runtime_rows),1,name)
            command=compile_rows[0]['command'];self.assertIn('tests/'+name+'.cpp',command)
            self.assertNotIn('src/owned/OwnedHost.cpp',command) # Included once by the fixture.
            self.assertIn('src/owned/ClassicProfiles.cpp',command)
            self.assertEqual(runtime_rows[0]['command'][1:],args)

    def test_all_permanent_voice_regressions_and_selectors_are_registered_once(self):
        value=self.plan();compiled={};executed={}
        for row in value['commands']:
            target=compiled if row['kind']=='compile'else executed if row['kind']=='project-runtime'else None
            if target is not None and row.get('case'):target.setdefault(row['case'],[]).append(row['command'])
        simple=['test_voice_failure_publication','test_voice_preemption','test_voice_token_owner',
                'test_voice_failed_begin_identity','test_voice_input_lifetime','test_voice_admission_busy',
                'test_sco_byte_pacing','test_voice_rx_byte_progress']
        for name in simple:
            with self.subTest(name=name):
                self.assertEqual(len(compiled.get(name,[])),1,'Missing or duplicate compile recipe '+name)
                self.assertEqual(len(executed.get(name,[])),1,'Missing or duplicate runtime recipe '+name)
        for name,selectors in [('test_hf_admission',['pending_user','pending_codec','invalid_generation']),
                               ('test_voice_rx_pacing_modes',['cvsd_unfragmented','ncp_fragmented','cvsd_fragmented','msbc_fragmented'])]:
            self.assertEqual(len(compiled.get(name,[])),1,name)
            for selector in selectors:
                key=name+':'+selector;self.assertEqual(len(executed.get(key,[])),1,key)
                self.assertEqual(executed[key][0][-1],selector)
        adapter=compiled['test_voice_rx_byte_progress'][0]
        self.assertNotIn('tests/test_owned_voice_audio.cpp',adapter) # Included by actual adapter fixture.
        self.assertIn('src/Types.cpp',compiled['test_hf_admission'][0]) # Real statusName used by exact fixture.

    def test_adts_child_plan_is_pure_and_uses_real_decoder(self):
        spec=importlib.util.spec_from_file_location('adts_child_plan',ROOT/'tools/test_adts_retained_prefix.py')
        child=importlib.util.module_from_spec(spec);spec.loader.exec_module(child)
        with tempfile.TemporaryDirectory(prefix='giga-adts-plan-') as temporary:
            argv=['test_adts_retained_prefix.py','--plan','--output',temporary,'--cc',sys.executable,'--cxx',sys.executable]
            with patch.object(sys,'argv',argv),contextlib.redirect_stdout(io.StringIO()),\
                 patch('subprocess.run',side_effect=AssertionError('ADTS plan launched a command')):
                child.main()
            value=json.loads((Path(temporary)/'receipt.json').read_text())
            self.assertEqual(value['status'],'PLAN_ONLY_NO_COMMANDS_EXECUTED')
            self.assertTrue(value['actual_aac_decoder']);self.assertFalse(value['runtime_or_sanitizers_qualified'])
            commands=[row['command'] for row in value['records']]
            self.assertTrue(all(not row['executed'] for row in value['records']))
            self.assertTrue(any(any(str(arg).endswith('src/codecs/Codecs.cpp') for arg in cmd) for cmd in commands))
            self.assertTrue(any('tests/test_adts_retained_prefix.cpp' in cmd for cmd in commands))
            self.assertTrue(any(any(str(arg).replace('\\','/').endswith('src/vendor/codecs/aac/aacdec.c') for arg in cmd) for cmd in commands))

if __name__=='__main__':unittest.main(verbosity=2)
