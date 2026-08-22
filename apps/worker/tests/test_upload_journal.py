from __future__ import annotations

import os
import time
from pathlib import Path

import pytest

from zenpdf_worker.upload_journal import JournalCapacityError, UploadJournal


def _entry(index: int) -> dict[str, object]:
    return {
        "jobId": f"job-{index}",
        "pendingUploadId": f"pending-{index}",
        "storageId": f"stored-{index}",
        "action": "uploaded",
    }


def test_journal_enforces_private_permissions(tmp_path: Path) -> None:
    root = tmp_path / "journal"
    journal = UploadJournal(root)
    journal.save(_entry(1))

    path = journal._path("pending-1")
    assert os.stat(root).st_mode & 0o777 == 0o700
    assert os.stat(path).st_mode & 0o777 == 0o600


def test_journal_bounds_count_entry_size_and_batches(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES", "5")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRY_BYTES", "256")
    monkeypatch.setenv("ZENPDF_UPLOAD_RECOVERY_BATCH_SIZE", "2")
    journal = UploadJournal(tmp_path / "journal")
    for index in range(5):
        journal.save(_entry(index))

    assert len(journal.entries()) == 2
    with pytest.raises(JournalCapacityError, match="journal is full"):
        journal.save(_entry(6))
    oversized = {**_entry(0), "padding": "x" * 512}
    with pytest.raises(JournalCapacityError, match="entry exceeds size"):
        journal.save(oversized)


def test_hostile_thousands_of_oversized_files_have_bounded_scan(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES", "16")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRY_BYTES", "128")
    monkeypatch.setenv("ZENPDF_UPLOAD_RECOVERY_BATCH_SIZE", "4")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_ENTRIES", "17")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_TRANSIENT_CLEANUP_BATCH", "17")
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    for index in range(2_000):
        (root / f"hostile-{index}.json").write_bytes(b"x" * 1024)
    journal = UploadJournal(root)

    started_at = time.monotonic()
    assert journal.entries() == []
    elapsed = time.monotonic() - started_at

    assert elapsed < 1
    assert len(list(root.glob("*.json"))) == 2_000 - 17


def test_scan_counts_every_entry_and_fails_closed_on_unknown(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES", "2")
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    (root / ".write-test-live").write_bytes(b"probe")
    journal = UploadJournal(root)
    journal.save(_entry(1))

    with pytest.raises(JournalCapacityError, match="journal is full"):
        journal.ensure_capacity(required_entries=1, required_bytes=1)

    (root / ".write-test-live").unlink()
    (root / "unknown.bin").write_bytes(b"unknown")
    with pytest.raises(JournalCapacityError, match="unknown entry"):
        journal.ensure_capacity(required_entries=0, required_bytes=0)


def test_scan_fails_closed_on_symlink_and_fifo(
    tmp_path: Path,
) -> None:
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    target = tmp_path / "target"
    target.write_bytes(b"target")
    (root / "unsafe.json").symlink_to(target)
    journal = UploadJournal(root)
    with pytest.raises(JournalCapacityError, match="unsafe entry type"):
        journal.entries()

    (root / "unsafe.json").unlink()
    os.mkfifo(root / "pipe.json")
    with pytest.raises(JournalCapacityError, match="unsafe entry type"):
        journal.entries()


def test_owned_stale_transient_cleanup_is_bounded_and_restart_safe(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_TRANSIENT_CLEANUP_BATCH", "3")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_TRANSIENT_STALE_SECONDS", "1")
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    stale_time = time.time() - 60
    for index in range(10):
        suffix = ".tmp" if index % 2 else ""
        name = f".owned-{index}{suffix}" if suffix else f".write-test-{index}"
        path = root / name
        path.write_bytes(b"stale")
        os.utime(path, (stale_time, stale_time))

    first = UploadJournal(root)
    assert first.entries() == []
    assert len(list(root.iterdir())) == 7

    restarted = UploadJournal(root)
    assert restarted.entries() == []
    assert len(list(root.iterdir())) == 4


def test_scan_entry_and_byte_budgets_are_hard(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_ENTRIES", "5")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_BYTES", "100")
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    for index in range(1000):
        (root / f"{index}.json").write_bytes(b"x" * 30)
    journal = UploadJournal(root)

    scan = journal._scan_directory(clean_owned=False)
    assert scan.entry_count <= 5
    assert scan.total_bytes <= 120
    assert not scan.complete
    with pytest.raises(JournalCapacityError, match="scan budget exhausted"):
        journal.ensure_capacity(required_entries=1, required_bytes=1)


def test_scan_time_budget_is_hard(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_MS", "1")
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    (root / "one.json").write_bytes(b"{}")
    ticks = iter([0.0, 1.0])
    monkeypatch.setattr(
        "zenpdf_worker.upload_journal.time.monotonic", lambda: next(ticks)
    )
    journal = UploadJournal(root)

    scan = journal._scan_directory(clean_owned=False)

    assert scan.entry_count == 0
    assert not scan.complete


def test_journal_total_byte_limit_blocks_new_entries(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES", "100")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_BYTES", "300")
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRY_BYTES", "256")
    journal = UploadJournal(tmp_path / "journal")
    journal.save(_entry(1))
    journal.save(_entry(2))
    journal.save(_entry(3))

    with pytest.raises(JournalCapacityError, match="byte limit"):
        journal.save(_entry(4))
