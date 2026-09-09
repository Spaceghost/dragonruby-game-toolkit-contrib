"""Summarize raw, interleaved benchmark samples without performance gates."""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    groups: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for line in args.log.read_text().splitlines():
        if line.startswith('BENCH '):
            row = json.loads(line[6:])
            groups[row['case'], row['variant']].append(row)
    if not groups:
        raise SystemExit('No fresh benchmark records; refusing to publish a result')
    metadata = {
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        'zig': subprocess.check_output(['zig', 'version'], text=True).strip(),
        'os': platform.platform(), 'machine': platform.machine(),
        'cpu_target': os.environ.get('ZIG_CPU', 'unspecified'),
        'build_mode': os.environ.get('ZIG_OPTIMIZE', 'unspecified'),
        'runner': os.environ.get('RUNNER_NAME'),
        'scope': 'Warm-buffer native kernels, not Ruby calls, allocation profiling or rendering',
    }
    if Path('/proc/cpuinfo').exists():
        metadata['cpu'] = next((line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')), platform.processor())
    else:
        metadata['cpu'] = platform.processor()
    print('BENCH_ENV ' + json.dumps(metadata, sort_keys=True))
    baselines = {}
    summaries = []
    for (case, variant), samples in sorted(groups.items()):
        trials = [row['trial'] for row in samples]
        if len(samples) < 3 or len(set(trials)) != len(trials):
            raise SystemExit(f'Incomplete or duplicated samples: {case}/{variant}')
        if len({row['checksum'] for row in samples}) != 1:
            raise SystemExit(f'Unstable output: {case}/{variant}')
        values = [row['nanoseconds'] / row['iterations'] for row in samples]
        median = statistics.median(values)
        mad = statistics.median(abs(value - median) for value in values)
        row = {'case': case, 'variant': variant, 'median_ns': median, 'min_ns': min(values), 'max_ns': max(values), 'mad_ns': mad, 'trials': len(values), 'noisy': mad / median > .05}
        summaries.append(row)
        if variant.startswith('c_'):
            baselines[case] = median
    lines = ['## Measured native kernels', '', 'Times are per call/tick, not per byte or star. Ratios compare medians on this runner only. Allocation counts are not instrumented.', '', '| Case | Variant | Median ns | MAD ns | Min..max ns | C / variant |', '| --- | --- | ---: | ---: | ---: | ---: |']
    for row in summaries:
        ratio = baselines[row['case']] / row['median_ns']
        row['c_over_variant'] = ratio
        print('BENCH_SUMMARY ' + json.dumps(row, sort_keys=True))
        label = ' noisy' if row['noisy'] else ''
        message = f"{row['case']} {row['variant']}: {row['median_ns']:.2f} ns/call, MAD {row['mad_ns']:.2f}, C/variant {ratio:.3f}x, n={row['trials']}{label}"
        if os.environ.get('GITHUB_ACTIONS'):
            print('::notice title=Native benchmark::' + message)
        lines.append(f"| {row['case']} | {row['variant']}{label} | {row['median_ns']:.2f} | {row['mad_ns']:.2f} | {row['min_ns']:.2f}..{row['max_ns']:.2f} | {ratio:.3f}x |")
    lines += ['', '```json', json.dumps(metadata, indent=2, sort_keys=True), '```', '', 'Raw BENCH records above retain every timed sample, iteration count and checksum. Shared-runner noise is reported, not used as a pass/fail speed threshold.']
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as summary:
            summary.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
