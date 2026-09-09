"""Run separately built timing/profiling executables; retain and validate evidence.

No timing thresholds are CI gates. Missing samples, bad checksums, failed controls,
and accidental allocator calls in the scoped native test ARE failures.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess

SUITE = Path(__file__).resolve().parents[1]
CALLS = ('malloc_calls', 'calloc_calls', 'realloc_calls', 'free_calls', 'aligned_calls')


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def integer(row: dict, name: str, minimum: int = 0) -> int:
    value = row.get(name)
    require(type(value) is int and value >= minimum, f'invalid {name}: {value!r}')
    return value


def validate(rows: list[dict], expected: dict[str, list[str]], trials: int,
             phase: str, native: bool = False, lifecycle: str | None = None) -> list[dict]:
    """Fail closed against a versioned case manifest, not the rows that survived."""
    allowed = {'case', 'timing', 'allocation', 'complete', 'meter_selftest',
               'sqlite_environment', 'ruby_environment'}
    require(all(isinstance(r, dict) and r.get('event') in allowed for r in rows), 'unknown event')
    declarations = [r for r in rows if r['event'] == 'case']
    require(len(declarations) == len(expected), 'missing or duplicated case declaration')
    require({r['case']: r['variants'] for r in declarations} == expected, 'case manifest differs')
    completed = [r for r in rows if r['event'] == 'complete']
    require(len(completed) == 1, 'missing or duplicated completion record')
    samples = [r for r in rows if r['event'] == phase and (phase != 'allocation' or r['phase'] == 'batch')]
    count = sum(map(len, expected.values())) * (trials if phase == 'timing' else 1)
    require(len(samples) == count == integer(completed[0], 'records'), 'incomplete or duplicated samples')
    require(integer(completed[0], 'cases') == len(expected), 'wrong completion case count')
    groups: dict[tuple, list] = defaultdict(list)
    keys = set()
    for row in samples:
        name, variant = row.get('case'), row.get('variant')
        require(name in expected and variant in expected[name], 'unexpected case or variant')
        trial = integer(row, 'trial') if phase == 'timing' else 0
        require(trial < trials, 'out-of-range trial')
        key = (name, variant, trial)
        require(key not in keys, 'duplicate sample')
        keys.add(key)
        integer(row, 'checksum')
        if phase == 'timing':
            duration = integer(row, 'nanoseconds', 1)
            integer(row, 'iterations', 1)
            minimum = integer(row, 'min_ns', 1)
            require(row.get('instrumented') is False, 'instrumented timing is not allowed')
            require(row.get('undersized') is (duration < minimum), 'wrong undersized label')
            require(integer(row, 'order') < len(expected[name]), 'invalid order slot')
        else:
            integer(row, 'operations', 1)
            for field in (*CALLS, 'failed_calls', 'requested_bytes', 'overflow_requests'):
                integer(row, field)
            if native:
                require(sum(row[field] for field in CALLS) == 0, 'native allocation guard failed')
        groups[name, trial].append(row)
    for (name, _), group in groups.items():
        require(len(group) == len(expected[name]), 'missing variant')
        work = 'iterations' if phase == 'timing' else 'operations'
        require(len({row[work] for row in group}) == 1, 'unequal work within comparison')
        require(len({row['checksum'] for row in group}) == 1, 'checksum mismatch')
        if phase == 'timing':
            require(len({row['order'] for row in group}) == len(group), 'duplicate order slot')
    if phase == 'allocation':
        controls = [r for r in rows if r['event'] == 'meter_selftest']
        require(len(controls) == 1 and controls[0].get('passed') is True, 'missing meter self-test')
        for row in [r for r in rows if r['event'] == 'allocation']:
            for field in (*CALLS, 'failed_calls', 'requested_bytes', 'overflow_requests'):
                integer(row, field)
            require(row['failed_calls'] == 0 and row['overflow_requests'] == 0, 'unexpected allocation failure in successful workload')
            allocations = sum(row[k] for k in CALLS if k != 'free_calls')
            require(row['failed_calls'] <= allocations, 'more failures than allocator attempts')
            if row.get('live_before') is not None:
                before, after = integer(row, 'live_before'), integer(row, 'live_after')
                peak = integer(row, 'peak_live')
                require(peak >= max(before, after), 'invalid live-memory high water')
            else:
                require(row.get('live_after') is None and row.get('peak_live') is None, 'partial live counters')
            if row['phase'] in ('close', 'close-and-shutdown'):
                require(row['live_after'] == 0, 'allocator-owned bytes remain at shutdown')
        required_phases = {
            'sqlite': ('open', 'close-and-shutdown'),
            'ruby': ('open-register-parse-gc', 'final-gc', 'close'),
        }.get(lifecycle, ())
        for required_phase in required_phases:
            matches = [r for r in rows if r['event'] == 'allocation' and r['phase'] == required_phase]
            require(len(matches) == 1, f'missing or duplicated lifecycle phase: {required_phase}')
            require(matches[0]['live_before'] is not None, 'lifecycle requires live-byte tracking')
        if required_phases:
            require(rows[-1].get('phase') == required_phases[-1], 'shutdown must be the final evidence record')
    return samples


def run_command(args: list[str], output: Path, timeout: int = 300) -> subprocess.CompletedProcess:
    process = subprocess.run(args, text=True, capture_output=True, timeout=timeout)
    output.with_suffix('.stdout').write_text(process.stdout)
    output.with_suffix('.stderr').write_text(process.stderr)
    if process.returncode:
        raise RuntimeError(f'{args[0]} exited {process.returncode}: {process.stderr[-6000:]}')
    return process


def summarize(rows: list[dict], manifest: dict[str, list[str]]) -> tuple[list[dict], str]:
    groups: dict[tuple, list] = defaultdict(list)
    references = {}
    for row in rows:
        if row.get('event') == 'timing':
            groups[row['case'], row['variant']].append(row)
            if row['variant'] == manifest[row['case']][0]:
                references[row['process'], row['case'], row['trial']] = row['nanoseconds'] / row['iterations']
    summaries = []
    lines = ['| Workload | Variant | Median ns/op | MAD | p95 batch-mean ns/op | Paired baseline/variant | Process-median ratio range | Undersized |',
             '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for (name, variant), group in sorted(groups.items()):
        times = [r['nanoseconds'] / r['iterations'] for r in group]
        median = statistics.median(times)
        process_ratios: dict[int, list[float]] = defaultdict(list)
        for row, duration in zip(group, times):
            process_ratios[row['process']].append(references[row['process'], name, row['trial']] / duration)
        medians = [statistics.median(v) for v in process_ratios.values()]
        ratio = statistics.median(medians)
        record = {'case': name, 'variant': variant, 'median_ns': median,
                  'mad_ns': statistics.median(abs(t - median) for t in times),
                  'p95_batch_mean_ns': sorted(times)[math.ceil(.95 * len(times)) - 1],
                  'min_ns': min(times), 'max_ns': max(times), 'samples': len(times),
                  'processes': len(medians), 'paired_ratio_median': ratio,
                  'process_ratio_min': min(medians), 'process_ratio_max': max(medians),
                  'undersized_samples': sum(r['undersized'] for r in group)}
        summaries.append(record)
        lines.append(f"| {name} | {variant} | {median:.3f} | {record['mad_ns']:.3f} | {record['p95_batch_mean_ns']:.3f} | {ratio:.3f}x | {min(medians):.3f}–{max(medians):.3f}x | {record['undersized_samples']} |")
    return summaries, '\n'.join(lines)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('native', 'ruby'), required=True)
    parser.add_argument('--bin', type=Path, default=SUITE / 'measure/zig-out/bin')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--processes', type=int, default=3)
    parser.add_argument('--trials', type=int, default=11)
    parser.add_argument('--min-ns', type=int, default=2_000_000)
    parser.add_argument('--seed', type=int, default=72819)
    args = parser.parse_args()
    require(1 <= args.processes <= 9 and 3 <= args.trials <= 101, 'invalid repetition configuration')
    require(1000 <= args.min_ns <= 100_000_000 and 1 <= args.seed <= 0xffffffff, 'invalid seed or duration')
    args.output.mkdir(parents=True, exist_ok=True)
    binary_dir = args.bin.resolve()
    build = json.loads((binary_dir.parent / 'build.json').read_text())
    require(build['zig'] == '0.16.0' and build['optimize'] == 'ReleaseFast', 'timing requires the pinned ReleaseFast build')
    affinity = None
    if hasattr(os, 'sched_getaffinity'):
        available = sorted(os.sched_getaffinity(0))
        try:
            os.sched_setaffinity(0, {available[0]})
            affinity = sorted(os.sched_getaffinity(0))
        except OSError:
            affinity = available
    git = lambda *cmd: subprocess.check_output(['git', *cmd], cwd=SUITE, text=True).strip()
    metadata = {'event': 'environment', 'commit': git('rev-parse', 'HEAD'),
                'tracked_dirty': bool(git('status', '--porcelain', '--untracked-files=no')),
                'build': build, 'machine': platform.machine(), 'os': platform.platform(),
                'affinity_cpus': affinity, 'cpuinfo': Path('/proc/cpuinfo').read_text() if Path('/proc/cpuinfo').exists() else None,
                'runner': os.environ.get('RUNNER_NAME'), 'runner_image': os.environ.get('ImageVersion'),
                'processes': args.processes, 'trials': args.trials, 'min_ns': args.min_ns,
                'linker_driver': subprocess.check_output(['cc', '--version'], text=True).splitlines()[0],
                'timer': 'CLOCK_MONOTONIC', 'scope': 'native kernels, SQLite API lifecycle, or Ruby test host; never GPU FPS'}
    require(not metadata['tracked_dirty'], 'tracked sources differ from the recorded commit')
    (args.output / 'environment.json').write_text(json.dumps(metadata, indent=2) + '\n')
    manifest_all = json.loads((SUITE / 'measure/CASES.json').read_text())
    kinds = ['native', 'sqlite'] if args.kind == 'native' else ['ruby']
    all_rows: list[dict] = []
    manifest = {}
    for kind in kinds:
        expected = manifest_all[kind]; manifest.update(expected)
        extra = [str(SUITE / 'measure/ruby.rb')] if kind == 'ruby' else []
        for suffix, phase in [('alloc', 'allocation'), ('time', 'timing')]:
            executable = binary_dir / f'{kind}-{suffix}'
            checksum = digest(executable)
            symbols = subprocess.run(['nm', '-u', str(executable)], text=True, capture_output=True, check=True).stdout
            (args.output / f'{kind}-{suffix}.symbols.txt').write_text(symbols)
            if kind == 'native' and suffix == 'time':
                full_symbols = subprocess.check_output(['nm', str(executable)], text=True)
                require('__wrap_malloc' not in full_symbols and 'meter_begin' not in full_symbols, 'allocator instrumentation leaked into timing binary')
            repeat = args.processes if phase == 'timing' else 1
            for process_id in range(repeat):
                seed = ((args.seed + process_id * 104729) & 0xffffffff) or 1
                command = [str(executable), *extra, str(args.trials), str(seed), str(args.min_ns)]
                name = f'{kind}-{suffix}-{process_id}'
                result = run_command(command, args.output / name)
                rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
                validate(rows, expected, args.trials, phase, native=kind == 'native', lifecycle=kind)
                all_rows.append({'event': 'invocation', 'command': command, 'binary_sha256': checksum,
                                 'process': process_id, 'kind': kind, 'phase': phase})
                all_rows.extend(dict(r, process=process_id, kind=kind, phase_group=phase) for r in rows)
                print(f'VALIDATED {name}: {len(rows)} evidence records', flush=True)
        if kind == 'native':
            command = [str(binary_dir / 'native-alloc'), '3', str(args.seed), '1000', '--inject-allocation']
            negative = subprocess.run(command, text=True, capture_output=True, timeout=30)
            (args.output / 'allocation-negative.stderr').write_text(negative.stderr)
            require(negative.returncode == 43 and negative.stderr == 'ALLOCATION_GUARD native/lf/31\n', 'allocation tripwire negative control failed')
            all_rows.append({'event': 'negative_control', 'name': 'native-allocation', 'exit_code': 43, 'passed': True})
    (args.output / 'raw.jsonl').write_text(''.join(json.dumps(r, sort_keys=True) + '\n' for r in all_rows))
    summaries, table = summarize(all_rows, manifest)
    (args.output / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    allocations = [r for r in all_rows if r.get('event') == 'allocation']
    (args.output / 'allocations.json').write_text(json.dumps(allocations, indent=2) + '\n')
    allocation_table = ['| Workload / phase | Variant | Ops | Allocation attempts | Frees | Requested bytes | Live before → after | Peak live |',
                        '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for r in allocations:
        attempts = sum(r[k] for k in CALLS if k != 'free_calls')
        before = 'not observed' if r['live_before'] is None else f"{r['live_before']} → {r['live_after']}"
        peak = 'not observed' if r['peak_live'] is None else str(r['peak_live'])
        allocation_table.append(f"| {r['case']} / {r['phase']} | {r['variant']} | {r['operations']} | {attempts} | {r['free_calls']} | {r['requested_bytes']} | {before} | {peak} |")
    text = ('# Measurement evidence\n\nCommit: `' + metadata['commit'] + '`\n\n'
            'Uninstrumented timings; three layers are not interchangeable. p95 is a percentile of batch means, not single-operation tail latency. '
            'Ratios are paired within trials; ranges describe process-median ratios, not confidence intervals. No outliers are removed.\n\n' + table + '\n\n'
            '## Separate allocation profiling\n\nCounters count allocator calls/bytes, not Ruby objects. Native scope is linked libc references; SQLite scope is its general-purpose allocator '
            '(lookaside slots are not individual heap calls); mruby live bytes are requested payload, excluding counter headers.\n\n' + '\n'.join(allocation_table) + '\n')
    (args.output / 'report.md').write_text(text)
    checksums = {p.name: digest(p) for p in sorted(args.output.iterdir()) if p.is_file() and p.name != 'SHA256SUMS.json'}
    (args.output / 'SHA256SUMS.json').write_text(json.dumps(checksums, indent=2) + '\n')
    if summary_path := os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(summary_path, 'a') as output:
            output.write(text)
    print('MEASUREMENT_PROOF ' + json.dumps({'commit': metadata['commit'], 'timing_samples': sum(r.get('event') == 'timing' for r in all_rows),
                                          'allocation_records': len(allocations), 'processes': args.processes, 'trials': args.trials, 'renderer_validated': False}))


if __name__ == '__main__':
    main()
