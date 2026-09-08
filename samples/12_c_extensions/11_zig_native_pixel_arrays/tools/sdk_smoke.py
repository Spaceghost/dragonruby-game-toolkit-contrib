#!/usr/bin/env python3
"""Build and exercise this sample in a user-supplied, matching DragonRuby SDK.

No SDK download, license bypass, source mutation, or fake SDK fallback is used.
The native library and game are staged in a temporary directory. A fresh nonce
must be reported after Ruby tests and 60 calls to the actual sample's tick.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

SAMPLE = Path(__file__).resolve().parents[1]


class SmokeError(RuntimeError):
    """A build, validation, or runtime probe failed."""


def selection(target: str | None, folder: str | None) -> tuple[str, str]:
    if bool(target) != bool(folder):
        raise SmokeError("Provide --target and --platform together.")
    if not target:
        machine = platform.machine().lower()
        choices = {
            ("Linux", "x86_64"): ("x86_64-linux-gnu", "linux-amd64"),
            ("Darwin", "x86_64"): ("x86_64-macos", "macos"),
            ("Darwin", "arm64"): ("aarch64-macos", "macos"),
            ("Windows", "amd64"): ("x86_64-windows-gnu", "windows-amd64"),
            ("Windows", "x86_64"): ("x86_64-windows-gnu", "windows-amd64"),
        }
        try:
            target, folder = choices[(platform.system(), machine)]
        except KeyError as error:
            raise SmokeError("This host needs explicit --target and --platform.") from error
    assert target and folder
    if not re.fullmatch(r"[A-Za-z0-9_-]+", folder):
        raise SmokeError("--platform must be a single native directory name.")
    if not re.fullmatch(r"[A-Za-z0-9_.+-]+", target):
        raise SmokeError("Invalid Zig target.")
    return target, folder


def validate_sdk(sdk: Path, runtime: Path | None) -> Path:
    headers = [sdk / "dragonruby.h", sdk / "include/dragonruby.h"]
    header = next((path for path in headers if path.is_file()), None)
    if header is None:
        raise SmokeError("The matching SDK's dragonruby.h is missing.")
    if "DRB_ZIG_TEST_HOST" in header.read_text(encoding="utf-8", errors="replace"):
        raise SmokeError("The test-contract header is not a DragonRuby SDK.")
    if not any((sdk / path).is_file() for path in ("include/mruby.h", "mruby/include/mruby.h")):
        raise SmokeError("The matching SDK's mruby.h is missing.")
    runtime = (runtime or sdk / ("dragonruby.exe" if os.name == "nt" else "dragonruby")).resolve()
    if not runtime.is_file():
        raise SmokeError("DragonRuby executable is missing; supply --runtime when needed.")
    return runtime


def probe_source(nonce: str) -> str:
    # JSON encoding is valid for these ASCII Ruby string literals.
    success, failure = json.dumps(nonce + ":PASS"), json.dumps(nonce + ":FAIL\n")
    return f'''

alias zig_sample_tick tick

def tick args
  unless @zig_smoke_started
    require "tests/smoke.rb"
    @zig_smoke_started = true
    @zig_smoke_frames = 0
  end
  zig_sample_tick args
  @zig_smoke_frames += 1
  if @zig_smoke_frames == 60
    DR.write_file "smoke.status", {success}
  end
rescue Exception => error
  DR.write_file "smoke.status", {failure} + error.class.to_s + ": " + error.message
  raise
end
'''


def wait_for_result(command: list[str], game: Path, nonce: str, timeout: float, log) -> None:
    """Only a fresh success marker counts. Exit code zero alone is insufficient."""
    process = subprocess.Popen(command, cwd=game, stdout=log, stderr=subprocess.STDOUT)
    deadline = time.monotonic() + timeout
    status = game / "smoke.status"
    try:
        while True:
            text = status.read_text(encoding="utf-8") if status.is_file() else ""
            if text == nonce + ":PASS":
                return
            if text.startswith(nonce + ":FAIL\n"):
                raise SmokeError("DragonRuby smoke suite failed: " + text.split("\n", 1)[1])
            code = process.poll()
            if code is not None:
                raise SmokeError(f"DragonRuby exited with {code} without a valid completion marker.")
            if time.monotonic() >= deadline:
                raise SmokeError("DragonRuby did not complete the smoke suite before the timeout.")
            time.sleep(0.05)
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        else:
            process.wait()


def run_checked(command: list[str], cwd: Path, log, timeout: float) -> None:
    try:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=timeout)
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        raise SmokeError(f"Build command failed: {error}") from error


def execute(sdk: Path, zig: str, runtime: Path | None, target: str | None,
            folder: str | None, timeout: float = 60, build_timeout: float = 300) -> dict:
    if not all(math.isfinite(n) and n > 0 for n in (timeout, build_timeout)):
        raise SmokeError("Timeouts must be finite positive numbers.")
    target, folder = selection(target, folder)
    sdk = sdk.resolve()
    runtime = validate_sdk(sdk, runtime)
    binary = shutil.which(zig)
    if binary is None:
        raise SmokeError("Zig executable not found.")
    binary = str(Path(binary).resolve())
    expected = (SAMPLE / ".zig-version").read_text().strip()
    version = subprocess.check_output([binary, "version"], text=True, timeout=15).strip()
    if version != expected:
        raise SmokeError(f"Expected Zig {expected}, found {version}.")

    with tempfile.TemporaryDirectory(prefix="dragonruby zig smoke ") as directory:
        work = Path(directory)
        game = work / "game"
        source = work / "source"
        source.mkdir()
        # Do not create caches or build outputs in the user's source checkout.
        for name in ("build.zig", ".zig-version"):
            shutil.copy2(SAMPLE / name, source / name)
        shutil.copytree(SAMPLE / "app", source / "app")
        shutil.copytree(SAMPLE / "tests", source / "tests")
        shutil.copytree(SAMPLE / "app", game / "app")
        shutil.copytree(SAMPLE / "tests", game / "tests")
        shutil.copytree(SAMPLE / "metadata", game / "metadata")
        nonce = uuid.uuid4().hex
        main = game / "app/main.rb"
        main.write_text(main.read_text() + probe_source(nonce), encoding="utf-8")
        log_path = work / "runtime.log"
        try:
            with log_path.open("wb") as log:
                run_checked([binary, "build", "extension", "--prefix", str(game),
                             "-Doptimize=ReleaseSafe", "-Dcpu=baseline", f"-Dtarget={target}",
                             f"-Dplatform={folder}", f"-Ddragonruby-root={sdk}"],
                            source, log, build_timeout)
                suffix = "dll" if "windows" in target else "dylib" if "macos" in target else "so"
                if not (game / "native" / folder / ("ext." + suffix)).is_file():
                    raise SmokeError("Build did not install the expected extension.")
                wait_for_result([str(runtime), str(game)], game, nonce, timeout, log)
        except (SmokeError, OSError) as error:
            with log_path.open("rb") as log:
                log.seek(max(0, log_path.stat().st_size - 8192))
                tail = log.read().decode("utf-8", errors="replace")
            raise SmokeError(f"{error}\n--- build/runtime log tail ---\n{tail}") from error
    return {"status": "passed", "zig": version, "target": target,
            "platform": folder, "frames": 60, "ruby_suite": "tests/smoke.rb",
            "validation": "SDK compilation, extension loading, Ruby smoke suite and sample tick loop",
            "visual_validation": "not performed"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--zig", default="zig")
    parser.add_argument("--target")
    parser.add_argument("--platform", dest="folder")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--build-timeout", type=float, default=300)
    args = parser.parse_args(argv)
    try:
        result = execute(args.sdk, args.zig, args.runtime, args.target, args.folder,
                         args.timeout, args.build_timeout)
    except (SmokeError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
