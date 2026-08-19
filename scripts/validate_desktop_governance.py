#!/usr/bin/env python3
"""Validate the Phase 0 matrix, dependency pins, and direct-component SBOM."""

from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "docs" / "LOCAL_ACROBAT_PARITY_PLAN.md"
LOCK = ROOT / "apps" / "desktop" / "dependencies.lock.json"
SBOM = ROOT / "apps" / "desktop" / "sbom.cdx.json"
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
ALLOWED = {"Not started", "Partial", "Verified", "Blocked"}
EXPECTED = Counter({"Not started": 45, "Partial": 29, "Verified": 1, "Blocked": 1})


def fail(message: str) -> None:
    raise SystemExit(f"desktop governance validation failed: {message}")


def matrix_rows() -> list[list[str]]:
    rows = []
    for line in PLAN.read_text(encoding="utf-8").splitlines():
        if re.match(r"^\| L\d{3} \|", line):
            rows.append([cell.strip() for cell in line.strip("|").split("|")])
    if len(rows) != 76:
        fail(f"expected 76 capability rows, found {len(rows)}")
    ids = [row[0] for row in rows]
    expected_ids = [f"L{number:03d}" for number in range(1, 77)]
    if ids != expected_ids:
        fail("capability IDs must be unique and contiguous L001-L076")
    if any(len(row) != 7 for row in rows):
        fail("every capability row must have seven columns")
    statuses = Counter(row[4] for row in rows)
    if set(statuses) - ALLOWED:
        fail(f"unsupported statuses: {sorted(set(statuses) - ALLOWED)}")
    if statuses != EXPECTED:
        fail(f"status counts are {dict(statuses)}, expected {dict(EXPECTED)}")
    return rows


def dependency_inventory() -> None:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    sbom = json.loads(SBOM.read_text(encoding="utf-8"))
    workflow = WORKFLOW.read_text(encoding="utf-8")
    if lock.get("schema") != 1 or sbom.get("bomFormat") != "CycloneDX" or sbom.get("specVersion") != "1.5":
        fail("unsupported lock or SBOM schema")
    for name, item in lock["ci"].items():
        for field in ("version", "sha256"):
            if field == "sha256" and name == "qt":
                continue
            value = item.get(field)
            if value and value not in workflow:
                fail(f"{name} {field} is not pinned in desktop CI")
        digest = item.get("sha256")
        if digest and not re.fullmatch(r"[0-9a-f]{64}", digest):
            fail(f"{name} has an invalid SHA-256")
    action_commit = lock["ci"]["qt"]["installerActionCommit"]
    if action_commit not in workflow:
        fail("Qt installer action is not commit-pinned")
    locked = {(item["name"], item["version"], item["license"]) for item in lock["runtime"]}
    components = set()
    for item in sbom["components"]:
        license_item = item["licenses"][0]
        license_value = license_item.get("expression") or license_item["license"]["id"]
        components.add((item["name"], item["version"], license_value))
    if components != locked:
        fail("SBOM components do not exactly match the locked runtime inventory")


if __name__ == "__main__":
    matrix_rows()
    dependency_inventory()
    print("desktop governance: 76 rows and dependency inventory valid")
