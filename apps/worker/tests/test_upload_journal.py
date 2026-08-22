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
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    for index in range(2_000):
        (root / f"hostile-{index}.json").write_bytes(b"x" * 1024)
    journal = UploadJournal(root)

    started_at = time.monotonic()
    assert journal.entries() == []
    elapsed = time.monotonic() - started_at

    assert elapsed < 1
    assert len(list(root.glob("*.rejected-*"))) <= 17


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
