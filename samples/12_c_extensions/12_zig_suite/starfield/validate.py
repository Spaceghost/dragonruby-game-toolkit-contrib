#!/usr/bin/env python3
from __future__ import annotations
import json, statistics, sys
from collections import defaultdict

SIZES = [64, 1024, 16384, 100000]
STAGES = ["update", "update-pack", "frame-sink"]
TRIALS = 11

rows = []
correct = complete = None
for raw in sys.stdin:
    line = raw.strip()
    if line.startswith("STARFIELD_CORRECTNESS "):
        correct = json.loads(line.split(" ", 1)[1])
    elif line.startswith("STARFIELD_COMPLETE "):
        complete = json.loads(line.split(" ", 1)[1])
    elif line.startswith("{"):
        obj = json.loads(line)
        if obj.get("event") == "starfield_timing": rows.append(obj)
if correct != {"sizes": 4, "stages": 3, "sink_calls_per_frame": 1, "renderer": False}:
    raise SystemExit(f"bad correctness record: {correct!r}")
expected = len(SIZES) * len(STAGES) * TRIALS
if complete is None or complete.get("records") != expected or len(rows) != expected:
    raise SystemExit(f"incomplete evidence: rows={len(rows)} complete={complete!r} expected={expected}")
groups = defaultdict(list)
seen = set()
for row in rows:
    key = (row["size"], row["stage"], row["trial"])
    if key in seen: raise SystemExit(f"duplicate {key}")
    seen.add(key)
    if row["size"] not in SIZES or row["stage"] not in STAGES or row["trial"] not in range(TRIALS): raise SystemExit(f"unexpected row {row}")
    if row["iterations"] <= 0 or row["elapsed_ns"] <= 0 or row["ns_per_frame"] <= 0: raise SystemExit(f"invalid timing {row}")
    expected_calls = row["iterations"] if row["stage"] == "frame-sink" else 0
    if row["sink_calls"] != expected_calls: raise SystemExit(f"sink call mismatch {row}")
    groups[(row["size"], row["stage"])].append(row["ns_per_frame"])
print("# Persistent starfield stages")
print("| stars | stage | median ns/frame | MAD |")
print("| ---: | --- | ---: | ---: |")
for size in SIZES:
    for stage in STAGES:
        values = groups[(size, stage)]
        if len(values) != TRIALS: raise SystemExit(f"missing group {(size, stage)}")
        med = statistics.median(values)
        mad = statistics.median(abs(v-med) for v in values)
        print(f"| {size} | {stage} | {med:.3f} | {mad:.3f} |")
