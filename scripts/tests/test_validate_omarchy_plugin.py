from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "apps" / "desktop" / "packaging" / "zenpdf-launch"


class LauncherTest(unittest.TestCase):
    def _run(self, zenpdf_body: str | None) -> tuple[subprocess.CompletedProcess[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        bin_dir = root / "bin"
        bin_dir.mkdir()
        if zenpdf_body is not None:
            binary = bin_dir / "zenpdf"
            binary.write_text(f"#!/bin/sh\n{zenpdf_body}\n", encoding="utf-8")
            binary.chmod(0o700)
        env = os.environ.copy()
        env.update(
            {
                "HOME": str(root),
                "PATH": f"{bin_dir}:/usr/bin:/bin",
                "XDG_STATE_HOME": str(root / "state"),
                "ZENPDF_LAUNCH_GRACE_SECONDS": "0.05",
            }
        )
        result = subprocess.run(
            ["sh", str(LAUNCHER)], env=env, text=True, capture_output=True, timeout=3
        )
        return result, root / "state" / "zenpdf" / "launcher.log"

    def test_missing_binary_is_actionable_and_persisted(self) -> None:
        result, log = self._run(None)
        self.assertEqual(result.returncode, 127)
        self.assertIn("Install the ZenPDF Arch package", result.stderr)
        self.assertIn("Install the ZenPDF Arch package", log.read_text(encoding="utf-8"))

    def test_early_failure_is_actionable_and_includes_native_diagnostic(self) -> None:
        result, log = self._run("echo native-start-failure >&2\nexit 23")
        self.assertEqual(result.returncode, 23)
        self.assertIn("exited during startup (status 23)", result.stderr)
        text = log.read_text(encoding="utf-8")
        self.assertIn("native-start-failure", text)
        self.assertIn("run zenpdf in a terminal", text)


if __name__ == "__main__":
    unittest.main()
