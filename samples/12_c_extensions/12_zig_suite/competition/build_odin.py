#!/usr/bin/env python3
"""Build the pinned Odin native package as one C-ABI object for shared harnesses."""
from __future__ import annotations
import argparse, hashlib, json, pathlib, subprocess

ROOT = pathlib.Path(__file__).resolve().parent
SOURCE_DIR = ROOT.parent / "src"
OUT = ROOT / "odin-out" / "competitive.o"
PIN = {"version": "dev-2026-09", "commit": "a2fb372b76e81ef31fbbc8a2cf2b4fdf5ac6c924"}


def hash_sources() -> dict[str, str]:
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(SOURCE_DIR.glob("*.odin"))}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpu", choices=("baseline", "native"), default="baseline")
    args = parser.parse_args()
    version = subprocess.check_output(["odin", "version"], text=True).strip()
    if PIN["version"] not in version:
        raise SystemExit(f"expected Odin {PIN['version']}, got {version!r}")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "odin", "build", str(SOURCE_DIR), "-build-mode:obj", "-no-entry-point",
        "-reloc-mode:pic", "-o:speed", "-no-bounds-check", f"-out:{OUT}",
    ]
    if args.cpu == "native":
        command.append("-microarch:native")
    subprocess.run(command, cwd=ROOT, check=True)
    payload = {
        **PIN,
        "reported_version": version,
        "cpu": args.cpu,
        "command": command,
        "source_sha256": hash_sources(),
        "object_sha256": hashlib.sha256(OUT.read_bytes()).hexdigest(),
        "object_bytes": OUT.stat().st_size,
    }
    (OUT.parent / "build.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print("ODIN_BUILD " + json.dumps(payload, sort_keys=True))


if __name__ == "__main__":
    main()
