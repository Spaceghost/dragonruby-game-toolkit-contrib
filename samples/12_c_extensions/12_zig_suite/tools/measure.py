"""Capture native timings with source and environment provenance at execution.

The hashes bind a log to recorded inputs/output, not to a trusted signer.
Run report.py on the saved log to validate completeness before publishing.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import os
import platform
import subprocess

SUITE = Path(__file__).resolve().parents[1]


def output(args: list[str], cwd: Path) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def source_hashes(suite: Path) -> dict[str, str]:
    # Include actual working files, not just index blobs; avoid build caches.
    paths = [suite / 'build.zig']
    for directory, suffixes in (('src', {'.zig', '.h'}), ('tests', {'.c', '.h'}),
                               ('sqlite', {'.zig', '.c', '.h'}), ('evidence', {'.zig', '.c', '.h'}),
                               ('tools', {'.py'})):
        paths.extend(p for p in (suite / directory).iterdir() if p.is_file() and p.suffix in suffixes)
    paths.extend(suite.parent / name for name in (
        '01_basics/app/ext.c', '02_intermediate/app/re.c', '02_intermediate/app/re.h',
        '03_native_pixel_arrays/app/ext.c', '04_handcrafted_extension_advanced/native/ext-bindings.c',
        '11_zig_native_pixel_arrays/app/native.zig', '11_zig_native_pixel_arrays/tests/support/guard.h'))
    return {str(p.relative_to(suite.parent)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def record(metadata: dict, payload: str, exit_code: int, unchanged: bool) -> str:
    # Canonical LF allows the same captured text to be validated on another OS.
    payload = payload.replace('\r\n', '\n')
    if payload and not payload.endswith('\n'):
        payload += '\n'
    footer = {'exit_code': exit_code, 'sources_unchanged': unchanged,
              'output_sha256': hashlib.sha256(payload.encode()).hexdigest()}
    return ('BENCH_ENV ' + json.dumps(metadata, sort_keys=True) + '\n' + payload
            + 'BENCH_RUN ' + json.dumps(footer, sort_keys=True) + '\n')


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('scope', choices=('kernels', 'sqlite'))
    parser.add_argument('--cpu', choices=('baseline', 'native'), default='baseline')
    parser.add_argument('--mode', choices=('Debug', 'ReleaseSafe', 'ReleaseFast'), default='ReleaseFast')
    parser.add_argument('--trials', type=int, default=11)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 3 <= args.trials <= 101 or (args.scope == 'sqlite' and args.trials != 11):
        parser.error('kernels require 3..101 trials; SQLite currently requires exactly 11')
    cwd = SUITE if args.scope == 'kernels' else SUITE / 'sqlite'
    command = ['zig', 'build', 'bench' if args.scope == 'kernels' else 'test',
               f'-Doptimize={args.mode}', f'-Dcpu={args.cpu}', '--summary', 'all']
    if args.scope == 'kernels':
        command += ['--', str(args.trials)]
    zig = output(['zig', 'version'], SUITE)
    if zig != '0.16.0':
        raise SystemExit('Use the pinned Zig 0.16.0 toolchain')
    before = source_hashes(SUITE)
    cpu = platform.processor()
    if Path('/proc/cpuinfo').exists():
        cpu = next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                    if line.startswith('model name')), cpu or platform.machine())
    metadata = {'schema': 1, 'scope': args.scope, 'trials': args.trials,
                'commit': output(['git', 'rev-parse', 'HEAD'], SUITE), 'source_sha256': before,
                'working_tree_status': output(['git', 'status', '--porcelain', '--untracked-files=no'], SUITE),
                'command': command, 'working_directory': str(cwd), 'zig': zig,
                'cpu_target': args.cpu, 'build_mode': args.mode, 'cpu': cpu,
                'os': platform.platform(), 'machine': platform.machine(), 'runner': os.environ.get('RUNNER_NAME')}
    try:
        result = subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=900, check=False)
        payload, code = result.stdout.decode('utf-8'), result.returncode
    except subprocess.TimeoutExpired as error:
        payload = (error.stdout or b'').decode('utf-8') + '\nCAPTURE_TIMEOUT\n'
        code = 124
    unchanged = before == source_hashes(SUITE)
    captured = record(metadata, payload, code, unchanged)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(captured, encoding='utf-8', newline='\n')
    print(captured, end='')
    if code != 0 or not unchanged:
        raise SystemExit('Benchmark capture failed; the saved run is not reportable')


if __name__ == '__main__':
    main()
