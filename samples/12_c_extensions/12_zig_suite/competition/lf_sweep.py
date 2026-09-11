#!/usr/bin/env python3
from __future__ import annotations
import json, pathlib, statistics, sys

VARIANTS = ("zig_scalar", "zig_blocked32", "zig_dual", "c_dual")
ZIG = VARIANTS[:3]
OFFSETS = (0, 1, 15)
TRIALS = 5


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: lf_sweep.py RAW_JSONL OUTPUT_DIR")
    raw = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
    rows = []
    complete = None
    for line in raw.read_text().splitlines():
        if line.startswith('{'):
            row = json.loads(line)
            if row.get('event') == 'lf_sweep': rows.append(row)
        elif line.startswith('LF_SWEEP_COMPLETE '):
            complete = json.loads(line.split(' ', 1)[1])
    expected_records = 513 * len(OFFSETS) * len(VARIANTS) * TRIALS
    if complete is None or complete.get('records') != expected_records or len(rows) != expected_records:
        raise SystemExit(f'incomplete sweep: rows={len(rows)} complete={complete}')

    groups: dict[tuple[int,int,str], list[float]] = {}
    iterations: dict[tuple[int,int], set[int]] = {}
    for r in rows:
        if r['variant'] not in VARIANTS or r['offset'] not in OFFSETS or not (0 <= r['length'] <= 512):
            raise SystemExit(f'invalid row {r}')
        if r['ns_per_call'] <= 0 or r['elapsed_ns'] <= 0 or r['iterations'] <= 0:
            raise SystemExit(f'invalid timing {r}')
        groups.setdefault((r['length'], r['offset'], r['variant']), []).append(r['ns_per_call'])
        iterations.setdefault((r['length'], r['offset']), set()).add(r['iterations'])
    for key, vals in groups.items():
        if len(vals) != TRIALS: raise SystemExit(f'missing trials {key}: {len(vals)}')
    for key, counts in iterations.items():
        if len(counts) != 1: raise SystemExit(f'unequal work {key}: {counts}')

    summary = []
    winners = []
    zig_winners = []
    for length in range(513):
        medians = {}
        for variant in VARIANTS:
            per_offset = [statistics.median(groups[length, off, variant]) for off in OFFSETS]
            medians[variant] = statistics.median(per_offset)
        winner = min(VARIANTS, key=medians.get)
        zig_winner = min(ZIG, key=medians.get)
        winners.append(winner); zig_winners.append(zig_winner)
        summary.append({'length': length, 'winner': winner, 'zig_winner': zig_winner, 'median_ns': medians})

    def ranges(values: list[str]) -> list[tuple[int,int,str]]:
        result=[]; start=0; current=values[0]
        for i, value in enumerate(values[1:], 1):
            if value != current:
                result.append((start, i-1, current)); start=i; current=value
        result.append((start, len(values)-1, current))
        return result

    zig_ranges = ranges(zig_winners)
    all_ranges = ranges(winners)
    # This intentionally does NOT emit a source threshold. Fragmented ranges are
    # evidence that one threshold would be dishonest; subsequent repeated runs
    # decide whether adjacent ranges collapse into a stable short/long split.
    lines = [
        '# Exhaustive short-newline sweep', '',
        'Lengths `0..512`, offsets `0/1/15`, five trials, equal iterations per length/offset/variant set.',
        'Medians are first taken within each offset and then across offsets. No timing gate or automatic dispatcher is installed.', '',
        '## Best Zig microkernel ranges', '',
    ]
    lines += [f'- `{a}..{b}`: `{v}`' for a,b,v in zig_ranges]
    lines += ['', '## Best overall C/Zig ranges', '']
    lines += [f'- `{a}..{b}`: `{v}`' for a,b,v in all_ranges]
    counts = {v: winners.count(v) for v in VARIANTS}
    zcounts = {v: zig_winners.count(v) for v in ZIG}
    lines += ['', f'Overall winner counts: `{counts}`.', f'Zig-only winner counts: `{zcounts}`.', '']
    (out/'lf-sweep-summary.json').write_text(json.dumps({'complete': complete, 'lengths': summary, 'winner_ranges': all_ranges, 'zig_winner_ranges': zig_ranges}, indent=2) + '\n')
    (out/'lf-sweep-report.md').write_text('\n'.join(lines))
    print('\n'.join(lines))

if __name__ == '__main__': main()
