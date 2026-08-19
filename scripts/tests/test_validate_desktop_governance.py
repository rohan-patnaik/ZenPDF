from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from validate_desktop_governance import (  # noqa: E402
    GovernanceError,
    parse_hash_manifest,
    validate_matrix,
    validate_workflow,
)


class DesktopGovernanceNegativeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.plan = (ROOT / "docs" / "LOCAL_ACROBAT_PARITY_PLAN.md").read_text(encoding="utf-8")
        cls.workflow = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        cls.lock = json.loads(
            (ROOT / "apps" / "desktop" / "dependencies.lock.json").read_text(encoding="utf-8")
        )

    def test_current_matrix_is_valid(self) -> None:
        self.assertEqual(sum(validate_matrix(self.plan).values()), 76)

    def test_rejects_missing_capability(self) -> None:
        line = next(line for line in self.plan.splitlines() if line.startswith("| L076 |"))
        with self.assertRaisesRegex(GovernanceError, "expected 76"):
            validate_matrix(self.plan.replace(line, ""))

    def test_rejects_empty_field(self) -> None:
        with self.assertRaisesRegex(GovernanceError, "empty field"):
            validate_matrix(
                self.plan.replace(
                    "| L001 | Native Arch/Wayland application launch |",
                    "| L001 |  |",
                    1,
                )
            )

    def test_rejects_bad_owner(self) -> None:
        with self.assertRaisesRegex(GovernanceError, "owner format"):
            validate_matrix(self.plan.replace("M0 / Platform", "platform-team", 1))

    def test_rejects_stale_summary(self) -> None:
        with self.assertRaisesRegex(GovernanceError, "do not match summary"):
            validate_matrix(self.plan.replace("| Partial | 30 |", "| Partial | 29 |"))

    def test_rejects_malformed_hash_manifest(self) -> None:
        with self.assertRaisesRegex(GovernanceError, "invalid Arch hash"):
            parse_hash_manifest("not-a-digest  package.pkg.tar.zst\n")

    def test_rejects_mutable_action_tag(self) -> None:
        sha = self.lock["actions"]["checkout"]["commit"]
        with self.assertRaisesRegex(GovernanceError, "not pinned"):
            validate_workflow(self.workflow.replace(f"actions/checkout@{sha}", "actions/checkout@v4"), self.lock)

    def test_rejects_changed_container_digest(self) -> None:
        digest = self.lock["containers"]["web"]["digest"]
        replacement = "sha256:" + "0" * 64
        with self.assertRaisesRegex(GovernanceError, "do not match lock"):
            validate_workflow(self.workflow.replace(digest, replacement), self.lock)


if __name__ == "__main__":
    unittest.main()
