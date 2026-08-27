from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "l004_wayland_acceptance",
    ROOT / "scripts" / "acceptance" / "l004_wayland_acceptance.py",
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class L004WaylandAcceptanceTest(unittest.TestCase):
    def test_strip_schema_preserves_window_snapshot(self) -> None:
        source = b"[schema]\nversion=1\n[window]\ngeometry=@ByteArray(ok)\nstate=@ByteArray(ok)\n"
        self.assertEqual(
            MODULE.strip_schema(source),
            b"[window]\ngeometry=@ByteArray(ok)\nstate=@ByteArray(ok)\n",
        )

    def test_strip_schema_rejects_unusable_seed(self) -> None:
        with self.assertRaisesRegex(AssertionError, "window section"):
            MODULE.strip_schema(b"[schema]\nversion=1\n")

    def test_address_rejects_dispatch_injection(self) -> None:
        self.assertEqual(MODULE.validate_address("0x123abc"), "0x123abc")
        with self.assertRaisesRegex(AssertionError, "compositor address"):
            MODULE.validate_address('0x1\" }) ; hl.dsp.exit()')

    def test_metadata_reports_private_regular_file_without_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "snapshot"
            path.write_bytes(b"snapshot")
            path.chmod(0o600)
            value = MODULE.metadata(path)
        self.assertTrue(value["regular"])
        self.assertEqual(value["mode"], "0600")
        self.assertNotIn("path", value)


if __name__ == "__main__":
    unittest.main()

