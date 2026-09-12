"""Execute native evidence and reject surviving, unbuildable or crashing mutants.

Only temporary candidate copies are changed. Test/oracle files stay identical.
This is finite regression evidence, not formal verification or exhaustive fuzzing.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

SUITE = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=240)


def build(suite: Path, output: Path, mode: str, cpu: str) -> None:
    args = ['zig', 'build', '--build-file', str(suite / 'evidence/build.zig'),
            f'-Doptimize={mode}', f'-Dcpu={cpu}', '--prefix', str(output), '--summary', 'all']
    result = command(args, suite)
    if result.returncode:
        print(result.stdout, end='')
        print(result.stderr, end='')
        raise SystemExit('Evidence build failed; compilation failure is not a killed mutation')


def execute(output: Path, name: str, suite: Path) -> subprocess.CompletedProcess[str]:
    return command([str(output / 'bin' / name)], suite)


def copy_inputs(destination: Path) -> Path:
    suite = destination / '12_zig_suite'
    for folder in ('src', 'evidence'):
        shutil.copytree(SUITE / folder, suite / folder,
                        ignore=shutil.ignore_patterns('.zig-cache', 'zig-out', '__pycache__'))
    for relative in ('02_intermediate/app/re.c', '02_intermediate/app/re.h',
                     '11_zig_native_pixel_arrays/tests/support/guard.h'):
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(SUITE.parent / relative, target)
    return suite


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpu', choices=('native', 'baseline'), default='baseline')
    parser.add_argument('--mode', action='append', choices=('Debug', 'ReleaseSafe', 'ReleaseFast'))
    parser.add_argument('--skip-mutations', action='store_true')
    args = parser.parse_args()
    sources = [p for folder in ('src', 'evidence') for p in (SUITE / folder).iterdir()
               if p.is_file() and p.suffix in ('.zig', '.c', '.h')]
    sources += [SUITE.parent / p for p in ('02_intermediate/app/re.c', '02_intermediate/app/re.h',
                '11_zig_native_pixel_arrays/tests/support/guard.h')]
    before = {str(p.relative_to(SUITE.parent)): digest(p) for p in sources}
    for relative, expected in {
        '02_intermediate/app/re.c': '2d64d9f3e941af2a670a4176615f71173a33fb92',
        '02_intermediate/app/re.h': 'fb2d0028f039138701e2d9bce2a39a4c2d54e426',
    }.items():
        data = (SUITE.parent / relative).read_bytes()
        actual = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        if actual != expected:
            raise SystemExit(f'Original oracle pin changed: {relative}')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SUITE, text=True).strip()
    print('SOURCE_EVIDENCE ' + json.dumps({'commit': commit, 'cpu_target': args.cpu,
          'zig': subprocess.check_output(['zig', 'version'], text=True).strip(),
          'sha256': before}, sort_keys=True), flush=True)
    try:
        with tempfile.TemporaryDirectory(prefix='drbz-evidence-') as tmp:
            work = Path(tmp)
            for mode in args.mode or ['Debug', 'ReleaseSafe', 'ReleaseFast']:
                output = work / ('normal-' + mode)
                build(SUITE, output, mode, args.cpu)
                for name, marker in (('drbz-regex-long', 'REGEX_LONG_EVIDENCE '),
                                     ('drbz-allocations', 'ALLOCATION_EVIDENCE ')):
                    result = execute(output, name, SUITE)
                    print(result.stdout, end='', flush=True)
                    print(result.stderr, end='', flush=True)
                    records = [line for line in result.stdout.splitlines() if line.startswith(marker)]
                    if result.returncode != 0 or len(records) != 1:
                        raise SystemExit(f'Positive evidence failed: {mode}/{name}')
                    print('EXECUTION_EVIDENCE ' + json.dumps({'mode': mode, 'executable': name,
                          'sha256': digest(output / 'bin' / name), 'exit_code': result.returncode}), flush=True)
            if args.skip_mutations:
                return
            mutations = [
                ('regex-last-lane', 'regex.zig', '@ctz(bits)', '15 - @clz(bits)',
                 'drbz-regex-long', 43, 'REGEX_LONG_MISMATCH '),
                ('newline-wrong-byte', 'counter.zig', "const newline: V = @splat('\\n');",
                 "const newline: V = @splat('\\r');", 'drbz-allocations', 43, 'KERNEL_MISMATCH newline counter'),
                ('star-subtraction', 'native.zig', 'const nx = vx + vs;', 'const nx = vx - vs;',
                 'drbz-allocations', 43, 'KERNEL_MISMATCH star'),
                ('hidden-allocation', 'native.zig',
                 'export fn drbz_count_blocked(bytes: [*c]const u8, len: usize) usize {',
                 'export fn drbz_count_blocked(bytes: [*c]const u8, len: usize) usize {\n'
                 '    const allocation = std.heap.c_allocator.create(u8) catch unreachable;\n'
                 '    std.mem.doNotOptimizeAway(allocation);\n'
                 '    std.heap.c_allocator.destroy(allocation);',
                 'drbz-allocations', 44, 'ALLOCATION_MISMATCH '),
            ]
            for name, source, needle, replacement, executable, code, marker in mutations:
                candidate = copy_inputs(work / name)
                path = candidate / 'src' / source
                original = path.read_text()
                if original.count(needle) != 1:
                    raise SystemExit(f'Mutation anchor changed: {name}; review the operator')
                path.write_text(original.replace(needle, replacement))
                output = work / (name + '-output')
                build(candidate, output, 'ReleaseFast', args.cpu)
                result = execute(output, executable, candidate)
                if result.returncode != code or not result.stderr.startswith(marker):
                    print(result.stdout, end='')
                    print(result.stderr, end='')
                    raise SystemExit(f'Mutation not detected by its intended assertion: {name}, exit={result.returncode}')
                print('MUTATION_EVIDENCE ' + json.dumps({'name': name, 'source': source,
                      'original_sha256': hashlib.sha256(original.encode()).hexdigest(),
                      'mutant_sha256': digest(path), 'executable_sha256': digest(output / 'bin' / executable),
                      'mode': 'ReleaseFast', 'compiled': True, 'exit_code': result.returncode,
                      'diagnostic': result.stderr.strip(), 'tests_modified': False}, sort_keys=True), flush=True)
    finally:
        after = {str(p.relative_to(SUITE.parent)): digest(p) for p in sources}
        if before != after:
            raise SystemExit('Evidence runner changed original source or test files')


if __name__ == '__main__':
    main()
