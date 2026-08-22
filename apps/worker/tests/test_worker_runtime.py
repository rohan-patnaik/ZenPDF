from __future__ import annotations

import os
import subprocess
import threading
import time
import traceback
from pathlib import Path

import pytest
from PIL import Image

from zenpdf_worker import tools
from zenpdf_worker.worker import (
    JobOwnershipLost,
    ToolRunResult,
    WorkerShutdown,
    ZenPdfWorker,
    _run_supervised,
    _stable_exception_code,
    _tool_process_entry,
    main,
)


def _hung_tool(_job: dict, _inputs: list[Path], temp: Path) -> ToolRunResult:
    child = subprocess.Popen(["sleep", "60"])
    (temp / "descendant.pid").write_text(str(child.pid), encoding="ascii")
    time.sleep(60)
    return ToolRunResult([])


class _FakeSender:
    @staticmethod
    def close() -> None:
        return None


class _FakeReceiver:
    def __init__(self, message: tuple[object, ...] | None = None) -> None:
        self.message = message

    def poll(self, timeout: float) -> bool:
        if self.message is None:
            time.sleep(min(max(timeout, 0.0), 0.001))
            return False
        return True

    def recv(self) -> tuple[object, ...]:
        assert self.message is not None
        message = self.message
        self.message = None
        return message

    @staticmethod
    def close() -> None:
        return None


class _StubbornChild:
    pid = 424246

    def __init__(self, on_start) -> None:  # type: ignore[no-untyped-def]
        self.on_start = on_start
        self.joins: list[int | float | None] = []
        self.terminated = False
        self.killed = False
        self.closed = False
        self.exitcode = None

    def start(self) -> None:
        self.on_start()

    @staticmethod
    def is_alive() -> bool:
        return True

    def join(self, timeout: int | float | None = None) -> None:
        self.joins.append(timeout)

    def terminate(self) -> None:
        self.terminated = True

    def kill(self) -> None:
        self.killed = True

    def close(self) -> None:
        self.closed = True


class _FakeChildContext:
    def __init__(
        self,
        child: _StubbornChild,
        message: tuple[object, ...] | None = None,
    ) -> None:
        self.child = child
        self.receiver = _FakeReceiver(message)

    def Pipe(self, duplex: bool = False) -> tuple[_FakeReceiver, _FakeSender]:
        assert not duplex
        return self.receiver, _FakeSender()

    def Process(self, *_args: object, **_kwargs: object) -> _StubbornChild:
        return self.child


class _IntegratedRestartWorker(ZenPdfWorker):
    def __init__(self, mode: str, trigger: str) -> None:
        super().__init__("https://example.invalid", "worker-a", "token")
        self.mode = mode
        self.trigger = trigger
        self.mutations: list[str] = []
        self.claims = 0
        self.child_started = threading.Event()
        self.journal_entry_scans = 0

    def _mutation(self, path: str, _args: dict) -> object:
        self.mutations.append(path)
        if path == "jobs:claimNextJob":
            self.claims += 1
            if self.claims == 1:
                return {"_id": "job-1", "inputs": [], "tool": "merge"}
            self.request_shutdown()
            return None
        if path == "files:beginWorkerUpload":
            return {
                "pendingUploadId": "pending-1",
                "uploadUrl": "https://upload.invalid/pending-1",
            }
        if path == "jobs:completeJob":
            return {"status": "succeeded", "claimedBy": self.worker_id}
        if path == "jobs:failJob":
            return {"status": "failed", "claimedBy": self.worker_id}
        if path == "files:registerWorkerUpload":
            return True
        raise AssertionError(path)

    def _report(self, _job_id: str, _progress: int) -> bool:
        return not (
            self.trigger == "lease" and self.child_started.is_set()
        )

    def _download_inputs(self, _inputs: list[dict], _temp: Path) -> list[Path]:
        return []

    def _run_tool_bounded(
        self,
        job: dict,
        inputs: list[Path],
        temp: Path,
        ownership_lost: threading.Event,
        runner=None,  # type: ignore[no-untyped-def]
    ) -> ToolRunResult:
        if self.mode == "upload":
            output = temp / "output.pdf"
            output.write_bytes(b"%PDF-output")
            return ToolRunResult([output])
        return super()._run_tool_bounded(
            job, inputs, temp, ownership_lost, runner=runner
        )


