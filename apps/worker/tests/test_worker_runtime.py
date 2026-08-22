from __future__ import annotations

import os
import subprocess
import threading
import time
from pathlib import Path

import pytest
from PIL import Image

from zenpdf_worker import tools
from zenpdf_worker.worker import JobOwnershipLost, ToolRunResult, ZenPdfWorker


def _hung_tool(_job: dict, _inputs: list[Path], temp: Path) -> ToolRunResult:
    child = subprocess.Popen(["sleep", "60"])
    (temp / "descendant.pid").write_text(str(child.pid), encoding="ascii")
    time.sleep(60)
    return ToolRunResult([])


class _LeaseWorker(ZenPdfWorker):
    def __init__(self, reports: list[object]) -> None:
        super().__init__("https://example.invalid", "worker-a", "token")
        self.reports = reports
        self.tool_ran = False
        self.upload_ran = False
        self.mutations: list[str] = []

    def _report(self, _job_id: str, _progress: int) -> bool:
        result = self.reports.pop(0)
        if isinstance(result, Exception):
            raise result
        return bool(result)

    def _download_inputs(self, _inputs: list[dict], _temp: Path) -> list[Path]:
        return []

    def _run_tool_bounded(self, *args, **kwargs) -> ToolRunResult:  # type: ignore[no-untyped-def]
        self.tool_ran = True
        return ToolRunResult([])

    def _upload_outputs(self, *args, **kwargs) -> list[dict]:  # type: ignore[no-untyped-def]
        self.upload_ran = True
        return []

    def _mutation(self, path: str, _args: dict) -> object:
        self.mutations.append(path)
        return {"status": "succeeded", "claimedBy": self.worker_id}


class _UploadWorker(ZenPdfWorker):
    def __init__(self) -> None:
        super().__init__("https://example.invalid", "worker-a", "token")
        self.mutations: list[tuple[str, dict]] = []
        self.register_result = True

    def _mutation(self, path: str, args: dict) -> object:
        self.mutations.append((path, args))
        if path == "files:beginWorkerUpload":
            return {
                "pendingUploadId": "pending-1",
                "uploadUrl": "https://upload.invalid/pending-1",
            }
        if path == "files:registerWorkerUpload":
            return self.register_result
        return True


class _RecoveryUploadWorker(_UploadWorker):
    def __init__(self) -> None:
        super().__init__()
        self.registration_failures = 0
        self.discard_failures = 0
        self.upload_state = "registered"

    def _mutation(self, path: str, args: dict) -> object:
        self.mutations.append((path, args))
        if path == "files:beginWorkerUpload":
            return {
                "pendingUploadId": "pending-1",
                "uploadUrl": "https://upload.invalid/pending-1",
                "uploadDeadlineAt": time.time() * 1000 + 120_000,
            }
        if path == "files:registerWorkerUpload":
            if self.registration_failures:
                self.registration_failures -= 1
                raise RuntimeError("registration transport unavailable")
            return True
        if path == "files:discardWorkerUpload":
            if self.discard_failures:
                self.discard_failures -= 1
                raise RuntimeError("cleanup transport unavailable")
            return True
        return True

    def _query(self, path: str, _args: dict) -> object:
        assert path == "files:getWorkerUploadState"
        return self.upload_state


class _CompletionRejectWorker(ZenPdfWorker):
    def __init__(self) -> None:
        super().__init__("https://example.invalid", "worker-a", "token")
        self.discarded: list[str] = []

    def _report(self, _job_id: str, _progress: int) -> bool:
        return True

    def _download_inputs(self, _inputs: list[dict], _temp: Path) -> list[Path]:
        return []

    def _run_tool_bounded(
        self, _job: dict, _inputs: list[Path], temp: Path, *_args, **_kwargs
    ) -> ToolRunResult:
        output = temp / "output.pdf"
        output.write_bytes(b"%PDF-output")
        return ToolRunResult([output])

    def _upload_outputs(self, *_args, **_kwargs) -> list[dict]:
        return [
            {
                "storageId": "stored-1",
                "pendingUploadId": "pending-1",
                "filename": "output.pdf",
                "sizeBytes": 11,
            }
        ]

    def _mutation(self, path: str, _args: dict) -> object:
        if path == "jobs:completeJob":
            return {"status": "running", "claimedBy": "worker-b"}
        if path == "files:discardWorkerUpload":
            self.discarded.append("pending-1")
            return True
        raise AssertionError(path)


