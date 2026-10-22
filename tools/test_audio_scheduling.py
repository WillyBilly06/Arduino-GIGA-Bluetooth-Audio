"""Run native audio scheduling regressions against this project's actual src.

Requires clang/clang++. ASan and UBSan are enabled. This checks actor ownership,
signaling progress, MTU updates, output-wait lifecycle, and PCM priming with
native boundary mocks. It does not validate M7 scheduling, RF, or audio fidelity.

Usage: python tools/test_audio_scheduling.py [--output PATH] [--compiler CLANGXX]
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiler(value, name, sibling=None):
    candidates = [value] if value else []
    if not value:
        if sibling:
            candidates.append(str(sibling.with_name(name + ('.exe' if os.name == 'nt' else ''))))
        candidates.append(shutil.which(name))
        if os.name == 'nt' and os.environ.get('ProgramFiles'):
            candidates.append(str(Path(os.environ['ProgramFiles']) / 'LLVM/bin' / (name + '.exe')))
    for candidate in candidates:
        if not candidate:
            continue
        found = shutil.which(str(candidate))
        path = Path(found or candidate).expanduser()
        if path.is_file():
            return path.resolve()
    raise RuntimeError(f'{name} was not found; install LLVM or specify an executable path')


def extract_write(text):
    """Keep production text verbatim while ignoring braces in comments/quotes."""
    match = re.search(r'size_t\s+i2sWrite\s*\(\s*const\s+int32_t\s*\*\s*pcm\s*,\s*size_t\s+frames\s*\)\s*\{', text)
    if not match:
        raise RuntimeError('Could not find the production I2S i2sWrite definition')
    start = match.start()
    index = match.end() - 1
    depth = 0
    state = 'code'
    while index < len(text):
        char = text[index]
        pair = text[index:index + 2]
        if state == 'line':
            if char == '\n':
                state = 'code'
        elif state == 'block':
            if pair == '*/':
                state = 'code'
                index += 1
        elif state in ('"', "'"):
            if char == '\\':
                index += 1
            elif char == state:
                state = 'code'
        elif pair == '//':
            state = 'line'
            index += 1
        elif pair == '/*':
            state = 'block'
            index += 1
        elif char in ('"', "'"):
            state = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return text[start:index + 1], text.count('\n', 0, start) + 1, text.count('\n', 0, index) + 1
        index += 1
    raise RuntimeError('Unbalanced production i2sWrite definition')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/audio-scheduling-tests')
    parser.add_argument('--compiler', help='clang++ executable; otherwise use CXX, PATH, or the Windows LLVM installation')
    parser.add_argument('--cc', help='clang executable; otherwise use CC, the clang++ directory, or PATH')
    args = parser.parse_args()
    out = args.output.resolve()
    if out == ROOT or any(out == ROOT / name or ROOT / name in out.parents for name in ('src', 'tests', 'tools', 'examples')):
        raise RuntimeError('Choose an output directory outside project source directories')
    out.mkdir(parents=True, exist_ok=True)
    cxx = compiler(args.compiler or os.environ.get('CXX'), 'clang++')
    cc = compiler(args.cc or os.environ.get('CC'), 'clang', cxx)
    env = os.environ.copy()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    flags = ['-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-sanitize=vptr', '-fno-omit-frame-pointer']
    tests = ['test_owned_host_expanded_control', 'test_owned_host_mtu_update', 'test_output_wait_lifecycle', 'test_output_priming', 'test_output_pipeline']
    test_paths = [ROOT / 'tests' / (name + '.cpp') for name in tests]
    test_paths += [ROOT / 'tests/test_owned_host.cpp', *sorted((ROOT / 'tests/support/audio_scheduler').glob('*.h')), Path(__file__).resolve()]
    source_paths = sorted(path for path in (ROOT / 'src').rglob('*') if path.is_file())
    inputs = {path.relative_to(ROOT).as_posix(): digest(path) for path in [*source_paths, *test_paths]}
    (out / 'source-before.json').write_text(json.dumps(inputs, indent=2) + '\n')
    steps = []
    executed = []
    artifacts = []
    start = time.monotonic()
    receipt = {'status': 'RUNNING', 'scope': __doc__, 'source_root': str(ROOT), 'compiler': str(cxx), 'cc': str(cc), 'asan': True, 'ubsan': True, 'source_before': inputs}

    def persist():
        (out / 'commands.json').write_text(json.dumps(steps, indent=2) + '\n')
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

    def run(label, command, timeout=120):
        command = [str(arg) for arg in command]
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            steps.append({'label': label, 'command': command, 'exit_code': result.returncode, 'output': result.stdout})
        except subprocess.TimeoutExpired as error:
            steps.append({'label': label, 'command': command, 'exit_code': None, 'timeout_seconds': timeout, 'output': str(error.stdout or '')})
            persist()
            raise RuntimeError(f'{label} timed out after {timeout} seconds') from error
        persist()
        if result.stdout:
            print(result.stdout, end='' if result.stdout.endswith('\n') else '\n', flush=True)
        if result.returncode:
            raise RuntimeError(f'{label} failed with exit code {result.returncode}; see {out / "commands.json"}')
        return result

    try:
        receipt['compiler_version'] = run('compiler-version', [cxx, '--version']).stdout
        runtime = Path(run('sanitizer-runtime', [cc, '-print-runtime-dir']).stdout.strip())
        runtime_paths = [path for path in (runtime, runtime.parent / 'windows', cxx.parent) if path.is_dir()]
        env['PATH'] = os.pathsep.join(map(str, runtime_paths)) + os.pathsep + env.get('PATH', '')
        receipt['runtime_paths'] = list(map(str, runtime_paths))
        platform = ROOT / 'src/platform/GigaI2s.cpp'
        body, first, last = extract_write(platform.read_text())
        include = out / 'i2s_write.inc'
        include.write_text(f'// BEGIN VERBATIM src/platform/GigaI2s.cpp:{first}-{last}\n{body}\n// END VERBATIM i2sWrite\n')
        receipt['extracted_i2s_write'] = {'source': 'src/platform/GigaI2s.cpp', 'source_sha256': digest(platform), 'first_line': first, 'last_line': last, 'body_sha256': hashlib.sha256(body.encode()).hexdigest(), 'include_sha256': digest(include)}
        artifacts.append(include)
        objects = []
        (out / 'objects').mkdir(exist_ok=True)
        for source in sorted((ROOT / 'src/owned/core').glob('*.c')):
            obj = out / 'objects' / (source.stem + ('.obj' if os.name == 'nt' else '.o'))
            run(source.stem + '-compile', [cc, '-std=c11', *flags, '-I' + str(ROOT / 'src/owned/core'), '-c', source, '-o', obj])
            objects.append(obj)
        receipt['compiled_c_kernels'] = len(objects)
        actor = [ROOT / 'src/owned/OwnedHost.cpp', ROOT / 'src/owned/ClassicProfiles.cpp', ROOT / 'src/owned/LeProfiles.cpp', ROOT / 'src/platform/RadioProtocol.cpp']
        for name in tests[:2]:
            exe = out / (name + ('.exe' if os.name == 'nt' else ''))
            run(name + '-compile', [cxx, '-std=c++17', *flags, '-I' + str(ROOT / 'src'), ROOT / 'tests' / (name + '.cpp'), *actor, *objects, '-o', exe])
            run(name, [exe], timeout=30)
            artifacts.append(exe)
            executed.append(name)
        frontend = [ROOT / 'src' / name for name in ('detail/Memory.cpp', 'detail/Decoder.cpp', 'detail/Media.cpp', 'detail/PacketQueue.cpp', 'detail/AudioTiming.cpp', 'Types.cpp')]
        support = ROOT / 'tests/support/audio_scheduler'
        for name in tests[2:]:
            exe = out / (name + ('.exe' if os.name == 'nt' else ''))
            actual_frontend = name != 'test_output_priming'
            extra_flags = ['-DARDUINO=1', '-I' + str(support), *(['-pthread'] if os.name != 'nt' else [])] if actual_frontend else []
            run(name + '-compile', [cxx, '-std=c++17', *flags, *extra_flags, '-I' + str(ROOT / 'src'), '-I' + str(out), ROOT / 'tests' / (name + '.cpp'), *(frontend if actual_frontend else []), '-o', exe])
            artifacts.append(exe)
            if name == 'test_output_wait_lifecycle':
                for action in ('stop', 'replace', 'same', 'shutdown', 'stall'):
                    run(name + '-' + action, [exe, action], timeout=30)
                    executed.append(name + '-' + action)
            elif name == 'test_output_pipeline':
                for codec, rate in (('lc', 8000), ('lc', 16000), ('lc', 44100), ('lc', 48000), ('he', 16000), ('he', 48000)):
                    for milliseconds in (2, 20):
                        label = f'{name}-{codec}-{rate}-{milliseconds}ms'
                        run(label, [exe, codec, milliseconds, rate, 3], timeout=30)
                        executed.append(label)
            else:
                run(name, [exe], timeout=30)
                executed.append(name)
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['status'] = 'FAIL'
        receipt['error'] = str(error)
        raise
    finally:
        after = {path: digest(ROOT / path) for path in inputs}
        changes = {path: {'before': inputs[path], 'after': after[path]} for path in inputs if inputs[path] != after[path]}
        receipt.update({'executed_cases': executed, 'passed_cases': len(executed), 'source_files_verified_unchanged': len(inputs), 'source_changes': changes, 'artifact_sha256': {str(path.relative_to(out)): digest(path) for path in artifacts if path.is_file()}, 'elapsed_seconds': time.monotonic() - start})
        if changes:
            receipt['status'] = 'FAIL'
            receipt['error'] = 'Source/test/tool inputs changed during verification'
        (out / 'source-after.json').write_text(json.dumps(after, indent=2) + '\n')
        persist()
    if receipt['status'] != 'PASS':
        raise RuntimeError(receipt.get('error', 'Audio scheduling verification failed'))
    print(f'PASS audio scheduling: {len(executed)} cases, {len(objects)} portable C kernels; source unchanged', flush=True)
    print(f'Receipt: {out / "receipt.json"}', flush=True)


if __name__ == '__main__':
    main()