def _run_integrated_stubborn_case(
    monkeypatch: pytest.MonkeyPatch,
    mode: str,
    trigger: str,
    message: tuple[object, ...] | None = None,
    create_unreadable_journal: bool = False,
    break_stdout: bool = False,
    seed_existing_journal: bool = False,
) -> tuple[_IntegratedRestartWorker, _StubbornChild, list[int], float]:
    worker = _IntegratedRestartWorker(mode, trigger)
    original_entries = worker.upload_journal.entries

    def counted_entries(*args, **kwargs):  # type: ignore[no-untyped-def]
        worker.journal_entry_scans += 1
        return original_entries(*args, **kwargs)

    monkeypatch.setattr(worker.upload_journal, "entries", counted_entries)

    def child_started() -> None:
        worker.child_started.set()
        if seed_existing_journal:
            worker.upload_journal.save(
                {
                    "jobId": "existing-job",
                    "pendingUploadId": "existing-pending",
                    "storageId": "existing-storage",
                    "action": "discard",
                }
            )
        if create_unreadable_journal:
            path = worker.upload_journal.root / f"{'0' * 64}.json"
            path.write_text("UPLOAD_SECRET_MARKER", encoding="utf-8")
            path.chmod(0o600)
        if trigger == "shutdown":
            worker.request_shutdown()

    child = _StubbornChild(child_started)
    context = _FakeChildContext(child, message)
    monkeypatch.setattr(
        "zenpdf_worker.worker.multiprocessing.get_context", lambda *_args: context
    )
    monkeypatch.setattr(
        "zenpdf_worker.worker.os.killpg",
        lambda *_args: (_ for _ in ()).throw(ProcessLookupError()),
    )
    monkeypatch.setenv("ZENPDF_JOB_WALL_SECONDS", "1")
    monkeypatch.setenv("ZENPDF_TOOL_PROCESS_JOIN_SECONDS", "1")
    monkeypatch.setenv("ZENPDF_UPLOAD_DEADLINE_SECONDS", "1")
    monkeypatch.setenv("ZENPDF_UPLOAD_PROCESS_JOIN_SECONDS", "1")
    monkeypatch.setenv("ZENPDF_WORKER_HEARTBEAT_SECONDS", "0.01")
    monkeypatch.setenv("ZENPDF_POLL_INTERVAL", "0")
    if break_stdout:
        monkeypatch.setattr(
            "builtins.print",
            lambda *_args, **_kwargs: (_ for _ in ()).throw(
                RuntimeError("UPLOAD_SECRET_MARKER")
            ),
        )
    exits: list[int] = []
    started_at = time.monotonic()
    _run_supervised(worker, exits.append)
    elapsed = time.monotonic() - started_at
    return worker, child, exits, elapsed


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


class _HostileFailureWorker(ZenPdfWorker):
    def __init__(self, hostile_message: str) -> None:
        super().__init__("https://example.invalid", "worker-a", "worker-secret")
        self.hostile_message = hostile_message
        self.failure_args: dict | None = None

    def _report(self, _job_id: str, _progress: int) -> bool:
        return True

    def _download_inputs(self, _inputs: list[dict], _temp: Path) -> list[Path]:
        return []

    def _run_tool_bounded(self, *_args, **_kwargs) -> ToolRunResult:  # type: ignore[no-untyped-def]
        raise RuntimeError(self.hostile_message)

    def _mutation(self, path: str, args: dict) -> object:
        if path == "jobs:failJob":
            self.failure_args = args
            return {"status": "failed", "claimedBy": self.worker_id}
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


