"""Run a matched C/Zig competition. Preserve losses; never gate CI on speed."""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
SUITE = ROOT.parent
sys.path.insert(0, str(SUITE / 'tools'))
from benchmark_evidence import require, validate, summarize


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(command: list[str], destination: Path, *, expected: int = 0, timeout: int = 300) -> str:
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
        stdout, stderr, code = result.stdout, result.stderr, result.returncode
    except subprocess.TimeoutExpired as error:
        stdout = (error.stdout or b'').decode(errors='replace')
        stderr = (error.stderr or b'').decode(errors='replace') + '\nTIMEOUT\n'
        code = 124
    destination.with_suffix('.stdout').write_text(stdout)
    destination.with_suffix('.stderr').write_text(stderr)
    require(code == expected, f'{command} exited {code}: {stderr[-4000:]}')
    return stdout


def source_hashes(repo: Path) -> dict[str, str]:
    paths = subprocess.check_output(['git', 'ls-files', '-z', 'samples/12_c_extensions/', '.github/workflows/zig-rivals.yml'], cwd=repo).split(b'\0')
    # Git-tracked inputs only; symlink text is hashed without following it.
    return {os.fsdecode(p): hashlib.sha256(os.fsencode(os.readlink(repo / os.fsdecode(p)))).hexdigest()
            if (repo / os.fsdecode(p)).is_symlink() else sha(repo / os.fsdecode(p))
            for p in paths if p}


