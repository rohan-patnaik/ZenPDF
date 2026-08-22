"""Pytest configuration for worker tests."""

import sys
from pathlib import Path

import pytest

sys.path.append(str(Path(__file__).resolve().parents[1]))


@pytest.fixture(autouse=True)
def durable_upload_journal(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Give every worker test an isolated writable durable journal."""
    monkeypatch.setenv("ZENPDF_UPLOAD_JOURNAL_DIR", str(tmp_path / "upload-journal"))
    monkeypatch.setenv("ZENPDF_UPLOAD_PROCESS_START_METHOD", "fork")
