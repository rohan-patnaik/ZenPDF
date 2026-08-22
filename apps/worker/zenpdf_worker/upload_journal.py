"""Durable recovery journal for worker output uploads."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List


JOURNAL_NAME_RE = re.compile(r"^(?P<key>[0-9a-f]{64})\.json$")
PROBE_NAME_RE = re.compile(r"^\.write-test-(?P<pid>[1-9][0-9]*)-(?P<nonce>[0-9a-f]{32})$")
TEMP_NAME_RE = re.compile(
    r"^\.(?P<key>[0-9a-f]{64})\.json\.(?P<nonce>[0-9a-f]{32})\.tmp$"
)


class JournalCapacityError(RuntimeError):
    """Raised before upload when durable recovery capacity is exhausted."""


@dataclass
class _DirectoryScan:
    json_paths: List[Path]
    entry_count: int
    total_bytes: int
    complete: bool


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
        self.expected_uid = os.geteuid()
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
        self.scan_max_entries = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_ENTRIES", 2048, 20_000
        )
        self.scan_max_bytes = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_BYTES", 16 * 1024 * 1024, 128 * 1024 * 1024
        )
        self.scan_max_ms = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_MS", 100, 5000
        )
        self.transient_cleanup_batch = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_TRANSIENT_CLEANUP_BATCH", 32, 256
        )
        self.transient_stale_seconds = _bounded_env_int(
            "ZENPDF_UPLOAD_JOURNAL_TRANSIENT_STALE_SECONDS", 300, 86_400
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
        scan = self._scan_directory(clean_owned=True)
        if not scan.complete:
            raise JournalCapacityError("Upload recovery directory scan budget exhausted")
        count = scan.entry_count
        total_bytes = scan.total_bytes
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
            metadata = target.lstat()
            self._validate_identity(metadata)
            self._unlink_authenticated(target, metadata)
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
        scan = self._scan_directory(clean_owned=True)
        recovered: List[Dict[str, Any]] = []
        process_limit = max(read_limit * 4, self.transient_cleanup_batch)
        for path in scan.json_paths[:process_limit]:
            value = self._read_entry(path)
            if value is not None:
                next_attempt_at = value.get("nextAttemptAt")
                if not (
                    ready_at is not None
                    and isinstance(next_attempt_at, (int, float))
                    and next_attempt_at > ready_at
                ):
                    recovered.append(value)
            if len(recovered) >= read_limit:
                break
        return recovered

    def has_entries(self) -> bool:
        """Check for pending work without loading journal content."""
        if not self.root.exists():
            return False
        scan = self._scan_directory(clean_owned=True)
        return bool(scan.json_paths) or not scan.complete

    def _validate_identity(self, metadata: os.stat_result) -> None:
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_uid != self.expected_uid
            or stat.S_IMODE(metadata.st_mode) != 0o600
            or metadata.st_nlink != 1
        ):
            raise JournalCapacityError(
                "Upload recovery directory contains unauthenticated entry"
            )

    def _unlink_authenticated(self, path: Path, expected: os.stat_result) -> None:
        current = path.lstat()
        self._validate_identity(current)
        if current.st_dev != expected.st_dev or current.st_ino != expected.st_ino:
            raise JournalCapacityError("Upload recovery entry changed during cleanup")
        path.unlink()

    def _read_owned_bytes(self, path: Path, maximum: int) -> bytes:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        try:
            opened = os.fstat(descriptor)
            self._validate_identity(opened)
            with os.fdopen(descriptor, "rb", closefd=False) as handle:
                return handle.read(maximum + 1)
        finally:
            os.close(descriptor)

    def _authenticate_transient(self, path: Path, name: str) -> None:
        probe = PROBE_NAME_RE.fullmatch(name)
        if probe:
            if self._read_owned_bytes(path, 5) != b"ready":
                raise JournalCapacityError(
                    "Upload recovery probe content authentication failed"
                )
            return
        temporary = TEMP_NAME_RE.fullmatch(name)
        if temporary:
            encoded = self._read_owned_bytes(path, self.max_entry_bytes)
            if len(encoded) > self.max_entry_bytes:
                raise JournalCapacityError(
                    "Upload recovery temp content authentication failed"
                )
            try:
                value = json.loads(encoded)
            except (ValueError, UnicodeDecodeError):
                raise JournalCapacityError(
                    "Upload recovery temp content authentication failed"
                ) from None
            pending_upload_id = (
                value.get("pendingUploadId") if isinstance(value, dict) else None
            )
            if (
                not isinstance(pending_upload_id, str)
                or not pending_upload_id
                or value.get("action") not in {"register", "uploaded", "discard"}
                or self._key(pending_upload_id) != temporary.group("key")
            ):
                raise JournalCapacityError(
                    "Upload recovery temp key binding failed"
                )
            return
        raise JournalCapacityError(
            "Upload recovery directory contains unknown entry"
        )

    def _scan_directory(self, clean_owned: bool) -> _DirectoryScan:
        """Inspect every encountered entry under hard entry, byte, and time budgets."""
        started = time.monotonic()
        json_paths: List[Path] = []
        entry_count = 0
        total_bytes = 0
        complete = True
        cleaned = 0
        removed_any = False
        now = time.time()
        with os.scandir(self.root) as entries:
            for item in entries:
                if entry_count >= self.scan_max_entries:
                    complete = False
                    break
                if (time.monotonic() - started) * 1000 >= self.scan_max_ms:
                    complete = False
                    break
                entry_count += 1
                path = Path(item.path)
                try:
                    metadata = item.stat(follow_symlinks=False)
                except OSError:
                    raise JournalCapacityError(
                        "Upload recovery directory metadata unreadable"
                    ) from None
                total_bytes += max(metadata.st_size, 0)
                if total_bytes > self.scan_max_bytes:
                    complete = False
                    break
                if not stat.S_ISREG(metadata.st_mode):
                    raise JournalCapacityError(
                        "Upload recovery directory contains unsafe entry type"
                    )
                self._validate_identity(metadata)
                journal_match = JOURNAL_NAME_RE.fullmatch(item.name)
                if journal_match:
                    json_paths.append(path)
                    continue
                if PROBE_NAME_RE.fullmatch(item.name) or TEMP_NAME_RE.fullmatch(item.name):
                    self._authenticate_transient(path, item.name)
                    stale = now - metadata.st_mtime >= self.transient_stale_seconds
                    if clean_owned and stale and cleaned < self.transient_cleanup_batch:
                        try:
                            self._unlink_authenticated(path, metadata)
                            cleaned += 1
                            removed_any = True
                        except OSError:
                            raise JournalCapacityError(
                                "Upload recovery transient cleanup failed"
                            ) from None
                    continue
                raise JournalCapacityError(
                    "Upload recovery directory contains unknown entry"
                )
        if removed_any:
            self._fsync_root()
        return _DirectoryScan(
            json_paths,
            max(entry_count - cleaned, 0),
            total_bytes,
            complete,
        )

    def _read_entry(self, path: Path) -> Dict[str, Any] | None:
        try:
            metadata = path.lstat()
            self._validate_identity(metadata)
            match = JOURNAL_NAME_RE.fullmatch(path.name)
            if not match or metadata.st_size > self.max_entry_bytes:
                raise JournalCapacityError("Upload recovery journal entry is invalid")
            encoded = self._read_owned_bytes(path, self.max_entry_bytes)
            if len(encoded) > self.max_entry_bytes:
                raise JournalCapacityError("Upload recovery journal entry is oversized")
            value = json.loads(encoded)
            if (
                not isinstance(value, dict)
                or not isinstance(value.get("pendingUploadId"), str)
                or value.get("action") not in {"register", "uploaded", "discard"}
            ):
                raise JournalCapacityError("Upload recovery journal content is invalid")
            if self._key(value["pendingUploadId"]) != match.group("key"):
                raise JournalCapacityError("Upload recovery journal key binding failed")
            return value
        except FileNotFoundError:
            return None
        except JournalCapacityError:
            raise
        except (OSError, ValueError, UnicodeDecodeError):
            raise JournalCapacityError(
                "Upload recovery journal entry is unreadable"
            ) from None

    def _fsync_root(self) -> None:
        descriptor = os.open(self.root, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
