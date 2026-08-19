#!/usr/bin/env python3
"""Validate the Phase 0 matrix, immutable CI inputs, and direct-package evidence."""

from __future__ import annotations

import hashlib
import json
import re
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PLAN = ROOT / "docs" / "LOCAL_ACROBAT_PARITY_PLAN.md"
LOCK = ROOT / "apps" / "desktop" / "dependencies.lock.json"
SBOM = ROOT / "apps" / "desktop" / "sbom.cdx.json"
POLICY = ROOT / "apps" / "desktop" / "license-policy.json"
NOTICES = ROOT / "apps" / "desktop" / "THIRD_PARTY_NOTICES.md"
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
ALLOWED_STATUSES = {"Not started", "Partial", "Verified", "Blocked"}
OWNER_PATTERN = re.compile(r"^M[0-6] / [A-Z][A-Za-z -]+$")
SPDX_ID_PATTERN = re.compile(r"^(?:[A-Za-z0-9][A-Za-z0-9.+-]*|LicenseRef-[A-Za-z0-9.-]+)$")


class GovernanceError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GovernanceError(message)


def sha256(path: Path) -> str:
    canonical_text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    return hashlib.sha256(canonical_text.encode("utf-8")).hexdigest()


def validate_matrix(text: str) -> Counter[str]:
    rows: list[list[str]] = []
    for line in text.splitlines():
        if re.match(r"^\| L\d{3} \|", line):
            rows.append([cell.strip() for cell in line.strip("|").split("|")])
    require(len(rows) == 76, f"expected 76 capability rows, found {len(rows)}")
    require(
        [row[0] for row in rows] == [f"L{number:03d}" for number in range(1, 77)],
        "capability IDs must be unique and contiguous L001-L076",
    )
    for row in rows:
        require(len(row) == 7, f"{row[0]} must have seven columns")
        require(all(row), f"{row[0]} contains an empty field")
        require(OWNER_PATTERN.fullmatch(row[2]) is not None, f"{row[0]} has invalid owner format")
        require(row[3] not in {"-", "—"}, f"{row[0]} must name dependencies or 'None'")
        require(row[4] in ALLOWED_STATUSES, f"{row[0]} has unsupported status {row[4]!r}")
        require(row[5] not in {"-", "—"}, f"{row[0]} must name test evidence")
        require(row[6] not in {"-", "—"}, f"{row[0]} must name interoperability evidence")

    summary = {}
    for status in sorted(ALLOWED_STATUSES):
        match = re.search(rf"^\| {re.escape(status)} \| (\d+) \|$", text, re.MULTILINE)
        require(match is not None, f"count summary is missing {status}")
        summary[status] = int(match.group(1))
    total = re.search(r"^\| \*\*Total\*\* \| \*\*(\d+)\*\* \|$", text, re.MULTILINE)
    require(total is not None and int(total.group(1)) == 76, "count summary total must be 76")
    statuses = Counter(row[4] for row in rows)
    require(statuses == Counter(summary), f"matrix counts {dict(statuses)} do not match summary {summary}")
    return statuses


def parse_hash_manifest(text: str) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  ([^/\s]+)", line)
        require(match is not None, f"invalid Arch hash-manifest line: {line!r}")
        digest, filename = match.groups()
        require(filename not in entries, f"duplicate hash-manifest filename {filename}")
        entries[filename] = digest
    return entries


def validate_spdx_expression(expression: str, allowed: set[str]) -> None:
    require(expression in allowed, f"license expression is not policy-approved: {expression}")
    tokens = re.findall(r"LicenseRef-[A-Za-z0-9.-]+|[A-Za-z0-9][A-Za-z0-9.+-]*", expression)
    identifiers = [token for token in tokens if token not in {"AND", "OR", "WITH"}]
    require(bool(identifiers), f"empty SPDX expression: {expression}")
    require(all(SPDX_ID_PATTERN.fullmatch(item) for item in identifiers), f"invalid SPDX syntax: {expression}")


