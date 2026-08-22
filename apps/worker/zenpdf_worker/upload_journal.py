"""Durable recovery journal for worker output uploads."""

from __future__ import annotations

import hashlib
import json
import os
import stat
import uuid
from pathlib import Path
from typing import Any, Dict, List


class JournalCapacityError(RuntimeError):
    """Raised before upload when durable recovery capacity is exhausted."""


def _bounded_env_int(name: str, default: int, hard_max: int) -> int:
    try:
        value = int(os.environ.get(name, str(default)))
    except ValueError:
        return default
    return min(max(value, 1), hard_max)


class UploadJournal:
    """Persist upload reconciliation state with atomic, fsynced replacements."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.max_entries = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES", 1024, 10_000
        )
        self.max_total_bytes = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_MAX_BYTES", 8 * 1024 * 1024, 64 * 1024 * 1024
        )
        self.max_entry_bytes = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_MAX_ENTRY_BYTES", 4096, 64 * 1024
        )
        self.batch_size = _bounded_env_int(
            "ZENPDF_UPLOAD_RECOVERY_BATCH_SIZE", 32, 256
        )

    def ensure_ready(self) -> None:
        """Create and verify the private journal directory before an upload begins."""
        if self.root.is_symlink():
            raise RuntimeError("Upload journal directory must not be a symbolic link")
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        os.chmod(self.root, 0o700)
        probe = self.root / f".write-test-{os.getpid()}-{uuid.uuid4().hex}"
        try:
            descriptor = os.open(probe, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(descriptor, "wb") as handle:
                handle.write(b"ready")
                handle.flush()
                os.fsync(handle.fileno())
        finally:
            try:
                probe.unlink()
            except FileNotFoundError:
                pass
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
        if entry.get("action") not in {"register", "uploaded", "discard"}:
            raise ValueError("Invalid upload recovery action")
        self.ensure_ready()
        target = self._path(pending_upload_id)
        encoded = json.dumps(entry, sort_keys=True, separators=(",", ":")).encode()
        if len(encoded) > self.max_entry_bytes:
            raise JournalCapacityError("Upload recovery entry exceeds size limit")
        if target.exists():
            try:
                previous_size = target.stat().st_size
            except OSError:
                previous_size = 0
            self.ensure_capacity(
                required_entries=0,
                required_bytes=max(len(encoded) - previous_size, 0),
            )
        else:
            self.ensure_capacity(required_entries=1, required_bytes=len(encoded))
        temporary = self.root / f".{target.name}.{uuid.uuid4().hex}.tmp"
        try:
            descriptor = os.open(
                temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600
            )
            with os.fdopen(descriptor, "wb") as handle:
                handle.write(encoded)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary, target)
            os.chmod(target, 0o600)
            self._fsync_root()
        finally:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass

    def ensure_capacity(
        self, required_entries: int = 1, required_bytes: int | None = None
    ) -> None:
        """Refuse new uploads before their recovery records could exceed bounds."""
        self.ensure_ready()
        count = 0
        total_bytes = 0
        with os.scandir(self.root) as entries:
            for item in entries:
                if not item.name.endswith(".json"):
                    continue
                count += 1
                try:
                    total_bytes += item.stat(follow_symlinks=False).st_size
                except OSError:
                    pass
                if count + required_entries > self.max_entries:
                    raise JournalCapacityError("Upload recovery journal is full")
                if total_bytes > self.max_total_bytes:
                    raise JournalCapacityError("Upload recovery journal byte limit reached")
        additional_bytes = self.max_entry_bytes if required_bytes is None else required_bytes
        projected_bytes = total_bytes + additional_bytes
        if count + required_entries > self.max_entries:
            raise JournalCapacityError("Upload recovery journal is full")
        if projected_bytes > self.max_total_bytes:
            raise JournalCapacityError("Upload recovery journal byte limit reached")

    def remove(self, pending_upload_id: str) -> None:
        """Remove a confirmed entry durably."""
        target = self._path(pending_upload_id)
        try:
            target.unlink()
        except FileNotFoundError:
            return
        self._fsync_root()

    def load(self, pending_upload_id: str) -> Dict[str, Any] | None:
        """Load one entry directly by its opaque pending-upload identifier."""
        return self._read_entry(self._path(pending_upload_id))

    def entries(
        self, limit: int | None = None, ready_at: int | None = None
    ) -> List[Dict[str, Any]]:
        """Read a bounded recovery batch and quarantine hostile files."""
        if not self.root.exists():
            return []
        read_limit = min(max(limit or self.batch_size, 1), self.batch_size)
        scan_limit = self.max_entries + 1
        recovered: List[Dict[str, Any]] = []
        scanned = 0
        with os.scandir(self.root) as entries:
            for item in entries:
                if not item.name.endswith(".json"):
                    continue
                scanned += 1
                value = self._read_entry(Path(item.path))
                if value is not None:
                    next_attempt_at = value.get("nextAttemptAt")
                    if not (
                        ready_at is not None
                        and isinstance(next_attempt_at, (int, float))
                        and next_attempt_at > ready_at
                    ):
                        recovered.append(value)
                if len(recovered) >= read_limit or scanned >= scan_limit:
                    break
        return recovered

    def has_entries(self) -> bool:
        """Check for pending work without loading journal content."""
        if not self.root.exists():
            return False
        with os.scandir(self.root) as entries:
            return any(item.name.endswith(".json") for item in entries)

    def _read_entry(self, path: Path) -> Dict[str, Any] | None:
        try:
            metadata = path.lstat()
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_size > self.max_entry_bytes:
                self._quarantine(path)
                return None
            with path.open("rb") as handle:
                encoded = handle.read(self.max_entry_bytes + 1)
            if len(encoded) > self.max_entry_bytes:
                self._quarantine(path)
                return None
            value = json.loads(encoded)
            if (
                not isinstance(value, dict)
                or not isinstance(value.get("pendingUploadId"), str)
                or value.get("action") not in {"register", "uploaded", "discard"}
            ):
                self._quarantine(path)
                return None
            os.chmod(path, 0o600)
            return value
        except (FileNotFoundError, OSError, ValueError, UnicodeDecodeError):
            self._quarantine(path)
            return None

    def _quarantine(self, path: Path) -> None:
        try:
            path.replace(path.with_suffix(f".rejected-{uuid.uuid4().hex}"))
        except (FileNotFoundError, OSError):
            pass

    def _fsync_root(self) -> None:
        descriptor = os.open(self.root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
