#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Hash every regular ZenPDF package member")
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    package = args.package.resolve()
    names = subprocess.check_output(["bsdtar", "-tf", str(package)]).decode().splitlines()
    members = []
    for name in names:
        if name.endswith("/"):
            continue
        data = subprocess.check_output(["bsdtar", "-xOf", str(package), name])
        members.append({
            "path": name,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })
    payload = {
        "schema": 1,
        "package_name": package.name,
        "package_sha256": sha256_file(package),
        "regular_member_count": len(members),
        "members": members,
    }
    descriptor = os.open(
        args.output.resolve(), os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o600
    )
    try:
        data = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode()
        os.write(descriptor, data)
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    print(args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
