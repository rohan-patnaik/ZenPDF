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