def test_heartbeat_retries_transient_failure_boundedly(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("ZENPDF_HEARTBEAT_RETRIES", "3")
    monkeypatch.setenv("ZENPDF_HEARTBEAT_RETRY_SECONDS", "0")
    worker = _LeaseWorker([RuntimeError("one"), RuntimeError("two"), True])
    assert worker._report_with_retry("job", 50)
    assert worker.reports == []


def test_lease_loss_suppresses_tool_upload_and_completion(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("ZENPDF_WORKER_HEARTBEAT_SECONDS", "60")
    worker = _LeaseWorker([True, False])
    worker._process_job({"_id": "job", "inputs": [], "tool": "merge"})
    assert not worker.tool_ran
    assert not worker.upload_ran
    assert "jobs:completeJob" not in worker.mutations
    assert "jobs:failJob" not in worker.mutations


def test_mutation_result_detects_reclaimed_job() -> None:
    worker = ZenPdfWorker("https://example.invalid", "worker-a", "token")
    assert worker._is_owned_job({"status": "running", "claimedBy": "worker-a"})
    assert not worker._is_owned_job({"status": "running", "claimedBy": "worker-b"})
    assert not worker._is_owned_job(None)


def test_close_ignoring_blocked_upload_process_is_killed_on_lease_loss(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    started = threading.Event()
    class FakeSession:
        def post(self, *_args, **_kwargs) -> None:
            started.set()
            while True:
                time.sleep(1)

        @staticmethod
        def close() -> None:
            return None

    monkeypatch.setattr("zenpdf_worker.worker.requests.Session", FakeSession)
    output = tmp_path / "output.pdf"
    output.write_bytes(b"%PDF-output")
    ownership_lost = threading.Event()
    worker = _UploadWorker()

    def lose_during_post() -> None:
        assert started.wait(5)
        ownership_lost.set()

    lease_thread = threading.Thread(target=lose_during_post)
    lease_thread.start()
    started_at = time.monotonic()
    with pytest.raises(JobOwnershipLost, match="during output upload"):
        worker._upload_outputs("job-1", [output], ownership_lost)
    lease_thread.join(timeout=5)

    assert time.monotonic() - started_at < 3
    assert [path for path, _args in worker.mutations] == ["files:beginWorkerUpload"]


def test_registration_and_cleanup_transport_failures_recover_from_journal(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    class FakeResponse:
        @staticmethod
        def raise_for_status() -> None:
            return None

        @staticmethod
        def json() -> dict:
            return {"storageId": "stored-recovery"}

    class FakeSession:
        @staticmethod
        def post(*_args, **_kwargs) -> FakeResponse:
            return FakeResponse()

        @staticmethod
        def close() -> None:
            return None

    monkeypatch.setattr("zenpdf_worker.worker.requests.Session", FakeSession)
    output = tmp_path / "output.pdf"
    output.write_bytes(b"%PDF-output")
    worker = _RecoveryUploadWorker()
    worker.registration_failures = 1

    with pytest.raises(RuntimeError, match="registration transport unavailable"):
        worker._upload_outputs("job-1", [output], threading.Event())
    entries = worker.upload_journal.entries()
    assert len(entries) == 1
    assert entries[0]["action"] == "register"
    assert entries[0]["storageId"] == "stored-recovery"

    recovered_worker = _RecoveryUploadWorker()
    recovered_worker._recover_pending_uploads()
    assert recovered_worker.upload_journal.entries()[0]["action"] == "uploaded"

    recovered_worker.discard_failures = 1
    recovered_worker._recover_pending_uploads()
    assert recovered_worker.upload_journal.entries()[0]["action"] == "discard"
    recovered_worker._recover_pending_uploads()
    assert recovered_worker.upload_journal.entries() == []


def test_completion_rejection_discards_registered_uploads(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("ZENPDF_WORKER_HEARTBEAT_SECONDS", "60")
    worker = _CompletionRejectWorker()
    worker._process_job({"_id": "job-1", "inputs": [], "tool": "merge"})
    assert worker.discarded == ["pending-1"]


def test_registration_rejection_deletes_returned_storage_id(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    class FakeResponse:
        @staticmethod
        def raise_for_status() -> None:
            return None

        @staticmethod
        def json() -> dict:
            return {"storageId": "stored-rejected"}

    class FakeSession:
        @staticmethod
        def post(*_args, **_kwargs) -> FakeResponse:
            return FakeResponse()

        @staticmethod
        def close() -> None:
            return None

    monkeypatch.setattr("zenpdf_worker.worker.requests.Session", FakeSession)
    output = tmp_path / "output.pdf"
    output.write_bytes(b"%PDF-output")
    worker = _UploadWorker()
    worker.register_result = False

    with pytest.raises(RuntimeError, match="could not be registered"):
        worker._upload_outputs("job-1", [output], threading.Event())
    discard_args = [
        args for path, args in worker.mutations if path == "files:discardWorkerUpload"
    ]
    assert discard_args == [
        {
            "pendingUploadId": "pending-1",
            "workerId": "worker-a",
            "storageId": "stored-rejected",
            "workerToken": "token",
        }
    ]


def test_hung_tool_hits_wall_limit_and_kills_descendant(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # Allow the spawn-based child to import the worker's native PDF bindings on
    # slower release images before measuring that the deliberately hung runner
    # is still stopped by the configured hard deadline.
    monkeypatch.setenv("ZENPDF_JOB_WALL_SECONDS", "3")
    worker = ZenPdfWorker("https://example.invalid", "worker-a", "token")
    started = time.monotonic()
    with pytest.raises(RuntimeError, match="hard wall-time"):
        worker._run_tool_bounded({}, [], tmp_path, threading.Event(), runner=_hung_tool)
    assert time.monotonic() - started < 7
    descendant_pid = int((tmp_path / "descendant.pid").read_text(encoding="ascii"))
    time.sleep(0.1)
    status_path = Path(f"/proc/{descendant_pid}/stat")
    if status_path.exists():
        assert status_path.read_text(encoding="ascii").split()[2] == "Z"


def test_lease_loss_cancels_running_tool_group(tmp_path: Path) -> None:
    worker = ZenPdfWorker("https://example.invalid", "worker-a", "token")
    ownership_lost = threading.Event()
    descendant_file = tmp_path / "descendant.pid"

    def lose_after_start() -> None:
        deadline = time.monotonic() + 5
        while not descendant_file.exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        ownership_lost.set()

    lease_thread = threading.Thread(target=lose_after_start)
    lease_thread.start()
    try:
        with pytest.raises(JobOwnershipLost):
            worker._run_tool_bounded(
                {}, [], tmp_path, ownership_lost, runner=_hung_tool
            )
    finally:
        lease_thread.join(timeout=6)
    descendant_pid = int(descendant_file.read_text(encoding="ascii"))
    time.sleep(0.1)
    status_path = Path(f"/proc/{descendant_pid}/stat")
    if status_path.exists():
        assert status_path.read_text(encoding="ascii").split()[2] == "Z"


def test_ocr_wrapper_refuses_unbounded_legacy_fallback(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class LegacyTesseract:
        @staticmethod
        def image_to_string(*_args, **_kwargs):  # type: ignore[no-untyped-def]
            raise TypeError("timeout unsupported")

    monkeypatch.setattr(tools, "pytesseract", LegacyTesseract())
    monkeypatch.setattr(tools.shutil, "which", lambda _name: "/usr/bin/tesseract")
    with pytest.raises(RuntimeError, match="bounded OCR execution"):
        tools._ocr_image(Image.new("RGB", (1, 1)), "eng")