def test_upload_child_error_is_sanitized_before_ipc_and_logging(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    secrets = {
        "url": "https://upload.invalid/path?token=signed-secret",
        "token": "worker-super-secret",
        "filename": "private-customer-name.pdf",
        "content": "private-pdf-content-marker",
    }

    class LeakySession:
        @staticmethod
        def post(*_args, **_kwargs) -> None:
            raise RuntimeError(" ".join(secrets.values()))

        @staticmethod
        def close() -> None:
            return None

    monkeypatch.setattr("zenpdf_worker.worker.requests.Session", LeakySession)
    output = tmp_path / secrets["filename"]
    output.write_text(secrets["content"], encoding="utf-8")
    worker = _UploadWorker()
    worker.worker_token = secrets["token"]

    with pytest.raises(RuntimeError, match=r"UPLOAD_FAILED") as captured_error:
        worker._upload_one_pending(
            "job-1",
            output,
            secrets["url"],
            "pending-1",
            time.time() * 1000 + 60_000,
            threading.Event(),
        )
    captured = capsys.readouterr()
    observable = f"{captured_error.value}\n{captured.out}\n{captured.err}"
    for secret in secrets.values():
        assert secret not in observable


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("UPLOAD_FAILED", "UPLOAD_FAILED"),
        ("UPLOAD_HTTP_100", "UPLOAD_HTTP_100"),
        ("UPLOAD_HTTP_599", "UPLOAD_HTTP_599"),
        ("UPLOAD_HTTP_099", "FIXED_DEFAULT"),
        ("UPLOAD_HTTP_600", "FIXED_DEFAULT"),
        ("UPLOAD_HTTP_200_EXTRA", "FIXED_DEFAULT"),
        ("UPLOAD_SECRET_MARKER", "FIXED_DEFAULT"),
        ("BACKEND_HTTP_500", "FIXED_DEFAULT"),
    ],
)
def test_stable_error_classifier_has_exact_vocabulary(
    value: str, expected: str
) -> None:
    assert _stable_exception_code(RuntimeError(value), "FIXED_DEFAULT") == expected


def test_hostile_upload_prefix_from_ipc_maps_to_fixed_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    hostile = "UPLOAD_SECRET_MARKER"

    class Receiver:
        @staticmethod
        def poll(_timeout: float) -> bool:
            return True

        @staticmethod
        def recv() -> tuple[str, str]:
            return ("error", hostile)

        @staticmethod
        def close() -> None:
            return None

    class Sender:
        @staticmethod
        def close() -> None:
            return None

    class Process:
        pid = 424245
        closed = False

        @staticmethod
        def start() -> None:
            return None

        @staticmethod
        def is_alive() -> bool:
            return False

        @staticmethod
        def join(timeout: float | None = None) -> None:
            return None

        def close(self) -> None:
            self.closed = True

    process = Process()

    class Context:
        @staticmethod
        def Pipe(duplex: bool = False) -> tuple[Receiver, Sender]:
            assert not duplex
            return Receiver(), Sender()

        @staticmethod
        def Process(*_args: object, **_kwargs: object) -> Process:
            return process

    monkeypatch.setattr(
        "zenpdf_worker.worker.multiprocessing.get_context", lambda *_args: Context()
    )
    output = tmp_path / "output.pdf"
    output.write_bytes(b"%PDF-output")
    worker = _UploadWorker()

    with pytest.raises(RuntimeError) as captured_error:
        worker._upload_one_pending(
            "job-1",
            output,
            "https://upload.invalid",
            "pending-1",
            time.time() * 1000 + 60_000,
            threading.Event(),
        )

    assert str(captured_error.value) == "Output upload failed (UPLOAD_FAILED)"
    assert hostile not in str(captured_error.value)
    assert process.closed


