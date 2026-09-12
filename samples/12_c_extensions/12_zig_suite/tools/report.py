"""Validate complete recorded runs before reporting workload-specific timings."""
from __future__ import annotations
import argparse
from collections import defaultdict
import hashlib
import json
import os
import re
from pathlib import Path
import statistics

# Explicit case contracts prevent an entirely missing variant/case from
# disappearing from the report. Update these alongside the C harness.
KERNELS = {}
for task, variants, cases in (
    ('lf', ('c_control', 'zig_previous16', 'zig_blocked32'), (('tiny', 32, 262144), ('page', 4096, 16384), ('large', 1048576, 128))),
    ('sum', ('c_control', 'zig_ordered', 'zig_unrolled'), (('tiny', 8, 262144), ('large', 65536, 256))),
    ('scanner', ('c_original_kernel', 'zig_previous', 'zig_dirty_rows'), (('frame', 100, 500000),)),
    ('stars', ('c_original_kernel', 'zig_scalar', 'zig_soa8'), (('small', 64, 32768), ('large', 16384, 1024), ('all-wrap', 4096, 512))),
    ('regex', ('c_original', 'zig_baseline', 'zig_bitmap_simd'), (('literal-small', 128, 8192), ('literal-large', 65536, 128), ('class', 4096, 512))),
):
    for name, size, iterations in cases:
        KERNELS[f'{task}/{name}'] = (variants, size, iterations)
CONTRACTS = {'kernels': KERNELS, 'sqlite': {
    'sqlite/repeated-query': (('c_control', 'zig_prepare_each', 'zig_reuse', 'c_reuse'), 1, 10000),
}}


def integer(value: object, minimum: int = 0) -> bool:
    return type(value) is int and value >= minimum


def validate(text: str) -> tuple[dict, dict]:
    lines = text.splitlines(keepends=True)
    if len(lines) < 3 or not lines[0].startswith('BENCH_ENV ') or not lines[-1].startswith('BENCH_RUN '):
        raise ValueError('Missing capture provenance; use tools/measure.py to record a run')
    metadata = json.loads(lines[0][10:])
    footer = json.loads(lines[-1][10:])
    if not isinstance(metadata, dict) or not isinstance(footer, dict):
        raise ValueError('Metadata must be JSON objects')
    payload = ''.join(lines[1:-1])
    if footer.get('exit_code') != 0 or type(footer.get('exit_code')) is not int or footer.get('sources_unchanged') is not True:
        raise ValueError('Failed run or changed sources')
    if footer.get('output_sha256') != hashlib.sha256(payload.encode()).hexdigest():
        raise ValueError('Recorded output digest mismatch')
    if type(metadata.get('schema')) is not int or metadata['schema'] != 1 or not isinstance(metadata.get('scope'), str) or metadata['scope'] not in CONTRACTS:
        raise ValueError('Unknown capture schema or scope')
    for key in ('commit', 'source_sha256', 'command', 'zig', 'cpu_target', 'build_mode', 'os', 'machine'):
        if not metadata.get(key):
            raise ValueError(f'Missing provenance field: {key}')
    if not isinstance(metadata['commit'], str) or not re.fullmatch(r'[0-9a-f]{40}', metadata['commit']):
        raise ValueError('Invalid recorded commit')
    hashes = metadata['source_sha256']
    if not isinstance(hashes, dict) or not all(isinstance(k, str) and k and isinstance(v, str) and re.fullmatch(r'[0-9a-f]{64}', v) for k, v in hashes.items()):
        raise ValueError('Invalid recorded source digests')
    if not isinstance(metadata['command'], list) or not all(isinstance(v, str) and v for v in metadata['command']):
        raise ValueError('Invalid recorded command')
    if metadata['zig'] != '0.16.0' or metadata['cpu_target'] not in ('baseline', 'native') or metadata['build_mode'] not in ('Debug', 'ReleaseSafe', 'ReleaseFast'):
        raise ValueError('Unexpected recorded toolchain or build options')
    trials = metadata.get('trials')
    if not integer(trials, 3) or trials > 101:
        raise ValueError('Invalid trial count')
    contract = CONTRACTS[metadata['scope']]
    groups = defaultdict(list)
    for line in lines[1:-1]:
        if line.startswith(('BENCH_ENV ', 'BENCH_RUN ')):
            raise ValueError('Duplicated run metadata')
        if not line.startswith('BENCH '):
            continue
        row = json.loads(line[6:])
        if not isinstance(row, dict):
            raise ValueError('Benchmark records must be JSON objects')
        case, variant = row.get('case'), row.get('variant')
        if not isinstance(case, str) or not isinstance(variant, str) or case not in contract or variant not in contract[case][0]:
            raise ValueError('Unexpected case or variant')
        for key in ('size', 'iterations', 'trial', 'nanoseconds', 'checksum'):
            if not integer(row.get(key), 1 if key in ('iterations', 'nanoseconds') else 0):
                raise ValueError(f'Invalid numeric field: {key}')
        if (row['size'], row['iterations']) != contract[case][1:]:
            raise ValueError('Inconsistent workload size or iteration count')
        if 'allocation_count' not in row or row['allocation_count'] is not None:
            raise ValueError('Timing harness does not instrument allocations; do not invent a count')
        groups[case, variant].append(row)
    expected = {(case, variant) for case, spec in contract.items() for variant in spec[0]}
    if set(groups) != expected:
        raise ValueError('Missing benchmark cases or variants')
    for key, rows in groups.items():
        if len(rows) != trials or sorted(row['trial'] for row in rows) != list(range(trials)):
            raise ValueError(f'Missing or duplicated trials: {key}')
    for case, spec in contract.items():
        checksums = {row['checksum'] for variant in spec[0] for row in groups[case, variant]}
        if len(checksums) != 1:
            raise ValueError(f'Outputs disagree within or between variants: {case}')
    return metadata, dict(groups)


