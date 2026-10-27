"""Root-only official ARM compile assertions. Never links or executes a fixture.

Default: read-only plan. --prepare: fresh source/pins/receipt, no process.
--compile --guard PATH: fresh attempt, pinned owned-job guard, two ARM layouts.
Scalar compile assertions do not prove threads, target timing or publication.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import re
import sys
import time
import uuid

sys.dont_write_bytecode = True
COMPILER = Path(r'C:/Users/ngocv/AppData/Local/Arduino15/packages/arduino/tools/arm-none-eabi-gcc/7-2017q4/bin/arm-none-eabi-g++.exe')
COMPILER_SHA256 = '0b007086d0b2e4a403440c09a907dd9f5da296a4163378d2cd2e85e35d8c13b5'
GUARD_SHA256 = 'ca6303777db7187fb206450684b1ea99263f4ef964517732fc70176b50aa9942'
INPUTS = (
    'tests/test_audio_supply_witness.cpp',
    'tools/check_audio_supply_witness.py',
    'src/platform/AudioSupplyWitness.h',
    'src/platform/AudioSupplyWitness.cpp',
)

FLAGS = ['-std=gnu++14', '-Os', '-mcpu=cortex-m7', '-mthumb',
         '-mfloat-abi=softfp', '-mfpu=fpv5-d16', '-Wall', '-Wextra', '-Werror']
SCOPE = 'ARM compile-only supply witness scalar/layout contracts; no link, native run, target execution, concurrency or timing proof'


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def source_pins(root):
    # Explicitly pin absence for the legitimate initial missing-header RED.
    return {name: digest(root / name) if (root / name).is_file() else None for name in INPUTS}


def write_json(file, value):
    with file.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')


def plan(root, label):
    return dict(source_root=str(root), label=label, scope=SCOPE,
                compiler=str(COMPILER), compiler_expected_sha256=COMPILER_SHA256,
                guard_expected_sha256=GUARD_SHA256, timeout_seconds_per_layout=600,
                flags=FLAGS, private_scheduler_layouts=[0, 1], source_inputs=list(INPUTS),
                artifact_parent=str(root / 'build'), creates_new_label_uuid_directory=True,
                default_action='READ_ONLY_PLAN_NO_PROCESS',
                initial_missing_header='expected legitimate RED only after Root observes compiler output')


def prepare(root, label, guard):
    attempt = root / 'build' / ('audio-supply-witness-' + label + '-' + uuid.uuid4().hex)
    attempt.mkdir(parents=True, exist_ok=False)
    before = source_pins(root)
    pins = dict(source_root=str(root), source_sha256=before,
                compiler=dict(path=str(COMPILER), expected_sha256=COMPILER_SHA256,
                              actual_sha256=digest(COMPILER)),
                guard=dict(path=str(guard) if guard else None, expected_sha256=GUARD_SHA256,
                           actual_sha256=digest(guard) if guard else None))
    write_json(attempt / 'pins.json', pins)
    source = attempt / 'source'
    source.mkdir(exist_ok=False)
    for name, expected in before.items():
        if expected is not None:
            destination = source / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            data = (root / name).read_bytes()
            if hashlib.sha256(data).hexdigest() != expected:
                raise RuntimeError('source changed while preparing: ' + name)
            destination.write_bytes(data)
    if source_pins(root) != before:
        raise RuntimeError('source changed while preparing')
    write_json(attempt / 'preparation-receipt.json',
               dict(status='PREPARED_NO_COMPILER_RUN', scope=SCOPE, source_sha256=before,
                    pins_sha256=digest(attempt / 'pins.json'), missing=[n for n, h in before.items() if h is None]))
    return attempt, pins


def compile_attempt(root, label, guard):
    attempt, pins = prepare(root, label, guard)
    receipt = dict(status='FAILED', scope=SCOPE, source_root=str(root), label=label,
                   attempt=str(attempt), pins_sha256=digest(attempt / 'pins.json'),
                   source_before_sha256=pins['source_sha256'], compiler=pins['compiler'],
                   guard=pins['guard'], layouts=[], errors=[],
                   target_execution='NONE', object_execution='NONE', owned_job_quiescent=True)
    started = time.monotonic()
    try:
        if pins['compiler']['actual_sha256'] != COMPILER_SHA256:
            raise RuntimeError('official ARM compiler SHA256 mismatch')
        if pins['guard']['actual_sha256'] != GUARD_SHA256:
            raise RuntimeError('owned-job guard SHA256 mismatch')
        if guard == Path(__file__).resolve() or guard.is_relative_to(root / 'src'):
            raise RuntimeError('guard must be the external pinned Root-owned guard')
        # Only this pinned guard is imported; no source helper or native runner.
        spec = importlib.util.spec_from_file_location('audio_supply_owned_guard_' + uuid.uuid4().hex, guard)
        if spec is None or spec.loader is None:
            raise RuntimeError('cannot load pinned guard')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        if digest(guard) != GUARD_SHA256:
            raise RuntimeError('guard changed during import')
        for scheduler in (0, 1):
            if source_pins(root) != pins['source_sha256']:
                raise RuntimeError('source changed before compiler invocation')
            if digest(COMPILER) != COMPILER_SHA256 or digest(guard) != GUARD_SHA256:
                raise RuntimeError('compiler/guard changed before compiler invocation')
            output = attempt / ('scheduler-' + str(scheduler))
            output.mkdir(exist_ok=False)
            obj = output / 'test_audio_supply_witness.o'
            defines = ['-DGIGA_BREDR_PRIVATE_SCHED_WITNESS=' + str(scheduler)]
            if scheduler:
                defines += ['-DARDUINO_GIGA=1', '-DCORE_CM7=1']
            command = [str(COMPILER), *FLAGS, *defines, '-I', str(root / 'src'),
                       '-c', str(root / 'tests/test_audio_supply_witness.cpp'), '-o', str(obj)]
            step = dict(scheduler=scheduler, command=command, timeout_seconds=600,
                        source_before_sha256=source_pins(root), returncode=None,
                        owned_job_quiescent=False, errors=[])
            stdout, stderr = '', ''
            receipt['layouts'].append(step)
            step_started = time.monotonic()
            try:
                result = module.run(command, cwd=root, timeout=600)
                stdout, stderr = result.stdout, result.stderr
                step['returncode'] = result.returncode
                step['owned_job_quiescent'] = bool(getattr(result, 'owned_job_quiescent', False))
            except BaseException as error:
                stdout = getattr(error, 'stdout', '') or getattr(error, 'output', '') or ''
                stderr = getattr(error, 'stderr', '') or ''
                step['errors'].append(repr(error))
                step['owned_job_quiescent'] = bool(getattr(error, 'owned_job_quiescent', False))
            if isinstance(stdout, bytes):
                stdout = stdout.decode('utf-8', errors='backslashreplace')
            if isinstance(stderr, bytes):
                stderr = stderr.decode('utf-8', errors='backslashreplace')
            (output / 'stdout.txt').write_text(stdout, encoding='utf-8', newline='\n')
            (output / 'stderr.txt').write_text(stderr, encoding='utf-8', newline='\n')
            step.update(seconds=time.monotonic() - step_started,
                        stdout_sha256=digest(output / 'stdout.txt'), stderr_sha256=digest(output / 'stderr.txt'),
                        source_after_sha256=source_pins(root),
                        compiler_after_sha256=digest(COMPILER), guard_after_sha256=digest(guard),
                        object_sha256=digest(obj) if obj.is_file() else None)
            if step['source_after_sha256'] != step['source_before_sha256']:
                step['errors'].append('source changed during compile; attempt invalid')
            if step['compiler_after_sha256'] != COMPILER_SHA256 or step['guard_after_sha256'] != GUARD_SHA256:
                step['errors'].append('compiler/guard changed during compile; attempt invalid')
            if step['returncode'] == 0 and step['object_sha256'] is None:
                step['errors'].append('compiler returned zero without object')
            step['passed'] = step['returncode'] == 0 and step['owned_job_quiescent'] and not step['errors']
            write_json(output / 'receipt.json', step)
            if not step['owned_job_quiescent']:
                receipt['owned_job_quiescent'] = False
                raise RuntimeError('owned compiler job not confirmed quiescent; no next layout')
            if step['errors']:
                raise RuntimeError('invalid/exceptional compile attempt; no retry or next layout')
            # A nonzero compiler diagnostic is retained; the other planned layout
            # is a distinct check, never a retry with changed flags.
        receipt['source_after_sha256'] = source_pins(root)
        if receipt['source_after_sha256'] != pins['source_sha256']:
            raise RuntimeError('source changed across attempt; no pass')
        if len(receipt['layouts']) == 2 and all(s['passed'] for s in receipt['layouts']):
            receipt['status'] = 'PASS_ARM_COMPILE_ONLY'
        else:
            receipt['errors'].append('one or both planned ARM layouts failed; retained without retry')
    except BaseException as error:
        receipt['errors'].append(repr(error))
    finally:
        receipt['seconds'] = time.monotonic() - started
        write_json(attempt / 'receipt.json', receipt)
    print(json.dumps(receipt, indent=2))
    return 0 if receipt['status'] == 'PASS_ARM_COMPILE_ONLY' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--label', required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--prepare', action='store_true')
    mode.add_argument('--compile', action='store_true')
    parser.add_argument('--guard', type=Path)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}', args.label):
        parser.error('label must be 1..64 safe filename characters, starting with a letter/digit')
    root = args.source_root.resolve(strict=True)
    if not root.is_dir() or Path(__file__).resolve() != root / 'tools/check_audio_supply_witness.py':
        parser.error('invoke the runner belonging to the requested source root')
    if args.compile != (args.guard is not None):
        parser.error('--guard is required for --compile and permitted only with --compile')
    if args.compile:
        return compile_attempt(root, args.label, args.guard.resolve(strict=True))
    if args.prepare:
        attempt, pins = prepare(root, args.label, None)
        prepared = dict(status='PREPARED_NO_COMPILER_RUN', attempt=str(attempt), pins=pins, scope=SCOPE,
                        preparation_receipt_sha256=digest(attempt / 'preparation-receipt.json'))
        write_json(attempt / 'receipt.json', prepared)
        print(json.dumps(prepared, indent=2))
    else:
        print(json.dumps(plan(root, args.label), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
