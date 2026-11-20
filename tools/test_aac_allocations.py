"""Run real AAC selected-storage, retry and channel-boundary regressions.

No board, radio, phone or external compressed-media files are used. These tests
cover the enumerated allocations and elements, not complete AAC conformance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=shutil.which('clang'))
    parser.add_argument('--cxx', default=shutil.which('clang++'))
    parser.add_argument('--output', type=Path, default=ROOT / 'build/aac-allocation-tests')
    parser.add_argument('--asan', action='store_true')
    parser.add_argument('--ubsan', action='store_true')
    parser.add_argument('--plan', action='store_true', help='record recipes without executing commands')
    args = parser.parse_args()
    if not args.cc or not args.cxx:
        parser.error('clang and clang++ must be on PATH or specified explicitly')
    out = args.output.resolve()
    if out == ROOT or any(out == ROOT / n or ROOT / n in out.parents
                          for n in ('src', 'tests', 'tools', 'examples', 'docs')):
        parser.error('output must be outside the source directories')
    out.mkdir(parents=True, exist_ok=True)
    sources = sorted((ROOT / 'src/vendor/codecs/aac').glob('*.c'))
    assert sources, 'AAC sources missing'
    fixtures = {
        'selected': (['test_aac_selected_allocations.cpp', 'aac_selected_layout.cpp'],
                     [('planes',), ('partial',), ('workleak',)]),
        'retry': (['test_aac_sbr_retry.cpp', 'aac_sbr_state.cpp'],
                  [('sweep',), ('generic',), ('cpe',)]),
        'spans': (['test_aac_plane_spans.cpp'], [()]),
        'output': (['test_aac_output_channels.cpp'],
                   [('one',), ('two',), ('stereo',), ('direct',)]),
        'elements': (['test_aac_element_storage.cpp'],
                     [('adif',), ('two-sce',), ('guards',)]),
    }
    inputs = [*sources, ROOT / 'src/detail/Memory.cpp']
    inputs += sorted((ROOT / 'src/vendor/codecs/aac').glob('*.h'))
    inputs += [ROOT / 'src/detail/Memory.h', ROOT / 'src/Types.h',
               ROOT / 'src/CodecBuildConfig.h', ROOT / 'src/vendor/codecs/CodecPlatform.h']
    inputs += [ROOT / 'tests' / n for names, _ in fixtures.values() for n in names]
    pins = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    env = os.environ.copy()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    if args.asan and not args.plan:
        runtime = Path(subprocess.check_output(
            [args.cc, '-print-runtime-dir'], text=True, timeout=15).strip())
        env['PATH'] = str(runtime) + os.pathsep + str(runtime.parent / 'windows') + os.pathsep + env.get('PATH', '')
    flags = ['-O1', '-g', '-fwrapv', '-fno-strict-aliasing', '-DGBR_HOST_TEST=1',
             '-D_CRT_SECURE_NO_WARNINGS', '-I' + str(ROOT / 'src')]
    flags += ['-DGBR_ENABLE_' + name + '=' + str(int(name in ('AAC', 'AAC_HE')))
              for name in ('SBC', 'AAC', 'AAC_HE', 'APTX', 'LDAC', 'LHDC', 'OPUS')]
    if args.asan:
        flags += ['-fsanitize=address', '-fno-omit-frame-pointer']
    if args.ubsan:
        # Retained vendor fixed-point arithmetic has separately scoped legacy
        # shift/overflow exclusions. Do not describe this as full UBSan coverage.
        flags += ['-fsanitize=undefined', '-fsanitize-trap=undefined',
                  '-fno-sanitize=shift,signed-integer-overflow']
    receipt = dict(status='RUNNING', scope=__doc__, asan=args.asan,
                   ubsan=args.ubsan, ubsan_exclusions=['shift', 'signed-integer-overflow'] if args.ubsan else [],
                   source_sha256=pins, commands=[], cases=[], device_actions=False)
    began = time.monotonic()

    def run(command, kind, case=None):
        command = list(map(str, command))
        if args.plan:
            receipt['commands'].append(dict(command=command, kind=kind, case=case,
                                            returncode=None, executed=False))
            return
        result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=60)
        receipt['commands'].append(dict(command=command, kind=kind, case=case,
                                        returncode=result.returncode,
                                        stdout=result.stdout, stderr=result.stderr))
        if kind == 'test':
            receipt['cases'].append(dict(case=case, returncode=result.returncode,
                                          stdout=result.stdout, stderr=result.stderr))
        if result.returncode:
            print(result.stdout + result.stderr, end='', flush=True)
            raise RuntimeError('Command failed: ' + (case or command[0]))
        if kind == 'test':
            print(result.stdout, end='', flush=True)

    try:
        objects = []
        for index, source in enumerate(sources):
            obj = out / ('aac-' + str(index) + '.o')
            run([args.cc, '-std=c11', *flags, '-c', source, '-o', obj], 'compile')
            objects.append(obj)
        for label, (names, cases) in fixtures.items():
            exe = out / (label + ('.exe' if os.name == 'nt' else ''))
            run([args.cxx, '-std=c++17', *flags, ROOT / 'src/detail/Memory.cpp',
                 *[ROOT / 'tests' / n for n in names], *objects, '-o', exe], 'link')
            for case in cases:
                run([exe, *case], 'test', label + ':' + '/'.join(case))
        receipt.update(status='PLAN_ONLY_NO_COMMANDS_EXECUTED' if args.plan else 'PASS',
                       compiled_c_units=None if args.plan else len(sources),
                       planned_c_units=len(sources), runtime_or_sanitizers_qualified=not args.plan)
    except BaseException as error:
        receipt.update(status='FAILED_OR_INCOMPLETE', error=repr(error))
        raise
    finally:
        unchanged = all(digest(ROOT / n) == h for n, h in pins.items())
        receipt.update(source_unchanged=unchanged, elapsed_seconds=time.monotonic() - began)
        if not unchanged:
            receipt['status'] = 'FAILED_SOURCE_CHANGED'
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if args.plan:
        assert receipt['status'] == 'PLAN_ONLY_NO_COMMANDS_EXECUTED'
        print('PLAN_ONLY AAC recipes; no commands executed')
        return
    assert receipt['status'] == 'PASS'
    print('PASS AAC allocation/element suites: ' + str(len(receipt['cases'])) + ' cases')


if __name__ == '__main__':
    main()
