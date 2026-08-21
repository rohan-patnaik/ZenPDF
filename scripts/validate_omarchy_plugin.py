#!/usr/bin/env python3
"""Validate the small, security-relevant Omarchy launcher contract."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "apps" / "desktop" / "packaging" / "zenpdf-launch"


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
            'command: ["sh", "-c", "command -v zenpdf-launch >/dev/null 2>&1"]':
                "short zenpdf-launch PATH preflight is missing",
            'Quickshell.execDetached(["zenpdf-launch"])':
                "ZenPDF must be launched through the detached Quickshell API",
            "The ZenPDF launcher is missing.": "missing-launcher diagnostic is missing",
        }
        errors.extend(message for snippet, message in required.items() if snippet not in qml)
        if '"-lc"' in qml:
            errors.append("launcher must not use a login shell")
        if "exec zenpdf" in qml:
            errors.append("a tracked Process must not own the ZenPDF application")

    if not LAUNCHER.is_file():
        errors.append("independent native launcher is missing")
    else:
        launcher = LAUNCHER.read_text(encoding="utf-8")
        launcher_required = {
            "command -v zenpdf": "launcher must diagnose a missing native binary",
            'nohup "$binary"': "launcher must detach the native binary from its caller",
            'kill -0 "$pid"': "launcher must detect an early native-process exit",
            "run zenpdf in a terminal": "early-exit diagnostic must be actionable",
        }
        errors.extend(
            message for snippet, message in launcher_required.items() if snippet not in launcher
        )
        if "sudo" in launcher or "pkexec" in launcher:
            errors.append("launcher must not elevate privileges")

    if errors:
        for error in errors:
            print(f"error: {error}")
        return 1
    print("Omarchy launcher contract is valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
