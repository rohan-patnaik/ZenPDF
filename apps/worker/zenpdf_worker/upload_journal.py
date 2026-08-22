"""Durable recovery journal for worker output uploads."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
from typing import Any, Dict, List


class UploadJournal:
    """Persist upload reconciliation state with atomic, fsynced replacements."""

    def __init__(self, root: Path) -> None:
        self.root = root

    def ensure_ready(self) -> None:
        """Create and verify the private journal directory before an upload begins."""
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        os.chmod(self.root, 0o700)
        probe = self.root / ".write-test"
        with probe.open("wb") as handle:
            handle.write(b"ready")
            handle.flush()
            os.fsync(handle.fileno())
        probe.unlink()
        self._fsync_root()

    @staticmethod
    def _key(pending_upload_id: str) -> str:
        return hashlib.sha256(pending_upload_id.encode("utf-8")).hexdigest()

    def _path(self, pending_upload_id: str) -> Path:
        return self.root / f"{self._key(pending_upload_id)}.json"

    def save(self, entry: Dict[str, Any]) -> None:
        """Atomically persist one complete recovery entry."""
        pending_upload_id = entry.get("pendingUploadId")
        if not isinstance(pending_upload_id, str) or not pending_upload_id:
            raise ValueError("pendingUploadId is required for upload recovery")
        self.ensure_ready()
        target = self._path(pending_upload_id)
        temporary = target.with_suffix(".tmp")
        encoded = json.dumps(entry, sort_keys=True, separators=(",", ":")).encode()
        with temporary.open("wb") as handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, target)
        self._fsync_root()

    def remove(self, pending_upload_id: str) -> None:
        """Remove a confirmed entry durably."""
        target = self._path(pending_upload_id)
        try:
            target.unlink()
        except FileNotFoundError:
            return
        self._fsync_root()

    def entries(self) -> List[Dict[str, Any]]:
        """Return valid entries, preserving malformed files for operator recovery."""
        if not self.root.exists():
            return []
        recovered: List[Dict[str, Any]] = []
        for path in sorted(self.root.glob("*.json")):
            try:
                value = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            if isinstance(value, dict):
                recovered.append(value)
        return recovered

    def _fsync_root(self) -> None:
        descriptor = os.open(self.root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
