from __future__ import annotations

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from validate_desktop_governance import (  # noqa: E402
    GovernanceError,
    parse_hash_manifest,
    validate_evidence_references,
    validate_matrix,
    validate_product_containers,
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
            validate_matrix(self.plan.replace("| Partial | 27 |", "| Partial | 26 |"))

    def test_rejects_missing_evidence_path(self) -> None:
        broken = self.plan.replace("`LoggingTest.cpp`", "`MissingTest.cpp`", 1)
        with self.assertRaisesRegex(GovernanceError, "does not resolve uniquely"):
            validate_evidence_references(ROOT, broken)

    def test_rejects_missing_evidence_symbol(self) -> None:
        broken = self.plan.replace(
            "LocalStateTest.cpp::clearingPurgesPathsFromDatabaseFiles",
            "LocalStateTest.cpp::missingEvidenceSymbol",
            1,
        )
        with self.assertRaisesRegex(GovernanceError, "evidence symbol is absent"):
            validate_evidence_references(ROOT, broken)

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

    def test_rejects_missing_product_docker_build(self) -> None:
        broken = self.workflow.replace(
            '--tag "zenpdf-web-ci:$GITHUB_SHA" apps/web',
            '--tag "zenpdf-web-ci:$GITHUB_SHA" missing-web-context',
        )
        with self.assertRaisesRegex(GovernanceError, "web product Dockerfile"):
            validate_workflow(broken, self.lock)

    def test_rejects_product_image_push(self) -> None:
        broken = self.workflow.replace(
            '--tag "zenpdf-worker-ci:$GITHUB_SHA" apps/worker',
            '--tag "zenpdf-worker-ci:$GITHUB_SHA" apps/worker\n          docker push image',
        )
        with self.assertRaisesRegex(GovernanceError, "must not publish"):
            validate_workflow(broken, self.lock)

    def product_root(self) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        for relative in (
            "apps/web/Dockerfile",
            "apps/worker/Dockerfile",
            "apps/worker/apt-packages.lock",
        ):
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, target)
        return temporary, root

    def test_rejects_mutable_product_container(self) -> None:
        temporary, root = self.product_root()
        self.addCleanup(temporary.cleanup)
        dockerfile = root / "apps/worker/Dockerfile"
        dockerfile.write_text(
            dockerfile.read_text(encoding="utf-8").replace(
                "python:3.11.13-slim-bookworm@sha256:86adf8dbadc3d6e82ee5dd2c74bec2e1c2467cdad47886280501df722372d2e1",
                "python:3.11-slim",
            ),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(GovernanceError, "base image is not locked"):
            validate_product_containers(root, self.lock)

    def test_rejects_unhashed_product_worker_install(self) -> None:
        temporary, root = self.product_root()
        self.addCleanup(temporary.cleanup)
        dockerfile = root / "apps/worker/Dockerfile"
        dockerfile.write_text(
            dockerfile.read_text(encoding="utf-8").replace("--require-hashes", ""),
            encoding="utf-8",
        )
        with self.assertRaisesRegex(GovernanceError, "hashed Python resolution"):
            validate_product_containers(root, self.lock)


if __name__ == "__main__":
    unittest.main()