def test_tool_exception_is_allowlisted_before_ipc(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    hostile = (
        "https://signed.invalid/file?token=url-secret worker-secret password-secret "
        "/private/path customer.pdf content-marker"
    )

    class FakeConnection:
        def __init__(self) -> None:
            self.messages: list[tuple[object, ...]] = []

        def send(self, message: tuple[object, ...]) -> None:
            self.messages.append(message)

        def close(self) -> None:
            return None

    def hostile_runner(_job: dict, _inputs: list[Path], _temp: Path) -> ToolRunResult:
        raise RuntimeError(hostile)

    monkeypatch.setattr("zenpdf_worker.worker.os.setsid", lambda: None)
    connection = FakeConnection()
    _tool_process_entry(hostile_runner, {}, [], tmp_path, connection)

    assert connection.messages == [("error", "TOOL_EXECUTION_FAILED")]
    assert hostile not in repr(connection.messages)


def test_backend_and_tool_failures_never_reach_logs_or_failure_payload(
    capsys: pytest.CaptureFixture[str],
) -> None:
    markers = [
        "https://signed.invalid/file?token=url-secret",
        "worker-secret",
        "password-secret",
        "/private/path",
        "customer.pdf",
        "content-marker",
    ]
    worker = _HostileFailureWorker(" ".join(markers))

    worker._process_job({"_id": "job-1", "inputs": [], "tool": "merge"})

    captured = capsys.readouterr()
    observable = f"{captured.out}\n{captured.err}\n{worker.failure_args!r}"
    for marker in markers:
        assert marker not in observable
    assert worker.failure_args is not None
    assert worker.failure_args["errorMessage"] == "Processing failed. Please retry."


def test_cleanup_and_failure_reporting_logs_only_stable_classes(
    capsys: pytest.CaptureFixture[str],
) -> None:
    hostile = "UPLOAD_SECRET_MARKER"

    class HostileCleanupWorker(_RecoveryUploadWorker):
        def _mutation(self, _path: str, _args: dict) -> object:
            raise RuntimeError(hostile)

    worker = HostileCleanupWorker()
    worker.upload_journal.save(
        {
            "jobId": "job-1",
            "pendingUploadId": "pending-1",
            "storageId": "stored-1",
            "action": "register",
        }
    )
    worker._discard_pending_upload("pending-1")
    worker._safe_fail("job-1", "SERVICE_CAPACITY_TEMPORARY", "Safe message")

    captured = capsys.readouterr()
    assert hostile not in captured.out
    assert "UPLOAD_CLEANUP_DEFERRED" in captured.out
    assert "FAILURE_REPORT_FAILED" in captured.out


def test_recovery_rejects_hostile_upload_prefixed_exception_code(
    capsys: pytest.CaptureFixture[str],
) -> None:
    hostile = "UPLOAD_SECRET_MARKER"

    class HostileRecoveryWorker(_RecoveryUploadWorker):
        def _query(self, _path: str, _args: dict) -> object:
            raise RuntimeError(hostile)

    worker = HostileRecoveryWorker()
    entry = {
        "jobId": "job-1",
        "pendingUploadId": "pending-1",
        "storageId": "stored-1",
        "action": "uploaded",
    }
    worker.upload_journal.save(entry)

    assert worker._recover_pending_uploads() == 1

    captured = capsys.readouterr()
    assert hostile not in captured.out
    assert "UPLOAD_RECOVERY_FAILED" in captured.out
    assert worker.upload_journal.load("pending-1") is not None


def test_shutdown_kills_active_upload_process_tree_promptly(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    started = threading.Event()
    descendant_file = tmp_path / "upload-descendant.pid"

    class BlockedSession:
        @staticmethod
        def post(*_args, **_kwargs) -> None:
            child = subprocess.Popen(["sleep", "60"])
            descendant_file.write_text(str(child.pid), encoding="ascii")
            started.set()
            time.sleep(60)

        @staticmethod
        def close() -> None:
            return None

    monkeypatch.setattr("zenpdf_worker.worker.requests.Session", BlockedSession)
    output = tmp_path / "output.pdf"
    output.write_bytes(b"%PDF-output")
    worker = _UploadWorker()

    shutdown = threading.Thread(
        target=lambda: (started.wait(5), worker.request_shutdown())
    )
    shutdown.start()
    started_at = time.monotonic()
    with pytest.raises(WorkerShutdown, match="cancelled output upload"):
        worker._upload_outputs("job-1", [output], threading.Event())
    shutdown.join(timeout=5)
    assert time.monotonic() - started_at < 3
    descendant_pid = int(descendant_file.read_text(encoding="ascii"))
    time.sleep(0.1)
    status_path = Path(f"/proc/{descendant_pid}/stat")
    if status_path.exists():
        assert status_path.read_text(encoding="ascii").split()[2] == "Z"


def test_stubborn_upload_process_forces_bounded_supervisor_return(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class StubbornProcess:
        pid = 424242

        def __init__(self) -> None:
            self.joins: list[int | None] = []
            self.terminated = False
            self.killed = False

        @staticmethod
        def is_alive() -> bool:
            return True

        def join(self, timeout: int | None = None) -> None:
            self.joins.append(timeout)

        def terminate(self) -> None:
            self.terminated = True

        def kill(self) -> None:
            self.killed = True

    monkeypatch.setenv("ZENPDF_UPLOAD_PROCESS_JOIN_SECONDS", "1")
    monkeypatch.setattr(
        "zenpdf_worker.worker.os.killpg",
        lambda *_args: (_ for _ in ()).throw(ProcessLookupError()),
    )
    worker = _UploadWorker()
    worker.upload_journal.save(
        {
            "jobId": "job-1",
            "pendingUploadId": "pending-1",
            "storageId": "stored-1",
            "action": "uploaded",
        }
    )
    process = StubbornProcess()
    started_at = time.monotonic()

    stopped = worker._stop_upload_process(process)  # type: ignore[arg-type]

    assert not stopped
    assert time.monotonic() - started_at < 1
    assert process.terminated
    assert process.killed
    assert process.joins == [1, 1]
    assert worker._supervisor_force_exit_required
    assert worker._stubborn_upload_processes == [process]
    assert worker.upload_journal.load("pending-1") is not None


def test_stubborn_tool_process_sets_shared_forced_exit_without_close(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    class StubbornToolProcess:
        pid = 424243

        def __init__(self) -> None:
            self.joins: list[int | None] = []
            self.terminated = False
            self.killed = False
            self.closed = False

        @staticmethod
        def is_alive() -> bool:
            return True

        def join(self, timeout: int | None = None) -> None:
            self.joins.append(timeout)

        def terminate(self) -> None:
            self.terminated = True

        def kill(self) -> None:
            self.killed = True

        def close(self) -> None:
            self.closed = True

    monkeypatch.setenv("ZENPDF_TOOL_PROCESS_JOIN_SECONDS", "1")
    monkeypatch.setattr(
        "zenpdf_worker.worker.os.killpg",
        lambda *_args: (_ for _ in ()).throw(ProcessLookupError()),
    )
    worker = ZenPdfWorker("https://example.invalid", "worker-a", "token")
    process = StubbornToolProcess()

    stopped = worker._terminate_tool_process(process)  # type: ignore[arg-type]

    assert not stopped
    assert process.terminated
    assert process.killed
    assert process.joins == [1, 1]
    assert not process.closed
    assert worker._stubborn_tool_processes == [process]
    assert worker._supervisor_force_exit_required

    monkeypatch.setattr(worker, "run", lambda: None)
    monkeypatch.setattr(worker, "_drain_upload_recovery", lambda: None)
    exits: list[int] = []
    _run_supervised(worker, exits.append)
    assert exits == [70]
    assert not process.closed


@pytest.mark.parametrize("trigger", ["timeout", "lease", "shutdown"])
def test_real_run_loop_stubborn_tool_unwinds_to_supervisor_restart(
    trigger: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    worker, child, exits, elapsed = _run_integrated_stubborn_case(
        monkeypatch, "tool", trigger
    )

    assert exits == [70]
    assert elapsed < 3
    assert worker.claims == 1
    assert worker.mutations.count("jobs:claimNextJob") == 1
    assert "jobs:completeJob" not in worker.mutations
    assert "jobs:failJob" not in worker.mutations
    assert "files:registerWorkerUpload" not in worker.mutations
    assert child.terminated and child.killed
    assert child.joins == [1, 1]
    assert not child.closed
    assert worker._stubborn_tool_processes == [child]
    assert worker.journal_entry_scans == 2


@pytest.mark.parametrize("trigger", ["timeout", "lease", "shutdown"])
def test_real_run_loop_stubborn_upload_unwinds_to_supervisor_restart(
    trigger: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    worker, child, exits, elapsed = _run_integrated_stubborn_case(
        monkeypatch, "upload", trigger
    )

    assert exits == [70]
    assert elapsed < 3
    assert worker.claims == 1
    assert worker.mutations.count("jobs:claimNextJob") == 1
    assert worker.mutations.count("files:beginWorkerUpload") == 1
    assert "jobs:completeJob" not in worker.mutations
    assert "jobs:failJob" not in worker.mutations
    assert "files:registerWorkerUpload" not in worker.mutations
    assert child.terminated and child.killed
    assert child.joins == [1, 1]
    assert not child.closed
    assert worker._stubborn_upload_processes == [child]
    assert worker.upload_journal.entries() == []
    assert worker.journal_entry_scans == 3


def test_real_run_loop_fsyncs_known_upload_before_supervisor_restart(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker, child, exits, elapsed = _run_integrated_stubborn_case(
        monkeypatch,
        "upload",
        "successful-ipc",
        message=("ok", "stored-known"),
    )

    assert exits == [70]
    assert elapsed < 2
    assert worker.claims == 1
    assert worker.mutations == [
        "jobs:claimNextJob",
        "files:beginWorkerUpload",
    ]
    assert child.terminated and child.killed
    assert child.joins == [1, 1]
    assert not child.closed
    retained = worker.upload_journal.load("pending-1")
    assert retained == {
        "jobId": "job-1",
        "pendingUploadId": "pending-1",
        "storageId": "stored-known",
        "action": "register",
    }
    restarted_journal = type(worker.upload_journal)(worker.upload_journal.root)
    assert restarted_journal.load("pending-1") == retained
    assert worker.journal_entry_scans == 2


def test_real_run_loop_discards_successful_tool_ipc_after_stuck_exit(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker, child, exits, elapsed = _run_integrated_stubborn_case(
        monkeypatch,
        "tool",
        "successful-ipc",
        message=("ok", ToolRunResult([])),
    )

    assert exits == [70]
    assert elapsed < 2
    assert worker.claims == 1
    assert worker.mutations == ["jobs:claimNextJob"]
    assert "jobs:completeJob" not in worker.mutations
    assert child.terminated and child.killed
    assert child.joins == [1, 0, 1, 1]
    assert not child.closed
    assert worker._stubborn_tool_processes == [child]
    assert worker.journal_entry_scans == 2


def test_real_run_loop_retains_existing_journal_when_upload_id_is_unknown(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker, child, exits, elapsed = _run_integrated_stubborn_case(
        monkeypatch,
        "upload",
        "timeout",
        seed_existing_journal=True,
    )

    assert exits == [70]
    assert elapsed < 3
    assert worker.mutations == [
        "jobs:claimNextJob",
        "files:beginWorkerUpload",
    ]
    assert child.terminated and child.killed
    assert not child.closed
    assert worker.upload_journal.load("existing-pending") == {
        "jobId": "existing-job",
        "pendingUploadId": "existing-pending",
        "storageId": "existing-storage",
        "action": "discard",
    }


def test_real_run_loop_exit70_survives_unreadable_journal_and_broken_stdout(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker, child, exits, elapsed = _run_integrated_stubborn_case(
        monkeypatch,
        "tool",
        "timeout",
        create_unreadable_journal=True,
        break_stdout=True,
    )

    assert exits == [70]
    assert elapsed < 3
    assert worker.claims == 1
    assert worker.mutations == ["jobs:claimNextJob"]
    assert child.terminated and child.killed
    assert child.joins == [1, 1]
    assert not child.closed
    assert worker.journal_entry_scans == 2
    assert (worker.upload_journal.root / f"{'0' * 64}.json").exists()


def test_supervisor_sanitizes_uncaught_run_and_drain_then_forces_exit(
    capsys: pytest.CaptureFixture[str],
) -> None:
    hostile = (
        "poll decode report cleanup https://signed.invalid?token=secret "
        "/private/path filename.pdf content-marker"
    )

    class HostileSupervisorWorker:
        _supervisor_force_exit_required = True

        @staticmethod
        def run() -> None:
            raise RuntimeError(hostile)

        @staticmethod
        def _drain_upload_recovery() -> None:
            raise RuntimeError(hostile)

    exits: list[int] = []
    _run_supervised(  # type: ignore[arg-type]
        HostileSupervisorWorker(), exits.append
    )

    captured = capsys.readouterr()
    observable = f"{captured.out}\n{captured.err}"
    assert hostile not in observable
    assert "content-marker" not in observable
    assert "WORKER_RUN_FAILED" in observable
    assert "UPLOAD_DRAIN_FAILED" in observable
    assert "CHILD_PROCESS_STUCK" in observable
    assert exits == [70]


def test_supervisor_forces_exit_when_stable_log_sink_fails(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    hostile = "signed-url token /private/path filename.pdf content-marker"

    class StubbornWorker:
        _supervisor_force_exit_required = True

        @staticmethod
        def run() -> None:
            raise RuntimeError(hostile)

        @staticmethod
        def _drain_upload_recovery() -> None:
            raise RuntimeError(hostile)

    monkeypatch.setattr(
        "builtins.print",
        lambda *_args, **_kwargs: (_ for _ in ()).throw(RuntimeError(hostile)),
    )
    exits: list[int] = []

    _run_supervised(StubbornWorker(), exits.append)  # type: ignore[arg-type]

    assert exits == [70]


def test_supervisor_failure_exit_has_no_hostile_cause_or_traceback(
    capsys: pytest.CaptureFixture[str],
) -> None:
    hostile = "UPLOAD_SECRET_MARKER"

    class HostileSupervisorWorker:
        _supervisor_force_exit_required = False

        @staticmethod
        def run() -> None:
            raise RuntimeError(hostile)

        @staticmethod
        def _drain_upload_recovery() -> None:
            raise RuntimeError(hostile)

    with pytest.raises(SystemExit) as captured_error:
        _run_supervised(HostileSupervisorWorker())  # type: ignore[arg-type]

    assert captured_error.value.code == 1
    assert captured_error.value.__cause__ is None
    formatted = "".join(
        traceback.format_exception(
            type(captured_error.value),
            captured_error.value,
            captured_error.value.__traceback__,
        )
    )
    captured = capsys.readouterr()
    observable = f"{formatted}\n{captured.out}\n{captured.err}"
    assert hostile not in observable
    assert "WORKER_RUN_FAILED" in observable
    assert "UPLOAD_DRAIN_FAILED" in observable


def test_main_startup_boundary_suppresses_hostile_constructor_traceback(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    hostile = (
        "https://signed.invalid?token=secret password /private/path "
        "filename.pdf content-marker"
    )
    monkeypatch.setenv("ZENPDF_CONVEX_URL", "https://example.invalid")
    monkeypatch.setenv("ZENPDF_WORKER_TOKEN", "worker-token")

    def fail_constructor(*_args: object, **_kwargs: object) -> None:
        raise RuntimeError(hostile)

    monkeypatch.setattr("zenpdf_worker.worker.ZenPdfWorker", fail_constructor)
    with pytest.raises(SystemExit) as captured_error:
        main()

    assert captured_error.value.code == 1
    assert captured_error.value.__cause__ is None
    formatted = "".join(
        traceback.format_exception(
            type(captured_error.value),
            captured_error.value,
            captured_error.value.__traceback__,
        )
    )
    captured = capsys.readouterr()
    observable = f"{formatted}\n{captured.out}\n{captured.err}"
    assert hostile not in observable
    assert "content-marker" not in observable
    assert "WORKER_STARTUP_FAILED" in observable


def test_stubborn_upload_and_unreadable_journal_still_exit_70(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    hostile = "signed-url token /private/path content-marker"

    class StubbornProcess:
        pid = 424244

        @staticmethod
        def is_alive() -> bool:
            return True

        @staticmethod
        def join(timeout: int | None = None) -> None:
            return None

        @staticmethod
        def terminate() -> None:
            return None

        @staticmethod
        def kill() -> None:
            return None

    monkeypatch.setattr(
        "zenpdf_worker.worker.os.killpg",
        lambda *_args: (_ for _ in ()).throw(ProcessLookupError()),
    )
    worker = _UploadWorker()
    worker.upload_journal.ensure_ready()
    unreadable = worker.upload_journal.root / f"{'0' * 64}.json"
    unreadable.write_text(hostile, encoding="utf-8")
    unreadable.chmod(0o600)
    worker._stop_upload_process(StubbornProcess())  # type: ignore[arg-type]
    monkeypatch.setattr(worker, "run", lambda: None)
    exits: list[int] = []

    _run_supervised(worker, exits.append)

    captured = capsys.readouterr()
    assert hostile not in captured.out
    assert "UPLOAD_DRAIN_FAILED" in captured.out
    assert exits == [70]
    assert unreadable.exists()


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
    retry = recovered_worker.upload_journal.load("pending-1")
    assert retry is not None
    retry["nextAttemptAt"] = 0
    recovered_worker.upload_journal.save(retry)
    recovered_worker._recover_pending_uploads()
    assert recovered_worker.upload_journal.entries() == []


def test_completion_response_loss_keeps_committed_output(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker = _RecoveryUploadWorker()
    entry = {
        "jobId": "job-1",
        "pendingUploadId": "pending-1",
        "storageId": "stored-committed",
        "action": "uploaded",
    }
    worker.upload_journal.save(entry)
    worker.upload_state = "committed"

    worker._discard_pending_upload("pending-1")

    assert worker.upload_journal.entries() == []
    assert all(path != "files:discardWorkerUpload" for path, _args in worker.mutations)


def test_lost_completion_and_cleanup_removed_row_reconcile_idempotently(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker = _RecoveryUploadWorker()
    entry = {
        "jobId": "job-1",
        "pendingUploadId": "pending-1",
        "storageId": "stored-cleaned",
        "action": "uploaded",
    }
    worker.upload_journal.save(entry)
    worker.upload_state = "registered"
    worker.discard_failures = 1

    worker._discard_pending_upload("pending-1")
    retained = worker.upload_journal.load("pending-1")
    assert retained is not None
    assert retained["action"] == "discard"

    worker.upload_state = "deleted"
    discard_calls = sum(
        path == "files:discardWorkerUpload" for path, _args in worker.mutations
    )
    retry = worker.upload_journal.load("pending-1")
    assert retry is not None
    retry["nextAttemptAt"] = 0
    worker.upload_journal.save(retry)
    worker._recover_pending_uploads()
    assert worker.upload_journal.entries() == []
    assert (
        sum(path == "files:discardWorkerUpload" for path, _args in worker.mutations)
        == discard_calls
    )


def test_missing_pending_row_with_live_unknown_object_is_retained(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    worker = _RecoveryUploadWorker()
    entry = {
        "jobId": "job-1",
        "pendingUploadId": "pending-1",
        "storageId": "stored-unknown",
        "action": "discard",
    }
    worker.upload_journal.save(entry)
    worker.upload_state = "orphaned"

    worker._recover_pending_uploads()

    assert worker.upload_journal.load("pending-1") == entry
    assert all(path != "files:discardWorkerUpload" for path, _args in worker.mutations)


def test_recovery_backoff_prevents_hot_loop(monkeypatch: pytest.MonkeyPatch) -> None:
    worker = _RecoveryUploadWorker()
    worker.upload_journal.save(
        {
            "jobId": "job-1",
            "pendingUploadId": "pending-1",
            "storageId": "stored-unknown",
            "action": "discard",
        }
    )
    worker.upload_state = "orphaned"

    assert worker._recover_pending_uploads() == 1
    mutation_count = len(worker.mutations)
    assert worker._recover_pending_uploads() == 0
    assert len(worker.mutations) == mutation_count


def test_shutdown_recovery_drain_has_hard_deadline(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    blocked = threading.Event()

    class BlockedRecoveryWorker(_RecoveryUploadWorker):
        def _query(self, _path: str, _args: dict) -> object:
            blocked.wait(60)
            return "registered"

    monkeypatch.setenv("ZENPDF_UPLOAD_SHUTDOWN_GRACE_SECONDS", "1")
    monkeypatch.setenv("ZENPDF_UPLOAD_SHUTDOWN_MAX_OPERATIONS", "2")
    worker = BlockedRecoveryWorker()
    worker.upload_journal.save(
        {
            "jobId": "job-1",
            "pendingUploadId": "pending-1",
            "storageId": "stored-1",
            "action": "uploaded",
        }
    )

    started_at = time.monotonic()
    worker._drain_upload_recovery()

    assert time.monotonic() - started_at < 2
    assert worker.upload_journal.has_entries()


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


def test_shutdown_cancels_running_tool_group_promptly(tmp_path: Path) -> None:
    worker = ZenPdfWorker("https://example.invalid", "worker-a", "token")
    descendant_file = tmp_path / "descendant.pid"

    def shutdown_after_start() -> None:
        deadline = time.monotonic() + 5
        while not descendant_file.exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        worker.request_shutdown()

    shutdown = threading.Thread(target=shutdown_after_start)
    shutdown.start()
    started_at = time.monotonic()
    try:
        with pytest.raises(WorkerShutdown, match="cancelled tool execution"):
            worker._run_tool_bounded(
                {}, [], tmp_path, threading.Event(), runner=_hung_tool
            )
    finally:
        shutdown.join(timeout=6)
    assert time.monotonic() - started_at < 4
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
