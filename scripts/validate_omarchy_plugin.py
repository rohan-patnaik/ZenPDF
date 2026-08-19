#!/usr/bin/env python3
"""Validate the small, security-relevant Omarchy launcher contract."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    manifest = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
    entry_point = manifest.get("entryPoints", {}).get("menu")
    errors: list[str] = []
    if entry_point != "Plugin.qml":
        errors.append("manifest menu entry point must remain Plugin.qml")

    plugin_path = ROOT / str(entry_point)
    if not plugin_path.is_file():
        errors.append("manifest menu entry point does not exist")
    else:
        qml = plugin_path.read_text(encoding="utf-8")
        required = {
            'command: ["sh", "-c", "command -v zenpdf >/dev/null 2>&1"]':
                "short zenpdf PATH preflight is missing",
            'Quickshell.execDetached(["zenpdf"])':
                "ZenPDF must be launched through the detached Quickshell API",
            "ZenPDF is not installed or is not available in PATH.":
                "missing-binary diagnostic is missing",
        }
        errors.extend(message for snippet, message in required.items() if snippet not in qml)
        if '"-lc"' in qml:
            errors.append("launcher must not use a login shell")
        if "exec zenpdf" in qml:
            errors.append("a tracked Process must not own the ZenPDF application")

    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print("Omarchy launcher contract is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