def validate_workflow(text: str, lock: dict) -> None:
    require("ubuntu-latest" not in text and ":latest" not in text, "mutable latest runner/container is forbidden")
    uses = re.findall(r"^\s*uses:\s*([^@\s]+)@([^\s]+)\s*$", text, re.MULTILINE)
    expected_action = lock["actions"]["checkout"]
    require(bool(uses), "workflow has no pinned actions")
    for repository, revision in uses:
        require(repository == expected_action["repository"], f"unapproved action {repository}")
        require(revision == expected_action["commit"], f"{repository} is not pinned to the locked commit")
        require(re.fullmatch(r"[0-9a-f]{40}", revision) is not None, f"{repository} is not full-SHA pinned")

    images = set(re.findall(r"^\s*image:\s*([^:\s]+):([^@\s]+)@(sha256:[0-9a-f]{64})\s*$", text, re.MULTILINE))
    expected_images = {
        (item["image"], item["tag"], item["digest"]) for item in lock["containers"].values()
    }
    require(images == expected_images, f"workflow containers {images} do not match lock {expected_images}")

    snapshot = re.search(r'^\s*ARCH_SNAPSHOT:\s*"([^"]+)"\s*$', text, re.MULTILINE)
    require(snapshot is not None and snapshot.group(1) == lock["arch"]["snapshot"], "Arch snapshot mismatch")
    url_template = lock["arch"]["repositoryUrl"].replace(lock["arch"]["snapshot"], "%s")
    require(url_template in text, "workflow Arch repository template does not match lock")
    require(lock["arch"]["hashManifest"] in text, "workflow does not verify the locked Arch hash manifest")
    require("--require-hashes" in text, "worker install must enforce hashes")
    require("npm run build" in text, "web production build is required")

    jobs_text = text.split("\njobs:\n", 1)
    require(len(jobs_text) == 2, "workflow jobs section is missing")
    lines = jobs_text[1].splitlines()
    for index, line in enumerate(lines):
        if re.match(r"^  [a-z0-9-]+:$", line):
            block = "\n".join(lines[index + 1 : index + 8])
            require(re.search(r"^    timeout-minutes:\s*\d+", block, re.MULTILINE) is not None,
                    f"job {line.strip(': ')} lacks a timeout")
        if re.match(r"^\s{6}- name:", line):
            block_lines = []
            for following in lines[index + 1 :]:
                if re.match(r"^\s{6}- name:", following):
                    break
                block_lines.append(following)
            block = "\n".join(block_lines)
            require(re.search(r"^\s{8}timeout-minutes:\s*\d+", block, re.MULTILINE) is not None,
                    f"step {line.strip()} lacks a timeout")


def validate_dependency_evidence(root: Path = ROOT) -> None:
    lock = json.loads((root / LOCK.relative_to(ROOT)).read_text(encoding="utf-8"))
    sbom = json.loads((root / SBOM.relative_to(ROOT)).read_text(encoding="utf-8"))
    policy = json.loads((root / POLICY.relative_to(ROOT)).read_text(encoding="utf-8"))
    notices = (root / NOTICES.relative_to(ROOT)).read_text(encoding="utf-8")
    workflow = (root / WORKFLOW.relative_to(ROOT)).read_text(encoding="utf-8")
    require(lock.get("schema") == 2, "unsupported dependency-lock schema")
    validate_workflow(workflow, lock)

    for item in lock["locks"].values():
        path = root / item["path"]
        require(path.is_file(), f"missing lock file {item['path']}")
        require(sha256(path) == item["sha256"], f"lock-file digest mismatch for {item['path']}")
    worker_lock = (root / lock["locks"]["worker"]["path"]).read_text(encoding="utf-8")
    requirement_starts = [line for line in worker_lock.splitlines() if line and not line[0].isspace() and not line.startswith("#")]
    require(bool(requirement_starts), "worker lock has no resolved requirements")
    require(all("==" in line for line in requirement_starts), "worker lock contains an unresolved requirement")
    require("--hash=sha256:" in worker_lock, "worker lock contains no hashes")

    manifest = parse_hash_manifest((root / lock["arch"]["hashManifest"]).read_text(encoding="utf-8"))
    expected_manifest = {item["filename"]: item["sha256"] for item in lock["arch"]["packages"]}
    require(manifest == expected_manifest, "Arch hash manifest does not exactly match structured package lock")
    for item in lock["arch"]["packages"]:
        require(re.fullmatch(r"[0-9a-f]{64}", item["sha256"]) is not None, f"invalid hash for {item['name']}")
        require(item["version"] in item["filename"], f"{item['name']} filename/version association is invalid")

    require(sbom.get("bomFormat") == "CycloneDX" and sbom.get("specVersion") == "1.5",
            "unsupported SBOM format")
    properties = {item["name"]: item["value"] for item in sbom["metadata"]["properties"]}
    require(properties.get("zenpdf:inventory-scope") == "direct-arch-packages", "SBOM scope must be explicit")
    require(properties.get("zenpdf:transitive-inventory-complete") == "false",
            "Phase 0 SBOM must not claim complete transitives")
    require(policy.get("requireTransitiveInventoryForRelease") is True, "release policy must require transitives")
    require(policy.get("policy"), "license policy rationale is empty")
    allowed = set(policy["allowedExpressions"])
    runtime_names = {"qpdf", "qt6-base", "qt6-webengine"}
    packages = {item["name"]: item for item in lock["arch"]["packages"] if item["name"] in runtime_names}
    components = {item["name"]: item for item in sbom["components"]}
    require(set(components) == runtime_names, "SBOM must exactly cover direct runtime packages")
    for name, component in components.items():
        package = packages[name]
        require(component["version"] == package["version"], f"SBOM version mismatch for {name}")
        require(component["hashes"] == [{"alg": "SHA-256", "content": package["sha256"]}],
                f"SBOM artifact hash mismatch for {name}")
        expression = component["licenses"][0]["expression"]
        validate_spdx_expression(expression, allowed)
        for required_text in (name, package["version"], package["sha256"], expression):
            require(required_text in notices, f"notices omit {required_text}")


def main() -> None:
    try:
        counts = validate_matrix(PLAN.read_text(encoding="utf-8"))
        validate_dependency_evidence()
    except (GovernanceError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise SystemExit(f"desktop governance validation failed: {error}") from error
    print(f"desktop governance: 76 rows valid; statuses={dict(counts)}; direct-package evidence valid")


if __name__ == "__main__":
    main()
