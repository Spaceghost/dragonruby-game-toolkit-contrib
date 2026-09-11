#!/usr/bin/env python3
from __future__ import annotations
import json, statistics, sys
from collections import defaultdict

SIZES = [64, 1024, 16384, 100000]
STAGES = ["update", "update-full-pack", "update-pack", "frame-sink"]
LANGUAGES = ["zig", "odin"]
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
if correct != {"sizes": 4, "stages": 4, "languages": 2, "sink_calls_per_frame": 1, "renderer": False}:
    raise SystemExit(f"bad correctness record: {correct!r}")
expected = len(SIZES) * len(STAGES) * len(LANGUAGES) * TRIALS
if complete is None or complete.get("records") != expected or len(rows) != expected:
    raise SystemExit(f"incomplete evidence: rows={len(rows)} complete={complete!r} expected={expected}")
groups = defaultdict(list)
seen = set()
iterations = defaultdict(set)
for row in rows:
    key = (row["size"], row["stage"], row["language"], row["trial"])
    if key in seen: raise SystemExit(f"duplicate {key}")
    seen.add(key)
    if row["size"] not in SIZES or row["stage"] not in STAGES or row["language"] not in LANGUAGES or row["trial"] not in range(TRIALS): raise SystemExit(f"unexpected row {row}")
    if row["order"] not in range(len(LANGUAGES)) or row["iterations"] <= 0 or row["elapsed_ns"] <= 0 or row["ns_per_frame"] <= 0: raise SystemExit(f"invalid timing {row}")
    expected_calls = row["iterations"] if row["stage"] == "frame-sink" else 0
    if row["sink_calls"] != expected_calls: raise SystemExit(f"sink call mismatch {row}")
    groups[(row["size"], row["stage"], row["language"])].append(row["ns_per_frame"])
    iterations[(row["size"], row["stage"], row["trial"])].add(row["iterations"])
for key, values in iterations.items():
    if len(values) != 1: raise SystemExit(f"unequal work {key}: {values}")

print("# Persistent Zig/Odin starfield stages")
print("| stars | stage | Zig ns/frame | Odin ns/frame | Zig/Odin ratio |")
print("| ---: | --- | ---: | ---: | ---: |")
for size in SIZES:
    for stage in STAGES:
        zg = groups[(size, stage, "zig")]; od = groups[(size, stage, "odin")]
        if len(zg) != TRIALS or len(od) != TRIALS: raise SystemExit(f"missing group {(size,stage)}")
        zm, om = statistics.median(zg), statistics.median(od)
        print(f"| {size} | {stage} | {zm:.3f} | {om:.3f} | {zm/om:.3f}x |")
    for language in LANGUAGES:
        full = statistics.median(groups[(size, "update-full-pack", language)])
        hot = statistics.median(groups[(size, "update-pack", language)])
        print(f"| {size} | {language} hot/full pack | {hot/full:.3f}x | - | - |")
