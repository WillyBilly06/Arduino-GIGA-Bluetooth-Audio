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
                 'test_owned_host_review', 'test_owned_host_bootstrap',
                 'test_owned_host_ble_large', 'test_owned_host_service', 'test_uart_tx_mailbox'):
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        run([cxx, '-std=c++17', *flags, 'tests/' + name + '.cpp', *adapters,
             *objects, '-o', exe])
        run([exe])
    # Separate link sets keep the receiver's fake codec/radio/output backend
    # independent from the actual owned protocol adapters and vendor decoders.
    portable = {
        'test_audio_timing': ['src/detail/AudioTiming.cpp'],
        'test_rtp_discontinuity': ['src/detail/AudioTiming.cpp'],
        'test_receiver': ['tests/empty_registry.cpp', 'src/GigaBredr.cpp',
                          'src/detail/Memory.cpp', 'src/detail/Decoder.cpp',
                          'src/detail/Media.cpp', 'src/detail/PacketQueue.cpp',
                          'src/detail/AudioTiming.cpp'],
        'test_i2s': [], 'test_i2s_thread': [], 'test_i2s_priming': [],
        'test_i2s_pins': [], 'test_i2s_runtime': ['src/platform/GigaI2s.cpp'],
    }
    for name, sources in portable.items():
        exe = output / (name + ('.exe' if os.name == 'nt' else ''))
        thread_flags = ['-pthread'] if os.name != 'nt' else []
        run([cxx, '-std=c++17', *flags, *thread_flags, 'tests/' + name + '.cpp',
             *sources, '-o', exe])
        run([exe])
    legacy = output / ('test_i2s_pins_legacy' + ('.exe' if os.name == 'nt' else ''))
    run([cxx, '-std=c++17', *flags, '-DGIGA_BREDR_I2S_WS_PIN=69',
         'tests/test_i2s_pins.cpp', '-o', legacy])
    run([legacy])


if __name__ == '__main__':
    main()