def summarize(metadata: dict, groups: dict) -> list[dict]:
    medians = {key: statistics.median(row['nanoseconds'] / row['iterations'] for row in rows)
               for key, rows in groups.items()}
    output = []
    for (case, variant), rows in sorted(groups.items()):
        values = [row['nanoseconds'] / row['iterations'] for row in rows]
        median = medians[case, variant]
        mad = statistics.median(abs(value - median) for value in values)
        reference = CONTRACTS[metadata['scope']][case][0][0]
        if case == 'sqlite/repeated-query' and variant in ('c_reuse', 'zig_reuse'):
            reference = 'c_reuse'
        output.append({'case': case, 'variant': variant, 'reference_variant': reference,
                       'reference_over_variant': medians[case, reference] / median,
                       'median_ns': median, 'min_ns': min(values), 'max_ns': max(values),
                       'mad_ns': mad, 'trials': len(rows), 'noisy': mad / median > .05})
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        metadata, groups = validate(args.log.read_text())
    except (ValueError, KeyError, TypeError) as error:
        raise SystemExit(f'Refusing benchmark report: {error}') from error
    print('BENCH_PROVENANCE ' + json.dumps(metadata, sort_keys=True))
    lines = ['## Validated native measurements', '',
             'Per native call/tick, not Ruby latency or renderer FPS. Allocation timing counts are unmeasured.', '',
             '| Case | Variant | Reference | Median ns | MAD ns | Reference / variant |',
             '| --- | --- | --- | ---: | ---: | ---: |']
    for row in summarize(metadata, groups):
        print('BENCH_SUMMARY ' + json.dumps(row, sort_keys=True))
        lines.append(f"| {row['case']} | {row['variant']} | {row['reference_variant']} | {row['median_ns']:.3f} | {row['mad_ns']:.3f} | {row['reference_over_variant']:.3f}x |")
    print('BENCH_COMPLETENESS ' + json.dumps({'cases': len(CONTRACTS[metadata['scope']]),
          'variants': len(groups), 'records': sum(map(len, groups.values())), 'trials_per_variant': metadata['trials']}))
    lines += ['', 'Provenance was captured with this run, not reconstructed from the reporting machine.',
              'Shared-runner timings are observations, not speed guarantees. Raw output and source hashes are in the log.',
              '', '```json', json.dumps(metadata, sort_keys=True, indent=2), '```']
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as output:
            output.write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
