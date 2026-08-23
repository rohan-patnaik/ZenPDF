#!/usr/bin/env python3
"""Emit a bounded, path-redacted post-shutdown L016 state audit."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def contains(path: Path, needle: bytes) -> bool:
    overlap = max(0, len(needle) - 1)
    tail = b""
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            value = tail + chunk
            if needle in value:
                return True
            tail = value[-overlap:] if overlap else b""
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True)
    parser.add_argument("--case", action="append", required=True, help="label=state-leaf")
    args = parser.parse_args()

    fixture = Path(args.fixture).resolve(strict=True)
    sentinel = os.fsencode(str(fixture))
    effective_uid = os.geteuid()
    result: dict[str, object] = {
        "recorded_utc": datetime.now(timezone.utc).isoformat(),
        "fixture_sha256": sha256(fixture),
        "installed_executable_sha256": sha256(Path("/usr/bin/zenpdf")),
        "cases": [],
    }

    for specification in args.case:
        label, raw_path = specification.split("=", 1)
        leaf = Path(raw_path)
        leaf_stat = leaf.lstat()
        case: dict[str, object] = {
            "label": label,
            "leaf": {
                "directory": stat.S_ISDIR(leaf_stat.st_mode),
                "mode": f"{stat.S_IMODE(leaf_stat.st_mode):04o}",
                "effective_uid_owned": leaf_stat.st_uid == effective_uid,
            },
            "sqlite": [],
            "sentinel_absent_from_diagnostics": True,
        }
        for name in ("state.sqlite3", "state.sqlite3-wal", "state.sqlite3-shm"):
            path = leaf / name
            if not path.exists():
                case["sqlite"].append({"name": name, "present": False})
                continue
            metadata = path.lstat()
            case["sqlite"].append(
                {
                    "name": name,
                    "present": True,
                    "regular": stat.S_ISREG(metadata.st_mode),
                    "mode": f"{stat.S_IMODE(metadata.st_mode):04o}",
                    "effective_uid_owned": metadata.st_uid == effective_uid,
                    "links": metadata.st_nlink,
                    "sha256": sha256(path),
                    "sentinel_absent": not contains(path, sentinel),
                }
            )
        logs = leaf / "logs"
        if logs.is_dir():
            for path in logs.rglob("*"):
                if path.is_file() and contains(path, sentinel):
                    case["sentinel_absent_from_diagnostics"] = False
        result["cases"].append(case)

    residual = False
    for comm in Path("/proc").glob("[0-9]*/comm"):
        try:
            if comm.read_text(encoding="utf-8").strip() == "zenpdf":
                residual = True
                break
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
    result["no_residual_zenpdf_process"] = not residual
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
