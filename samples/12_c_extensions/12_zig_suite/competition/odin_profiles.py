#!/usr/bin/env python3
"""Measure pinned Odin optimization profiles without changing benchmark semantics."""
from __future__ import annotations
import argparse, json, pathlib, shutil, statistics, subprocess, time

ROOT = pathlib.Path(__file__).resolve().parent
PROFILES = ("minimal", "size", "speed", "aggressive")
SELECTED = (
    "rival/lf/4k", "rival/lf/16k", "rival/lf/64k", "rival/lf/1m-warm",
    "rival/stars/16384-no-wrap", "rival/stars/16384-mixed", "rival/stars/4096-all-wrap",
)


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{command} exited {result.returncode}\n{result.stdout[-2000:]}\n{result.stderr[-4000:]}")
    return result


def symbol_sizes(obj: pathlib.Path) -> dict[str, int]:
    result = run(["nm", "-S", "--size-sort", "--defined-only", str(obj)])
    sizes = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 4:
            try: sizes[fields[-1]] = int(fields[1], 16)
            except ValueError: pass
    return sizes


def text_bytes(obj: pathlib.Path) -> int:
    result = run(["size", "-A", str(obj)])
    total = 0
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith(".text"):
            try: total += int(fields[1])
            except ValueError: pass
    return total


def benchmark(binary: pathlib.Path) -> dict[str, float]:
    samples: dict[str, list[float]] = {case: [] for case in SELECTED}
    for process in range(3):
        result = run([str(binary), "5", str(72819 + process * 104729), "1000000"], timeout=180)
        for line in result.stdout.splitlines():
            if not line.startswith("{"): continue
            row = json.loads(line)
            if row.get("event") != "timing" or row.get("variant") != "odin_tuned": continue
            case = row["case"]
            if case in samples:
                samples[case].append(row["nanoseconds"] / row["iterations"])
    if any(len(values) != 15 for values in samples.values()):
        raise RuntimeError(f"missing Odin profile samples: { {k: len(v) for k,v in samples.items()} }")
    return {case: statistics.median(values) for case, values in samples.items()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu", choices=("baseline", "native"), default="baseline")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    work = ROOT / ".odin-profile-work"
    shutil.rmtree(work, ignore_errors=True); work.mkdir()
    rows = []
    try:
        for profile in PROFILES:
            folder = work / profile; folder.mkdir()
            obj = folder / "competitive.o"
            metadata = folder / "build.json"
            built = run(["python3", "build_odin.py", "--cpu", args.cpu, "--profile", profile,
                         "--out", str(obj), "--metadata", str(metadata)], timeout=300)
            (output / f"{profile}-build.log").write_text(built.stdout + built.stderr)
            meta = json.loads(metadata.read_text())
            prefix = folder / "out"
            rel_obj = obj.relative_to(ROOT)
            started = time.perf_counter()
            linked = run(["zig", "build", "-Doptimize=ReleaseFast", f"-Dcpu={args.cpu}",
                          f"-Dodin-object={rel_obj}", "--prefix", str(prefix)], timeout=600)
            link_seconds = time.perf_counter() - started
            (output / f"{profile}-link.log").write_text(linked.stdout + linked.stderr)
            binary = prefix / "bin/rivals-time"
            sizes = symbol_sizes(obj)
            assembly = run(["objdump", "-dr", str(obj)])
            (output / f"{profile}-disassembly.txt").write_text(assembly.stdout)
            row = {
                "profile": profile,
                "compile_seconds": meta["compile_seconds"],
                "link_seconds": link_seconds,
                "object_bytes": obj.stat().st_size,
                "text_bytes": text_bytes(obj),
                "symbols": {name: sizes.get(name) for name in (
                    "drbo_count_dual", "drbo_stars_block", "drbo_starfield_update",
                    "drbo_query_pack")},
                "median_ns": benchmark(binary),
            }
            rows.append(row)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    payload = {"cpu": args.cpu, "profiles": rows, "samples_per_case": 15,
               "scope": "same Odin sources; unchanged shared rivals-time harness; not a C compiler comparison"}
    (output / "odin-profiles.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    lines = ["# Odin optimization profile matrix", "",
             "Same Odin sources and shared C/Zig/Odin runtime harness. Runtime cells are median ns/op across 3 processes × 5 trials.", "",
             "| profile | compile s | link s | object bytes | text bytes | 4K LF ns | 16K LF ns | 1M LF ns | stars no-wrap ns | stars mixed ns |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in rows:
        med=row["median_ns"]
        lines.append(f"| {row['profile']} | {row['compile_seconds']:.3f} | {row['link_seconds']:.3f} | {row['object_bytes']} | {row['text_bytes']} | {med['rival/lf/4k']:.3f} | {med['rival/lf/16k']:.3f} | {med['rival/lf/1m-warm']:.3f} | {med['rival/stars/16384-no-wrap']:.3f} | {med['rival/stars/16384-mixed']:.3f} |")
    report="\n".join(lines)+"\n"
    (output / "odin-profiles.md").write_text(report)
    print(report)

if __name__ == "__main__":
    main()
