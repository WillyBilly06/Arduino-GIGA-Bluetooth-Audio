"""Portable synthetic owned-host checks; requires clang and clang++ on PATH."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true', help='trap undefined behavior')
    args = parser.parse_args()
    cc, cxx = shutil.which('clang'), shutil.which('clang++')
    if not cc or not cxx:
        parser.error('Install clang and clang++ and add them to PATH')
    output = ROOT / 'build/owned-tests'
    output.mkdir(parents=True, exist_ok=True)
    flags = ['-Wall', '-Wextra', '-Werror', '-g', '-Isrc', '-Isrc/owned/core']
    if args.sanitize:
        flags += ['-fsanitize=undefined', '-fno-sanitize=vptr', '-fsanitize-trap=all']

    def run(command):
        subprocess.run([str(x) for x in command], cwd=ROOT, check=True)

    objects = []
    for source in sorted((ROOT / 'src/owned/core').glob('*.c')):
        obj = output / (source.stem + '.o')
        run([cc, '-std=c11', *flags, '-c', source, '-o', obj])
        objects.append(obj)
    adapters = ['src/owned/OwnedHost.cpp', 'src/owned/ClassicProfiles.cpp',
                'src/owned/LeProfiles.cpp', 'src/platform/RadioProtocol.cpp']
    for name in ('test_owned_classic', 'test_owned_ble', 'test_owned_host',
                 'test_owned_host_review', 'test_owned_host_bootstrap', 'test_uart_tx_mailbox'):
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        run([cxx, '-std=c++17', *flags, 'tests/' + name + '.cpp', *adapters,
             *objects, '-o', exe])
        run([exe])


if __name__ == '__main__':
    main()
