from __future__ import annotations

import json
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


def _write_private(path: Path, content: bytes) -> None:
    path.write_bytes(content)
    path.chmod(0o600)


def _probe_name(index: int) -> str:
    return f".write-test-{1000 + index}-{index:032x}"


def _temp_name(journal: UploadJournal, index: int) -> tuple[str, bytes]:
    entry = _entry(index)
    encoded = json.dumps(entry, sort_keys=True, separators=(",", ":")).encode()
    key = journal._key(str(entry["pendingUploadId"]))
    return f".{key}.json.{index:032x}.tmp", encoded


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


def test_hostile_thousands_of_lookalike_files_fail_closed_quickly(
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
        _write_private(root / f"hostile-{index}.json", b"x" * 1024)
    journal = UploadJournal(root)

    started_at = time.monotonic()
    with pytest.raises(JournalCapacityError, match="unknown entry"):
        journal.entries()
    elapsed = time.monotonic() - started_at

    assert elapsed < 1
    assert len(list(root.glob("*.json"))) == 2_000


def test_scan_counts_every_entry_and_fails_closed_on_unknown(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES", "2")
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    live_probe = root / _probe_name(1)
    _write_private(live_probe, b"ready")
    journal = UploadJournal(root)
    journal.save(_entry(1))

    with pytest.raises(JournalCapacityError, match="journal is full"):
        journal.ensure_capacity(required_entries=1, required_bytes=1)

    live_probe.unlink()
    _write_private(root / "unknown.bin", b"unknown")
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
    journal = UploadJournal(root)
    stale_time = time.time() - 60
    for index in range(10):
        if index % 2:
            name, content = _temp_name(journal, index)
        else:
            name, content = _probe_name(index), b"ready"
        path = root / name
        _write_private(path, content)
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
    helper = UploadJournal(root)
    for index in range(1000):
        entry = _entry(index)
        path = root / f"{helper._key(str(entry['pendingUploadId']))}.json"
        _write_private(path, b"x" * 30)
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


def test_transient_identity_mode_link_and_content_are_strict(
    tmp_path: Path,
) -> None:
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    journal = UploadJournal(root)

    wrong_mode = root / _probe_name(1)
    wrong_mode.write_bytes(b"ready")
    wrong_mode.chmod(0o644)
    with pytest.raises(JournalCapacityError, match="unauthenticated entry"):
        journal.entries()
    wrong_mode.unlink()

    wrong_content = root / _probe_name(2)
    _write_private(wrong_content, b"not-ready")
    with pytest.raises(JournalCapacityError, match="content authentication"):
        journal.entries()
    wrong_content.unlink()

    linked = root / _probe_name(3)
    _write_private(linked, b"ready")
    os.link(linked, tmp_path / "second-link")
    with pytest.raises(JournalCapacityError, match="unauthenticated entry"):
        journal.entries()


def test_wrong_owner_and_live_owned_transient_fail_safe(
    tmp_path: Path,
) -> None:
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    live = root / _probe_name(1)
    _write_private(live, b"ready")
    journal = UploadJournal(root)
    journal.expected_uid += 1
    with pytest.raises(JournalCapacityError, match="unauthenticated entry"):
        journal.entries()

    journal.expected_uid = os.geteuid()
    assert journal.entries() == []
    assert live.exists()

    temp_name, temp_content = _temp_name(journal, 2)
    live_temp = root / temp_name
    _write_private(live_temp, temp_content)
    assert journal.entries() == []
    assert live_temp.exists()


def test_journal_filename_is_bound_to_pending_upload_id(tmp_path: Path) -> None:
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    journal = UploadJournal(root)
    encoded = json.dumps(_entry(1), separators=(",", ":")).encode()
    wrong_path = root / f"{'0' * 64}.json"
    _write_private(wrong_path, encoded)

    with pytest.raises(JournalCapacityError, match="key binding"):
        journal.entries()


def test_temp_key_binding_and_probe_grammar_fail_closed(tmp_path: Path) -> None:
    root = tmp_path / "journal"
    root.mkdir(mode=0o700)
    journal = UploadJournal(root)
    encoded = json.dumps(_entry(1), separators=(",", ":")).encode()
    wrong_temp = root / f".{'0' * 64}.json.{'1' * 32}.tmp"
    _write_private(wrong_temp, encoded)
    with pytest.raises(JournalCapacityError, match="temp key binding"):
        journal.entries()
    wrong_temp.unlink()

    lookalike = root / ".write-test-123-not-a-uuid"
    _write_private(lookalike, b"ready")
    with pytest.raises(JournalCapacityError, match="unknown entry"):
        journal.entries()


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
