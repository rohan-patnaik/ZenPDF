#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Hash a private L005 acceptance bundle")
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    output = args.output.resolve()
    if args.check:
        existing = json.loads(output.read_text())
        excluded = {root / value for value in existing["excluded_paths"]}
        required_exclusions = {output, output.parent / "l005-evidence-index.json"}
        if excluded != required_exclusions:
            raise SystemExit("manifest exclusions are not the required self/index pair")
    else:
        excluded = {output, output.parent / "l005-evidence-index.json"}
    entries = []
    for path in sorted(root.rglob("*")):
        if path in excluded or "__pycache__" in path.parts:
            continue
        value = path.lstat()
        item = {
            "path": path.relative_to(root).as_posix(),
            "mode": f"{stat.S_IMODE(value.st_mode):04o}",
            "owner_matches_euid": value.st_uid == os.geteuid(),
            "group_matches_egid": value.st_gid == os.getegid(),
        }
        if stat.S_ISREG(value.st_mode):
            item.update({"type": "regular", "size": value.st_size, "sha256": sha256_file(path)})
        elif stat.S_ISDIR(value.st_mode):
            item.update({"type": "directory"})
        elif stat.S_ISLNK(value.st_mode):
            item.update({
                "type": "symlink",
                "target_sha256": hashlib.sha256(os.readlink(path).encode()).hexdigest(),
            })
        elif stat.S_ISFIFO(value.st_mode):
            item.update({"type": "fifo"})
        elif stat.S_ISSOCK(value.st_mode):
            item.update({"type": "socket"})
        else:
            item.update({"type": "other"})
        entries.append(item)
    payload = {
        "schema": 1,
        "root_redacted": True,
        "excluded_paths": sorted(path.relative_to(root).as_posix() for path in excluded),
        "entry_count": len(entries),
        "entries": entries,
    }
    data = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode()
    if args.check:
        if output.read_bytes() != data:
            raise SystemExit("manifest does not reproduce")
        print(f"PASS {output}")
        return 0
    descriptor = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600)
    try:
        os.write(descriptor, data)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
