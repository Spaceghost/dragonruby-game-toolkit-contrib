"""Compile isolated C/Zig/Odin source mutants; unchanged tests must reject each one."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
SRC = ROOT.parent / 'src'

def record(language: str, name: str, mutant: Path, binary: Path, tests: bytes, result: subprocess.CompletedProcess[str]) -> None:
    print('RIVAL_MUTATION ' + json.dumps({
        'language': language, 'mutation': name, 'compiled': True,
        'exit_code': result.returncode, 'diagnostic': result.stderr.strip(),
        'source_sha256': hashlib.sha256(mutant.read_bytes()).hexdigest(),
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'tests_sha256': hashlib.sha256(tests).hexdigest(),
    }), flush=True)


def c_zig_mutations(cpu: str, tests: bytes) -> None:
    for language in ('c', 'zig'):
        original = SRC / f'competitive.{language}'
        text = original.read_text()
        vector = ("const bytes32 newline = (bytes32)'\\n';" if language == 'c'
                  else "const newline: V = @splat('\\n');")
        chunk = ('if (pairs > 255) pairs = 255;' if language == 'c'
                 else '@min((bytes.len - offset) / 64, 255)')
        for name, old, new, code, diagnostic in (
            ('vector-newline', vector, vector.replace("'\\n'", "'\\r'"), 45, 'RIVAL_COUNT_MISMATCH'),
            ('lane-overflow', chunk, chunk.replace('255', '256'), 45, 'RIVAL_COUNT_MISMATCH'),
            ('star-direction', 'vx + vs', 'vx - vs', 46, 'RIVAL_STAR_MISMATCH'),
        ):
            if text.count(old) != 1:
                raise RuntimeError(f'Mutation anchor changed: {language}/{name}')
            with tempfile.TemporaryDirectory(prefix='rival-mutant-') as tmp:
                folder = Path(tmp)
                mutant = folder / original.name
                mutant.write_text(text.replace(old, new))
                command = ['zig', 'build', 'build-tests', '-Doptimize=ReleaseFast',
                           f'-Dcpu={cpu}', f'-D{language}-source={mutant}',
                           '--prefix', str(folder / 'out')]
                built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=300)
                if built.returncode:
                    raise RuntimeError(f'Mutant must compile: {language}/{name}\n{built.stderr}')
                binary = folder / 'out/bin/rivals-check'
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
                candidate = 'c_tuned' if language == 'c' else 'zig_tuned'
                if result.returncode != code or not result.stderr.startswith(f'{diagnostic} candidate={candidate} '):
                    raise RuntimeError(f'Mutant not correctly detected: {language}/{name}: {result.returncode} {result.stderr}')
                record(language, name, mutant, binary, tests, result)
            if original.read_text() != text or (ROOT / 'test.c').read_bytes() != tests:
                raise RuntimeError('Original source/tests changed during mutation checks')


def odin_mutations(cpu: str, tests: bytes) -> None:
    original = SRC / 'competitive.odin'
    text = original.read_text()
    medium_newline = "count_medium :: proc \"contextless\" (bytes: [^]u8, length: uintptr) -> uintptr #no_bounds_check {\n\tnewline: simd.u8x32 = u8('\\n')"
    mutations = (
        ('medium-vector-newline', medium_newline, medium_newline.replace("'\\n'", "'\\r'"), 45, 'RIVAL_COUNT_MISMATCH'),
        # Drop one independently accumulated half in the long path. This is a
        # genuine observable defect, unlike a 255->256 edit that the pinned Odin
        # compiler happened to make behaviorally equivalent.
        ('drop-odd-accumulator', 'total += uintptr(ea[lane]) + uintptr(oa[lane])',
         'total += uintptr(ea[lane])', 45, 'RIVAL_COUNT_MISMATCH'),
        ('star-direction', 'nx := vx + vs', 'nx := vx - vs', 46, 'RIVAL_STAR_MISMATCH'),
    )
    for name, old, new, code, diagnostic in mutations:
        if text.count(old) != 1:
            raise RuntimeError(f'Mutation anchor changed: odin/{name}')
        with tempfile.TemporaryDirectory(prefix='rival-odin-mutant-') as tmp:
            folder = Path(tmp)
            package = folder / 'src'; package.mkdir()
            for source in SRC.glob('*.odin'):
                destination = package / source.name
                destination.write_text(text.replace(old, new) if source == original else source.read_text())
            mutant = package / original.name
            object_file = folder / 'mutant.o'
            command = ['odin', 'build', str(package), '-build-mode:obj', '-no-entry-point',
                       '-reloc-mode:pic', '-o:speed', '-no-bounds-check', f'-out:{object_file}']
            if cpu == 'native': command.append('-microarch:native')
            built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=300)
            if built.returncode:
                raise RuntimeError(f'Mutant must compile: odin/{name}\n{built.stderr}')
            out = folder / 'out'
            link = ['zig', 'build', 'build-tests', '-Doptimize=ReleaseFast', f'-Dcpu={cpu}',
                    f'-Dodin-object={object_file}', '--prefix', str(out)]
            linked = subprocess.run(link, cwd=ROOT, capture_output=True, text=True, timeout=300)
            if linked.returncode:
                raise RuntimeError(f'Mutant must link: odin/{name}\n{linked.stderr}')
            binary = out / 'bin/rivals-check'
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
            if result.returncode != code or not result.stderr.startswith(f'{diagnostic} candidate=odin_tuned '):
                raise RuntimeError(f'Mutant not correctly detected: odin/{name}: {result.returncode} {result.stderr}')
            record('odin', name, mutant, binary, tests, result)
        if original.read_text() != text or (ROOT / 'test.c').read_bytes() != tests:
            raise RuntimeError('Original source/tests changed during Odin mutation checks')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=('baseline', 'native'), default='baseline')
    args = parser.parse_args()
    tests = (ROOT / 'test.c').read_bytes()
    c_zig_mutations(args.cpu, tests)
    odin_mutations(args.cpu, tests)

if __name__ == '__main__':
    main()