def head_to_head(rows: list[dict], manifest: dict) -> tuple[list[dict], str]:
    timings = {(r['process'], r['case'], r['trial'], r['variant']): r['nanoseconds'] / r['iterations']
               for r in rows if r.get('event') == 'timing'}
    records = []
    table = ['| Workload | Comparison (reference / candidate) | Paired process-median ratio | Range |',
             '| --- | --- | ---: | ---: |']
    for case, variants in manifest.items():
        for reference, candidate in ((variants[0], 'c_tuned'), ('zig_previous', 'zig_tuned'), ('c_tuned', 'zig_tuned')):
            by_process = defaultdict(list)
            for (process, name, trial, variant), duration in timings.items():
                if name == case and variant == candidate:
                    by_process[process].append(timings[process, name, trial, reference] / duration)
            medians = [statistics.median(values) for _, values in sorted(by_process.items())]
            require(bool(medians), 'missing comparison')
            record = {'case': case, 'reference': reference, 'candidate': candidate,
                      'paired_ratio': statistics.median(medians), 'process_ratios': medians}
            records.append(record)
            table.append(f"| {case} | {reference} / {candidate} | {record['paired_ratio']:.3f}x | {min(medians):.3f}..{max(medians):.3f}x |")
    return records, '\n'.join(table)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=('baseline', 'native'), default='baseline')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--processes', type=int, default=3)
    parser.add_argument('--trials', type=int, default=11)
    parser.add_argument('--min-ns', type=int, default=2_000_000)
    args = parser.parse_args()
    require(1 <= args.processes <= 9 and 3 <= args.trials <= 101, 'invalid repetitions')
    require(1000 <= args.min_ns <= 100_000_000, 'invalid minimum duration')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    git = lambda *a: subprocess.check_output(['git', *a], cwd=ROOT, text=True).strip()
    require(not git('status', '--porcelain', '--untracked-files=no'), 'tracked worktree must be clean')
    repo = Path(git('rev-parse', '--show-toplevel'))
    before = source_hashes(repo)
    zig = subprocess.check_output(['zig', 'version'], text=True).strip()
    require(zig == '0.16.0', 'use Zig 0.16.0')
    build = ['zig', 'build', '-Doptimize=ReleaseFast', f'-Dcpu={args.cpu}']
    capture([*build, 'test'], out / 'correctness', timeout=900)
    capture(build, out / 'build', timeout=900)
    for name, artifact in (('c', ROOT / 'zig-out/lib/rival-c.o'), ('zig', ROOT / 'zig-out/lib/librival_zig.a')):
        capture(['objdump', '-dr', str(artifact)], out / f'{name}-assembly')
        capture(['nm', '-S', '--size-sort', '--defined-only', str(artifact)], out / f'{name}-sizes')
    if hasattr(os, 'sched_getaffinity'):
        available = sorted(os.sched_getaffinity(0))
        try:
            os.sched_setaffinity(0, {available[0]})
        except OSError:
            pass
        affinity = sorted(os.sched_getaffinity(0))
    else:
        affinity = None
    metadata = {'commit': git('rev-parse', 'HEAD'), 'source_sha256': before, 'zig': zig,
                'cpu_target': args.cpu, 'mode': 'ReleaseFast', 'build_command': build,
                'machine': platform.machine(), 'os': platform.platform(), 'affinity': affinity,
                'cpuinfo': Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else None,
                'runner_image': os.environ.get('ImageVersion'), 'processes': args.processes,
                'trials': args.trials, 'min_ns': args.min_ns, 'lto': False, 'fast_math': False,
                'c_compiler': 'Zig bundled Clang; autovectorization enabled',
                'linker': subprocess.check_output(['cc', '--version'], text=True).splitlines()[0]}
    (out / 'environment.json').write_text(json.dumps(metadata, indent=2) + '\n')
    manifest = json.loads((ROOT / 'CASES.json').read_text())
    all_rows = []
    for kind, phase in (('alloc', 'allocation'), ('time', 'timing')):
        binary = ROOT / f'zig-out/bin/rivals-{kind}'
        symbols = capture(['nm', str(binary)], out / f'{kind}-symbols')
        if kind == 'time':
            require('__wrap_malloc' not in symbols and 'meter_begin' not in symbols, 'instrumentation leaked into timing')
        for process in range(args.processes if kind == 'time' else 1):
            command = [str(binary), str(args.trials), str(72819 + process * 104729), str(args.min_ns)]
            text = capture(command, out / f'{kind}-{process}')
            rows = [json.loads(line) for line in text.splitlines() if line.strip()]
            validate(rows, manifest, args.trials, phase, native=True)
            all_rows.append({'event': 'invocation', 'command': command, 'binary_sha256': sha(binary)})
            all_rows.extend(dict(row, process=process) for row in rows)
            print(f'VALIDATED rivals-{kind}-{process}: {len(rows)} records', flush=True)
    negative = [str(ROOT / 'zig-out/bin/rivals-alloc'), '3', '72819', '1000', '--inject-allocation']
    capture(negative, out / 'allocation-negative', expected=43)
    require((out / 'allocation-negative.stderr').read_text() == 'ALLOCATION_GUARD rival/lf/31\n', 'wrong allocation-negative diagnostic')
    require(before == source_hashes(repo), 'sources changed during capture')
    require(not git('status', '--porcelain', '--untracked-files=no'), 'worktree changed during capture')
    (out / 'raw.jsonl').write_text(''.join(json.dumps(row, sort_keys=True) + '\n' for row in all_rows))
    summary, table = summarize(all_rows, manifest)
    comparisons, comparison_table = head_to_head(all_rows, manifest)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    (out / 'comparisons.json').write_text(json.dumps(comparisons, indent=2) + '\n')
    allocations = [r for r in all_rows if r.get('event') == 'allocation']
    (out / 'allocations.json').write_text(json.dumps(allocations, indent=2) + '\n')
    report = ('# Matched C/Zig competition\n\nCommit: `' + metadata['commit'] + '`\n\n'
              'Ratios above 1 favor the candidate. Process ranges are not confidence intervals. '
              'All samples, losses and undersized batches are retained. Timings are batch means, '
              'not per-operation tail latency or renderer FPS. Allocation observations cover only '
              'the exercised linked libc references with the nonallocating test RNG.\n\n'
              + comparison_table + '\n\n## All participants\n\n' + table + '\n')
    (out / 'report.md').write_text(report)
    print(report)
    (out / 'SHA256SUMS.json').write_text(json.dumps({p.name: sha(p) for p in sorted(out.iterdir())
                                                  if p.is_file() and p.name != 'SHA256SUMS.json'}, indent=2) + '\n')

if __name__ == '__main__':
    main()
