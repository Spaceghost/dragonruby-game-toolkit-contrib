"""Test SDK smoke orchestration, NOT DragonRuby or its proprietary ABI."""
from __future__ import annotations

import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SAMPLE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sdk_smoke", SAMPLE / "tools/sdk_smoke.py")
assert spec and spec.loader
sdk_smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk_smoke)


class SelectionTests(unittest.TestCase):
    def test_native_selection(self):
        for system, machine, expected in [
            ("Linux", "x86_64", ("x86_64-linux-gnu", "linux-amd64")),
            ("Darwin", "arm64", ("aarch64-macos", "macos")),
            ("Darwin", "x86_64", ("x86_64-macos", "macos")),
            ("Windows", "AMD64", ("x86_64-windows-gnu", "windows-amd64")),
        ]:
            with self.subTest(system=system, machine=machine):
                with patch.object(sdk_smoke.platform, "system", return_value=system), \
                     patch.object(sdk_smoke.platform, "machine", return_value=machine):
                    self.assertEqual(sdk_smoke.selection(None, None), expected)

    def test_unknown_host_does_not_guess_engine_directory(self):
        with patch.object(sdk_smoke.platform, "system", return_value="Linux"), \
             patch.object(sdk_smoke.platform, "machine", return_value="aarch64"):
            with self.assertRaises(sdk_smoke.SmokeError):
                sdk_smoke.selection(None, None)

    def test_explicit_target_and_folder(self):
        self.assertEqual(sdk_smoke.selection("x86_64-linux-gnu.2.28", "linux-amd64"),
                         ("x86_64-linux-gnu.2.28", "linux-amd64"))

    def test_reject_half_override_and_path_traversal(self):
        for target, folder in [("x86_64-linux-gnu", None), (None, "linux-amd64"),
                               ("x86_64-linux-gnu", "../native"), ("a", "/tmp"),
                               ("a;echo", "native"), ("a", "a\\b"), ("a", ".")]:
            with self.subTest(target=target, folder=folder):
                with self.assertRaises(sdk_smoke.SmokeError):
                    sdk_smoke.selection(target, folder)


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="test SDK with spaces ")
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name)
        (self.sdk / "include").mkdir()
        (self.sdk / "include/dragonruby.h").write_text("/* test placeholder */")
        (self.sdk / "include/mruby.h").write_text("/* test placeholder */")
        self.runtime = self.sdk / "runtime with spaces"
        self.runtime.write_text("not a real runtime")

    def test_accept_expected_header_locations(self):
        self.assertEqual(sdk_smoke.validate_sdk(self.sdk, self.runtime), self.runtime)
        (self.sdk / "include/dragonruby.h").rename(self.sdk / "dragonruby.h")
        (self.sdk / "mruby/include").mkdir(parents=True)
        (self.sdk / "include/mruby.h").rename(self.sdk / "mruby/include/mruby.h")
        self.assertEqual(sdk_smoke.validate_sdk(self.sdk, self.runtime), self.runtime)

    def test_reject_missing_header_or_executable(self):
        for path in [self.sdk / "include/dragonruby.h", self.sdk / "include/mruby.h", self.runtime]:
            with self.subTest(path=path):
                contents = path.read_text()
                path.unlink()
                with self.assertRaises(sdk_smoke.SmokeError):
                    sdk_smoke.validate_sdk(self.sdk, self.runtime)
                path.write_text(contents)

    def test_reject_test_contract_masquerading_as_sdk(self):
        (self.sdk / "include/dragonruby.h").write_text("#define DRB_ZIG_TEST_HOST 1")
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "not a DragonRuby SDK"):
            sdk_smoke.validate_sdk(self.sdk, self.runtime)

    def test_invalid_timeout_is_rejected_before_build(self):
        for number in (0, -1, float("nan"), float("inf")):
            with self.subTest(number=number), self.assertRaises(sdk_smoke.SmokeError):
                sdk_smoke.execute(self.sdk, "zig", self.runtime, None, None, timeout=number)

    def test_wrong_compiler_version_is_rejected(self):
        with patch.object(sdk_smoke.shutil, "which", return_value=sys.executable), \
             patch.object(sdk_smoke.subprocess, "check_output", return_value="0.1.0\n"):
            with self.assertRaisesRegex(sdk_smoke.SmokeError, "Expected Zig"):
                sdk_smoke.execute(self.sdk, "zig", self.runtime, "x86_64-linux-gnu", "linux-amd64")

    def test_missing_compiler_is_rejected(self):
        with patch.object(sdk_smoke.shutil, "which", return_value=None):
            with self.assertRaisesRegex(sdk_smoke.SmokeError, "not found"):
                sdk_smoke.execute(self.sdk, "zig", self.runtime, "x86_64-linux-gnu", "linux-amd64")

    def test_isolated_staging_and_argument_boundaries(self):
        original = (SAMPLE / "app/main.rb").read_bytes()
        temporary_paths = []

        def build(command, source, log, timeout):
            self.assertEqual(command[1:4], ["build", "extension", "--prefix"])
            game = Path(command[4])
            temporary_paths.extend([source, game])
            self.assertNotEqual(source, SAMPLE)
            self.assertIn(f"-Ddragonruby-root={self.sdk}", command)
            self.assertNotIn(str(self.runtime), command)
            self.assertTrue((source / "app/bridge.c").is_file())
            artifact = game / "native/linux-amd64/ext.so"
            artifact.parent.mkdir(parents=True)
            artifact.write_bytes(b"test artifact")

        def runtime(command, game, nonce, timeout, log):
            self.assertEqual(command, [str(self.runtime), str(game)])
            self.assertFalse((game / "smoke.status").exists())
            staged = (game / "app/main.rb").read_text()
            self.assertIn(nonce + ":PASS", staged)
            self.assertIn("zig_sample_tick args", staged)
            self.assertIn("== 60", staged)

        with patch.object(sdk_smoke.shutil, "which", return_value=sys.executable), \
             patch.object(sdk_smoke.subprocess, "check_output", return_value="0.16.0\n"), \
             patch.object(sdk_smoke, "run_checked", side_effect=build), \
             patch.object(sdk_smoke, "wait_for_result", side_effect=runtime):
            result = sdk_smoke.execute(self.sdk, "zig", self.runtime,
                                       "x86_64-linux-gnu", "linux-amd64")
        self.assertEqual(result["frames"], 60)
        self.assertEqual(result["visual_validation"], "not performed")
        self.assertEqual((SAMPLE / "app/main.rb").read_bytes(), original)
        self.assertTrue(all(not path.exists() for path in temporary_paths))

    def test_build_success_without_library_is_failure(self):
        with patch.object(sdk_smoke.shutil, "which", return_value=sys.executable), \
             patch.object(sdk_smoke.subprocess, "check_output", return_value="0.16.0\n"), \
             patch.object(sdk_smoke, "run_checked"), \
             patch.object(sdk_smoke, "wait_for_result") as run:
            with self.assertRaisesRegex(sdk_smoke.SmokeError, "expected extension"):
                sdk_smoke.execute(self.sdk, "zig", self.runtime,
                                   "x86_64-linux-gnu", "linux-amd64")
            run.assert_not_called()


class ProcessTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="process tests ")
        self.addCleanup(self.temp.cleanup)
        self.game = Path(self.temp.name)
        self.log = tempfile.TemporaryFile()
        self.addCleanup(self.log.close)

    def invoke(self, source, timeout=5):
        # Execute a real child, capturing it to prove cleanup on every outcome.
        real_popen = subprocess.Popen
        children = []

        def spawn(*args, **kwargs):
            process = real_popen(*args, **kwargs)
            children.append(process)
            return process

        try:
            with patch.object(sdk_smoke.subprocess, "Popen", side_effect=spawn):
                sdk_smoke.wait_for_result([sys.executable, "-c", source], self.game,
                                          "current", timeout, self.log)
        finally:
            self.assertEqual(len(children), 1)
            self.assertIsNotNone(children[0].poll(), "child left running")

    def test_pass_marker_terminates_child(self):
        self.invoke("from pathlib import Path; import time; "
                    "Path('smoke.status').write_text('current:PASS'); time.sleep(30)")

    def test_failure_marker_reports_message_and_terminates(self):
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "bad argument"):
            self.invoke("from pathlib import Path; import time; "
                        "Path('smoke.status').write_text('current:FAIL\\nbad argument'); time.sleep(30)")

    def test_zero_exit_is_not_a_pass(self):
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "exited with 0"):
            self.invoke("pass")

    def test_nonzero_exit_fails(self):
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "exited with 7"):
            self.invoke("raise SystemExit(7)")

    def test_wrong_nonce_is_ignored(self):
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "without a valid completion"):
            self.invoke("from pathlib import Path; Path('smoke.status').write_text('old:PASS')")

    def test_partial_marker_is_not_premature_success(self):
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "without a valid completion"):
            self.invoke("from pathlib import Path; Path('smoke.status').write_text('current:PA')")

    def test_timeout_kills_child(self):
        with self.assertRaisesRegex(sdk_smoke.SmokeError, "timeout"):
            self.invoke("import time; time.sleep(30)", timeout=0.2)

    def test_build_failure_and_timeout(self):
        for source, timeout in [("raise SystemExit(9)", 5), ("import time; time.sleep(30)", 0.2)]:
            with self.subTest(source=source), self.assertRaises(sdk_smoke.SmokeError):
                sdk_smoke.run_checked([sys.executable, "-c", source], self.game, self.log, timeout)


class CliTests(unittest.TestCase):
    def test_cli_failure_exit_code(self):
        with patch.object(sdk_smoke, "execute", side_effect=sdk_smoke.SmokeError("missing SDK")), \
             patch("sys.stderr", new_callable=io.StringIO) as stderr:
            self.assertEqual(sdk_smoke.main(["--sdk", "missing"]), 1)
            self.assertIn("missing SDK", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
