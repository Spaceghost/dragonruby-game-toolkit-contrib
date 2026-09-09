"""Compile isolated C/Zig source mutants; unchanged tests must reject each one."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=('baseline', 'native'), default='baseline')
    args = parser.parse_args()
    tests = (ROOT / 'test.c').read_bytes()
    for language in ('c', 'zig'):
        original = ROOT.parent / 'src' / f'competitive.{language}'
        text = original.read_text()
        for name, old, new, code, diagnostic in (
            ('newline', "'\\n'", "'\\r'", 45, 'RIVAL_COUNT_MISMATCH'),
            ('star-direction', 'vx + vs', 'vx - vs', 46, 'RIVAL_STAR_MISMATCH'),
        ):
            assert old in text
            with tempfile.TemporaryDirectory(prefix='rival-mutant-') as tmp:
                folder = Path(tmp)
                mutant = folder / original.name
                mutant.write_text(text.replace(old, new))
                command = ['zig', 'build', 'build-tests', '-Doptimize=ReleaseFast',
                           f'-Dcpu={args.cpu}', f'-D{language}-source={mutant}',
                           '--prefix', str(folder / 'out')]
                built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=300)
                if built.returncode:
                    raise RuntimeError(f'Mutant must compile: {language}/{name}\n{built.stderr}')
                binary = folder / 'out/bin/rivals-check'
                result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
                candidate = 'c_tuned' if language == 'c' else 'zig_tuned'
                if result.returncode != code or not result.stderr.startswith(f'{diagnostic} candidate={candidate} '):
                    raise RuntimeError(f'Mutant not correctly detected: {language}/{name}: {result.returncode} {result.stderr}')
                print('RIVAL_MUTATION ' + json.dumps({
                    'language': language, 'mutation': name, 'compiled': True,
                    'exit_code': result.returncode, 'diagnostic': result.stderr.strip(),
                    'source_sha256': hashlib.sha256(mutant.read_bytes()).hexdigest(),
                    'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                    'tests_sha256': hashlib.sha256(tests).hexdigest(),
                }), flush=True)
            if original.read_text() != text or (ROOT / 'test.c').read_bytes() != tests:
                raise RuntimeError('Original source/tests changed during mutation checks')

if __name__ == '__main__':
    main()
