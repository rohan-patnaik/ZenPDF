"""Worker runtime for executing PDF tools."""

import os
import multiprocessing
import signal
import threading
import time
from pathlib import Path
from tempfile import TemporaryDirectory
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional
from urllib.parse import urlparse

try:
    import resource
except ImportError:  # pragma: no cover - worker production target is Linux
    resource = None

import requests

from .client import ConvexClient, ConvexError
from .upload_journal import UploadJournal
from .tools import (
    compare_pdfs,
    compress_pdf,
    crop_pdf,
    edit_pdf,
    excel_to_pdf,
    image_to_pdf,
    merge_pdfs,
    ocr_pdf,
    page_numbers_pdf,
    pdf_to_pdfa,
    pdf_to_docx,
    pdf_to_powerpoint,
    pdf_to_xlsx,
    pdf_to_jpg,
    powerpoint_to_pdf,
    protect_pdf,
    repair_pdf,
    redact_pdf,
    organize_pdf,
    rotate_pdf,
    scan_to_pdf,
    sign_pdf,
    split_pdf,
    unlock_pdf,
    watermark_pdf,
    web_to_pdf,
    word_to_pdf,
    zip_outputs,
)

TOOL_OUTPUT_SUFFIXES = {
    "merge": ("merged", None),
    "split": ("split", ".zip"),
    "compress": ("compressed", None),
    "pdf-to-word": ("word", ".docx"),
    "pdf-to-powerpoint": ("powerpoint", ".pptx"),
    "pdf-to-excel": ("excel", ".xlsx"),
    "word-to-pdf": ("word", ".pdf"),
    "powerpoint-to-pdf": ("powerpoint", ".pdf"),
    "excel-to-pdf": ("excel", ".pdf"),
    "edit-pdf": ("edited", ".pdf"),
    "pdf-to-jpg": ("jpg", ".zip"),
    "jpg-to-pdf": ("images", ".pdf"),
    "sign-pdf": ("signed", ".pdf"),
    "watermark": ("watermarked", None),
    "rotate": ("rotated", None),
    "html-to-pdf": ("html", ".pdf"),
    "unlock": ("unlocked", None),
    "protect": ("protected", None),
    "organize-pdf": ("organized", ".pdf"),
    "pdfa": ("pdfa", None),
    "repair": ("repaired", None),
    "page-numbers": ("numbered", None),
    "scan-to-pdf": ("scan", ".pdf"),
    "ocr-pdf": ("ocr", ".pdf"),
    "compare": ("compare", ".txt"),
    "redact": ("redacted", None),
    "crop": ("cropped", None),
}


def _strip_input_prefix(path: Path) -> Path:
    """Remove the two-digit download prefix from a filename if present."""
    name = path.name
    if "_" in name:
        prefix, remainder = name.split("_", 1)
        if prefix.isdigit() and len(prefix) == 2:
            name = remainder
    return Path(name)


def _normalize_extension(extension: str) -> str:
    """Ensure the file extension starts with a dot."""
    return extension if extension.startswith(".") else f".{extension}"


def _build_output_path(tool: str, inputs: List[Path], temp: Path) -> Path:
    """Compute a consistent output filename based on the tool and input."""
    if tool == "html-to-pdf":
        return temp / "html_to_pdf.pdf"
    if tool not in TOOL_OUTPUT_SUFFIXES or not inputs:
        return temp / "output.pdf"
    base_path = _strip_input_prefix(inputs[0])
    stem = base_path.stem or "output"
    suffix, extension = TOOL_OUTPUT_SUFFIXES[tool]
    resolved_extension = extension or (base_path.suffix or ".pdf")
    resolved_extension = _normalize_extension(resolved_extension)
    return temp / f"{stem}_{suffix}{resolved_extension}"


def _sanitize_filename_token(value: str) -> str:
    """Create a filesystem-safe lowercase token."""
    token = "".join(char.lower() if char.isalnum() else "-" for char in value)
    token = "-".join(part for part in token.split("-") if part)
    return token[:48] or "html"


def _build_html_output_path(url: str, temp: Path) -> Path:
    """Build a descriptive, unique output filename for HTML-to-PDF jobs."""
    try:
        hostname = urlparse(url).hostname or "html"
    except ValueError:
        hostname = "html"
    prefix = _sanitize_filename_token(hostname)
    timestamp = int(time.time())
    return temp / f"{prefix}_{timestamp}_html.pdf"


def _rename_output(source: Path, target: Path) -> Path:
    """Rename a generated output file if the target name differs."""
    if source == target:
        return source
    if target.exists():
        target.unlink()
    return source.rename(target)


def _parse_int(value: Any, default: int) -> int:
    """Parse an integer with a safe fallback."""
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _parse_int_strict(value: Any, field: str, default: int | None = None) -> int:
    """Parse an integer and fail on invalid user-provided values."""
    if value is None or (isinstance(value, str) and not value.strip()):
        if default is None:
            raise ValueError(f"{field} is required")
        return default
    try:
        return int(value)
    except (TypeError, ValueError) as error:
        raise ValueError(f"{field} must be an integer") from error


def _parse_bool(value: Any, default: bool = False) -> bool:
    """Parse a boolean from common string/number forms."""
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    normalized = str(value).strip().lower()
    if normalized in {"1", "true", "yes", "on", "y"}:
        return True
    if normalized in {"0", "false", "no", "off", "n"}:
        return False
    return default


MAX_DPI = 300

SIGN_IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".webp"}
SIGN_PKCS12_EXTENSIONS = {".p12", ".pfx"}


def _select_sign_inputs(inputs: List[Path]) -> tuple[Path, Path | None, Path | None]:
    """Return (source_pdf, signature_image, pkcs12_cert) from mixed sign inputs."""
    pdfs = [path for path in inputs if path.suffix.lower() == ".pdf"]
    if len(pdfs) != 1:
        raise ValueError("Sign PDF requires exactly one PDF input")
    images = [path for path in inputs if path.suffix.lower() in SIGN_IMAGE_EXTENSIONS]
    pkcs12_files = [path for path in inputs if path.suffix.lower() in SIGN_PKCS12_EXTENSIONS]
    if len(images) > 1:
        allowed = ", ".join(sorted(SIGN_IMAGE_EXTENSIONS))
        raise ValueError(f"Sign PDF accepts at most one signature image ({allowed})")
    if len(pkcs12_files) > 1:
        allowed = ", ".join(sorted(SIGN_PKCS12_EXTENSIONS))
        raise ValueError(f"Sign PDF accepts at most one PKCS12 certificate ({allowed})")
    return pdfs[0], (images[0] if images else None), (pkcs12_files[0] if pkcs12_files else None)


@dataclass
class ToolRunResult:
    """Return value for a tool run, including output paths and metadata."""
    outputs: List[Path]
    tool_result: Optional[Dict[str, Any]] = None


class JobOwnershipLost(RuntimeError):
    """Raised when this worker no longer owns the active job lease."""


class WorkerShutdown(RuntimeError):
    """Raised when shutdown cancels active worker processing."""


class SupervisorRestart(RuntimeError):
    """Raised after child termination becomes uncertain and claims must stop."""


TOOL_IPC_ERROR_CODES = {
    "USER_INPUT_INVALID",
    "TOOL_EXECUTION_FAILED",
    "TOOL_IO_ERROR",
    "TOOL_MEMORY_LIMIT",
}

STABLE_EXCEPTION_CODES = {
    "BACKEND_APPLICATION_ERROR",
    "BACKEND_TIMEOUT",
    "BACKEND_REQUEST_FAILED",
    "BACKEND_HTTP_ERROR",
    "BACKEND_INVALID_RESPONSE",
    "UPLOAD_FAILED",
    "UPLOAD_TIMEOUT",
    "UPLOAD_HTTP_ERROR",
    "UPLOAD_IO_ERROR",
    "UPLOAD_RECOVERY_UNRESOLVED",
}

UPLOAD_IPC_ERROR_CODES = {
    "UPLOAD_FAILED",
    "UPLOAD_TIMEOUT",
    "UPLOAD_HTTP_ERROR",
    "UPLOAD_IO_ERROR",
}


def _strict_upload_http_code(value: object) -> str | None:
    """Accept only canonical UPLOAD_HTTP_100 through UPLOAD_HTTP_599."""
    if not isinstance(value, str) or not value.startswith("UPLOAD_HTTP_"):
        return None
    suffix = value.removeprefix("UPLOAD_HTTP_")
    if len(suffix) != 3 or not suffix.isascii() or not suffix.isdigit():
        return None
    status = int(suffix)
    return value if 100 <= status <= 599 else None


def _stable_upload_ipc_code(value: object) -> str:
    """Constrain child upload errors to the upload transport vocabulary."""
    if isinstance(value, str) and value in UPLOAD_IPC_ERROR_CODES:
        return value
    return _strict_upload_http_code(value) or "UPLOAD_FAILED"


def _stable_exception_code(error: BaseException, default: str) -> str:
    """Classify an exception without retaining attacker-controlled text."""
    if isinstance(error, ConvexError):
        return "BACKEND_APPLICATION_ERROR"
    if isinstance(error, requests.Timeout):
        return "NETWORK_TIMEOUT"
    if isinstance(error, requests.RequestException):
        return "NETWORK_REQUEST_FAILED"
    if isinstance(error, OSError):
        return "LOCAL_IO_ERROR"
    value = error.args[0] if error.args and isinstance(error.args[0], str) else ""
    if value in STABLE_EXCEPTION_CODES:
        return value
    upload_http_code = _strict_upload_http_code(value)
    if upload_http_code is not None:
        return upload_http_code
    return default


def _log_stable(message: str) -> None:
    """Emit a pre-sanitized boundary message without blocking forced exit."""
    try:
        print(message)
    except BaseException:  # noqa: BLE001 - logging must not strand live children
        pass


def _positive_env_int(name: str, default: int) -> int:
    """Read a positive integer runtime limit with a safe default."""
    try:
        value = int(os.environ.get(name, str(default)))
    except ValueError:
        return default
    return value if value > 0 else default


def _tool_process_entry(
    runner: Callable[[Dict[str, Any], List[Path], Path], ToolRunResult],
    job: Dict[str, Any],
    inputs: List[Path],
    temp: Path,
    connection: Any,
) -> None:
    """Run one tool in a new process group with kernel-enforced ceilings."""
    try:
        if hasattr(os, "setsid"):
            os.setsid()
        if resource is not None:
            memory_bytes = _positive_env_int("ZENPDF_JOB_MEMORY_BYTES", 4 * 1024**3)
            output_bytes = _positive_env_int("ZENPDF_JOB_OUTPUT_BYTES", 2 * 1024**3)
            cpu_seconds = _positive_env_int("ZENPDF_JOB_CPU_SECONDS", 300)
            resource.setrlimit(resource.RLIMIT_AS, (memory_bytes, memory_bytes))
            resource.setrlimit(resource.RLIMIT_FSIZE, (output_bytes, output_bytes))
            resource.setrlimit(resource.RLIMIT_CPU, (cpu_seconds, cpu_seconds + 1))
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        connection.send(("ok", runner(job, inputs, temp)))
    except BaseException as error:  # noqa: BLE001 - child emits only safe codes
        if isinstance(error, ValueError):
            error_code = "USER_INPUT_INVALID"
        elif isinstance(error, MemoryError):
            error_code = "TOOL_MEMORY_LIMIT"
        elif isinstance(error, OSError):
            error_code = "TOOL_IO_ERROR"
        else:
            error_code = "TOOL_EXECUTION_FAILED"
        try:
            connection.send(("error", error_code))
        except (BrokenPipeError, EOFError, OSError):
            pass
    finally:
        connection.close()


def _upload_process_entry(
    output: Path,
    upload_url: str,
    request_timeout_seconds: int,
    connection: Any,
) -> None:
    """POST one output in a killable process and return only its storage ID."""
    session = requests.Session()
    try:
        if hasattr(os, "setsid"):
            os.setsid()
        with output.open("rb") as handle:
            response = session.post(
                upload_url,
                data=handle,
                headers={"Content-Type": "application/octet-stream"},
                timeout=(10, request_timeout_seconds),
            )
        response.raise_for_status()
        storage_id = response.json().get("storageId")
        if not isinstance(storage_id, str) or not storage_id:
            raise RuntimeError("Upload response did not include a storage ID")
        connection.send(("ok", storage_id))
    except BaseException as error:  # noqa: BLE001 - child emits only safe classes
        error_code = "UPLOAD_FAILED"
        if isinstance(error, requests.Timeout):
            error_code = "UPLOAD_TIMEOUT"
        elif isinstance(error, requests.HTTPError):
            status = getattr(getattr(error, "response", None), "status_code", None)
            if isinstance(status, int) and 100 <= status <= 599:
                error_code = f"UPLOAD_HTTP_{status}"
            else:
                error_code = "UPLOAD_HTTP_ERROR"
        elif isinstance(error, OSError):
            error_code = "UPLOAD_IO_ERROR"
        try:
            connection.send(("error", error_code))
        except (BrokenPipeError, EOFError, OSError):
            pass
    finally:
        session.close()
        connection.close()


class ZenPdfWorker:
    """Poll Convex for jobs and execute PDF tools."""

    def __init__(self, convex_url: str, worker_id: str, worker_token: str) -> None:
        """Initialize the worker with Convex configuration."""
        self.client = ConvexClient(convex_url)
        self.worker_id = worker_id
        self.worker_token = worker_token
        self._client_lock = threading.Lock()
        journal_root = Path(
            os.environ.get(
                "ZENPDF_UPLOAD_JOURNAL_DIR", "/var/lib/zenpdf-worker/upload-recovery"
            )
        )
        self.upload_journal = UploadJournal(journal_root)
        self._shutdown_requested = threading.Event()
        self._supervisor_restart_requested = threading.Event()
        self._stubborn_upload_processes: List[multiprocessing.Process] = []
        self._stubborn_tool_processes: List[multiprocessing.Process] = []

    @property
    def _supervisor_force_exit_required(self) -> bool:
        """Compatibility view of the dedicated supervisor-restart state."""
        return self._supervisor_restart_requested.is_set()

    def run(self) -> None:
        """Run the worker polling loop."""
        poll_interval = float(os.environ.get("ZENPDF_POLL_INTERVAL", "5"))
        self.upload_journal.ensure_ready()
        while not self._shutdown_requested.is_set():
            self._require_supervisor_ready()
            self._recover_pending_uploads()
            self._require_supervisor_ready()
            job = self._mutation(
                "jobs:claimNextJob",
                {"workerId": self.worker_id, "workerToken": self.worker_token},
            )
            if not job:
                self._shutdown_requested.wait(max(poll_interval, 0.0))
                continue
            self._process_job(job)
            self._require_supervisor_ready()

    def request_shutdown(self) -> None:
        """Stop claiming work; durable upload recovery is drained before return."""
        self._shutdown_requested.set()

    def _require_running(self) -> None:
        self._require_supervisor_ready()
        if self._shutdown_requested.is_set():
            raise WorkerShutdown("Worker shutdown requested")

    def _require_supervisor_ready(self) -> None:
        if self._supervisor_restart_requested.is_set():
            raise SupervisorRestart("Worker supervisor restart required")

    def _process_job(self, job: Dict[str, Any]) -> None:
        """Process a single job from Convex."""
        job_id = job["_id"]
        started = time.time()
        progress = {"value": 10}
        stop_event = threading.Event()
        ownership_lost = threading.Event()
        heartbeat: threading.Thread | None = None
        output_payload: List[Dict[str, Any]] = []
        uploads_committed = False
        try:
            if not self._report_with_retry(job_id, 10):
                raise JobOwnershipLost("Job lease was lost before processing started")
            heartbeat = threading.Thread(
                target=self._heartbeat,
                args=(job_id, progress, stop_event, ownership_lost),
                daemon=True,
            )
            heartbeat.start()
            with TemporaryDirectory() as temp:
                temp_path = Path(temp)
                inputs = self._download_inputs(job["inputs"], temp_path)
                self._require_ownership(ownership_lost)
                progress["value"] = 40
                if not self._report_with_retry(job_id, 40):
                    ownership_lost.set()
                    raise JobOwnershipLost("Job lease was lost before tool execution")
                run_result = self._run_tool_bounded(job, inputs, temp_path, ownership_lost)
                self._require_ownership(ownership_lost)
                progress["value"] = 75
                if not self._report_with_retry(job_id, 75):
                    ownership_lost.set()
                    raise JobOwnershipLost("Job lease was lost before upload")
                output_payload = self._upload_outputs(
                    job_id, run_result.outputs, ownership_lost
                )
                self._require_ownership(ownership_lost)
            elapsed_minutes = max((time.time() - started) / 60, 0.01)
            bytes_processed = sum(item.get("sizeBytes", 0) for item in job["inputs"])
            completed = self._mutation(
                "jobs:completeJob",
                {
                    "jobId": job_id,
                    "workerId": self.worker_id,
                    "outputs": output_payload,
                    "toolResult": run_result.tool_result,
                    "minutesUsed": elapsed_minutes,
                    "bytesProcessed": bytes_processed,
                    "workerToken": self.worker_token,
                },
            )
            if not self._is_owned_job(completed, "succeeded"):
                ownership_lost.set()
                raise JobOwnershipLost("Job lease was lost before completion")
            uploads_committed = True
            self._confirm_uploaded_outputs(output_payload)
        except SupervisorRestart:
            raise
        except WorkerShutdown:
            print(f"Job {job_id} stopped for worker shutdown")
        except JobOwnershipLost:
            print(f"Job {job_id} ownership lost")
        except ValueError as error:
            self._safe_fail(
                job_id,
                "USER_INPUT_INVALID",
                "Input configuration is invalid.",
                "USER_INPUT_INVALID",
                ownership_lost,
            )
        except ConvexError as error:
            self._safe_fail(
                job_id,
                "SERVICE_CAPACITY_TEMPORARY",
                "Processing failed. Please retry.",
                error.code,
                ownership_lost,
            )
        except Exception as error:  # noqa: BLE001
            self._safe_fail(
                job_id,
                "SERVICE_CAPACITY_TEMPORARY",
                "Processing failed. Please retry.",
                _stable_exception_code(error, "WORKER_OPERATION_FAILED"),
                ownership_lost,
            )
        finally:
            stop_event.set()
            if heartbeat is not None:
                heartbeat.join(timeout=1)
            if (
                output_payload
                and not uploads_committed
                and not self._supervisor_restart_requested.is_set()
            ):
                self._discard_uploaded_outputs(output_payload)

    def _is_owned_job(self, result: Any, expected_status: str = "running") -> bool:
        """Validate that a mutation result still belongs to this worker."""
        return (
            isinstance(result, dict)
            and result.get("status") == expected_status
            and result.get("claimedBy") == self.worker_id
        )

    @staticmethod
    def _require_ownership(ownership_lost: threading.Event) -> None:
        if ownership_lost.is_set():
            raise JobOwnershipLost("Job ownership changed while work was in progress")

    def _report(self, job_id: str, progress: int) -> bool:
        """Update job progress and renew the lease."""
        result = self._mutation(
            "jobs:reportJobProgress",
            {
                "jobId": job_id,
                "workerId": self.worker_id,
                "progress": progress,
                "workerToken": self.worker_token,
            },
        )
        return self._is_owned_job(result)

    def _report_with_retry(self, job_id: str, progress: int) -> bool:
        """Renew a lease with bounded retries for transient transport failures."""
        attempts = _positive_env_int("ZENPDF_HEARTBEAT_RETRIES", 3)
        retry_delay = float(os.environ.get("ZENPDF_HEARTBEAT_RETRY_SECONDS", "0.5"))
        for attempt in range(attempts):
            try:
                return self._report(job_id, progress)
            except (ConvexError, OSError, requests.RequestException, RuntimeError):
                if attempt + 1 >= attempts:
                    return False
                time.sleep(max(retry_delay, 0.0) * (attempt + 1))
        return False

    def _fail(
        self,
        job_id: str,
        error_code: str,
        error_message: str,
        log_message: str | None = None,
    ) -> None:
        """Report a failed job with a friendly error."""
        safe_log_code = (
            log_message
            if isinstance(log_message, str)
            and log_message.replace("_", "").isalnum()
            and len(log_message) <= 64
            else "JOB_FAILED"
        )
        print(f"Job {job_id} failed ({safe_log_code})")
        result = self._mutation(
            "jobs:failJob",
            {
                "jobId": job_id,
                "workerId": self.worker_id,
                "errorCode": error_code,
                "errorMessage": error_message,
                "workerToken": self.worker_token,
            },
        )
        if not self._is_owned_job(result, "failed"):
            raise JobOwnershipLost("Job ownership changed before failure was recorded")

    def _safe_fail(
        self,
        job_id: str,
        error_code: str,
        error_message: str,
        log_message: str | None = None,
        ownership_lost: threading.Event | None = None,
    ) -> None:
        """Attempt to report a failure without crashing the worker."""
        if ownership_lost is not None and ownership_lost.is_set():
            return
        try:
            self._fail(job_id, error_code, error_message, log_message)
        except Exception as error:  # noqa: BLE001
            error_class = _stable_exception_code(error, "FAILURE_REPORT_FAILED")
            print(f"Failed to report job failure for {job_id} ({error_class})")

    def _heartbeat(
        self,
        job_id: str,
        progress: Dict[str, int],
        stop_event: threading.Event,
        ownership_lost: threading.Event,
    ) -> None:
        """Heartbeat loop that renews the job lease."""
        interval = float(os.environ.get("ZENPDF_WORKER_HEARTBEAT_SECONDS", "25"))
        while not stop_event.wait(interval):
            if not self._report_with_retry(job_id, progress["value"]):
                ownership_lost.set()
                return

    def _download_inputs(self, inputs: List[Dict[str, Any]], temp: Path) -> List[Path]:
        """Download job inputs to a temporary directory."""
        paths: List[Path] = []
        for index, item in enumerate(inputs, start=1):
            url = self._query(
                "files:getDownloadUrl",
                {
                    "storageId": item["storageId"],
                    "workerToken": self.worker_token,
                },
            )
            if not url:
                raise RuntimeError("Missing download URL")
            filename = f"{index:02d}_{Path(item['filename']).name}"
            target = temp / filename
            with requests.get(url, stream=True, timeout=120) as response:
                response.raise_for_status()
                with target.open("wb") as handle:
                    for chunk in response.iter_content(chunk_size=1024 * 1024):
                        if chunk:
                            handle.write(chunk)
            paths.append(target)
        return paths

    def _run_tool_bounded(
        self,
        job: Dict[str, Any],
        inputs: List[Path],
        temp: Path,
        ownership_lost: threading.Event,
        runner: Callable[[Dict[str, Any], List[Path], Path], ToolRunResult] | None = None,
    ) -> ToolRunResult:
        """Execute a tool in a killable process group with a hard wall-time."""
        context = multiprocessing.get_context("spawn")
        receiver, sender = context.Pipe(duplex=False)
        process = context.Process(
            target=_tool_process_entry,
            args=(runner or ZenPdfWorker._run_tool, job, inputs, temp, sender),
            daemon=False,
        )
        process.start()
        sender.close()
        timeout_seconds = _positive_env_int("ZENPDF_JOB_WALL_SECONDS", 600)
        deadline = time.monotonic() + timeout_seconds
        message: Any = None
        try:
            while process.is_alive():
                if self._shutdown_requested.is_set():
                    self._terminate_tool_process(process)
                    raise WorkerShutdown("Worker shutdown cancelled tool execution")
                if ownership_lost.is_set():
                    self._terminate_tool_process(process)
                    raise JobOwnershipLost("Job ownership changed during tool execution")
                if time.monotonic() >= deadline:
                    self._terminate_tool_process(process)
                    raise RuntimeError("Tool execution exceeded its hard wall-time limit")
                if receiver.poll(0.1):
                    message = receiver.recv()
                    process.join(timeout=1)
                    break
            process.join(timeout=0)
            if message is None:
                if not receiver.poll(0.5):
                    raise RuntimeError(
                        f"Tool process exited without a result (status {process.exitcode})"
                    )
                message = receiver.recv()
            if message[0] == "ok" and isinstance(message[1], ToolRunResult):
                return message[1]
            if message[0] == "error":
                error_code = message[1] if len(message) > 1 else "TOOL_EXECUTION_FAILED"
                if error_code not in TOOL_IPC_ERROR_CODES:
                    error_code = "TOOL_EXECUTION_FAILED"
                if error_code == "USER_INPUT_INVALID":
                    raise ValueError("Input configuration is invalid.")
                raise RuntimeError(error_code)
            raise RuntimeError("Tool process returned an invalid result")
        finally:
            try:
                receiver.close()
            finally:
                stopped = (
                    False
                    if process in self._stubborn_tool_processes
                    else self._terminate_tool_process(process)
                )
                if stopped:
                    process.close()
                else:
                    raise SupervisorRestart(
                        "Tool child termination requires supervisor restart"
                    ) from None

    def _terminate_tool_process(self, process: multiprocessing.Process) -> bool:
        """Bound tool TERM/KILL joins and retain a stubborn live handle."""
        join_seconds = min(
            _positive_env_int("ZENPDF_TOOL_PROCESS_JOIN_SECONDS", 2), 5
        )
        return self._terminate_process_group(
            process, join_seconds, self._stubborn_tool_processes
        )

    def _terminate_process_group(
        self,
        process: multiprocessing.Process,
        join_seconds: int,
        stubborn_processes: List[multiprocessing.Process],
    ) -> bool:
        """Terminate a child group without leaking exceptions or live handles."""

        def retain_for_supervisor() -> bool:
            if process not in stubborn_processes:
                stubborn_processes.append(process)
            self._supervisor_restart_requested.set()
            return False

        try:
            pid = process.pid
            alive = process.is_alive()
        except BaseException:  # noqa: BLE001 - process errors may contain secrets
            return retain_for_supervisor()
        if not alive:
            try:
                process.join(timeout=0)
            except BaseException:  # noqa: BLE001 - retain on uncertain child state
                return retain_for_supervisor()
            return True
        if pid is None:
            return retain_for_supervisor()

        stages = (
            (signal.SIGTERM, process.terminate),
            (signal.SIGKILL, process.kill),
        )
        for child_signal, fallback in stages:
            try:
                os.killpg(pid, child_signal)
            except BaseException:  # noqa: BLE001 - fallback is still bounded
                try:
                    fallback()
                except BaseException:  # noqa: BLE001 - force cgroup exit below
                    pass
            try:
                process.join(timeout=join_seconds)
                alive = process.is_alive()
            except BaseException:  # noqa: BLE001 - child state is now uncertain
                return retain_for_supervisor()
            if not alive:
                return True
        return retain_for_supervisor()

    @staticmethod
    def _run_tool(
        job: Dict[str, Any], inputs: List[Path], temp: Path
    ) -> ToolRunResult:
        """
        Dispatch and execute the PDF tool specified by a job.
        
        The function reads tool and config from `job`, validates required config fields for each tool, invokes the corresponding PDF helper, and returns a ToolRunResult containing produced output file paths plus any tool-specific result metadata.
        
        Returns:
            ToolRunResult: Output paths and optional tool metadata.
        
        Raises:
            ValueError: When required configuration or inputs for a specific tool are missing or invalid (e.g., missing watermark text, password, URL, or insufficient input files).
            RuntimeError: When the job specifies an unsupported tool.
        """
        tool = job["tool"]
        config = job.get("config")
        if not isinstance(config, dict):
            config = {}
        output_path = _build_output_path(tool, inputs, temp)
        if tool == "merge":
            return ToolRunResult([merge_pdfs(inputs, output_path)])
        if tool == "split":
            outputs = split_pdf(
                inputs[0],
                temp,
                config.get("ranges"),
                tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
            )
            zip_path = temp / "split_output.zip"
            return ToolRunResult([zip_outputs(outputs, zip_path)])
        if tool == "compress":
            compressed_path, tool_result = compress_pdf(inputs[0], output_path)
            return ToolRunResult([compressed_path], tool_result)
        if tool == "repair":
            return ToolRunResult([repair_pdf(inputs[0], output_path)])
        if tool == "rotate":
            angle = _parse_int_strict(config.get("angle"), "Angle", default=90)
            if angle not in (90, 180, 270):
                raise ValueError("Angle must be 90, 180, or 270")
            return ToolRunResult(
                [
                    rotate_pdf(
                        inputs[0],
                        output_path,
                        angle,
                        config.get("pages"),
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "organize-pdf":
            return ToolRunResult(
                [
                    organize_pdf(
                        inputs[0],
                        output_path,
                        config.get("order"),
                        config.get("delete"),
                        config.get("rotate"),
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "watermark":
            text = config.get("text") or ""
            if not str(text).strip():
                raise ValueError("Watermark text is required")
            return ToolRunResult(
                [
                    watermark_pdf(
                        inputs[0],
                        output_path,
                        str(text),
                        config.get("pages"),
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "page-numbers":
            start = _parse_int(config.get("start"), 1)
            return ToolRunResult(
                [
                    page_numbers_pdf(
                        inputs[0],
                        output_path,
                        start,
                        config.get("pages"),
                        str(config.get("numberingMode") or "selectionIndex"),
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "crop":
            margins = config.get("margins") or ""
            if not str(margins).strip():
                raise ValueError("Margins are required")
            return ToolRunResult(
                [
                    crop_pdf(
                        inputs[0],
                        output_path,
                        str(margins),
                        config.get("pages"),
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "redact":
            text = config.get("text") or ""
            if not str(text).strip():
                raise ValueError("Text to redact is required")
            return ToolRunResult(
                [
                    redact_pdf(
                        inputs[0],
                        output_path,
                        str(text),
                        config.get("pages"),
                        case_sensitive=_parse_bool(config.get("caseSensitive"), False),
                        whole_word=_parse_bool(config.get("wholeWord"), False),
                        use_regex=_parse_bool(config.get("regex"), False),
                        ocr_assist=_parse_bool(config.get("ocrAssist"), False),
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "edit-pdf":
            return ToolRunResult([edit_pdf(inputs[0], output_path, config.get("operations"))])
        if tool == "compare":
            if len(inputs) != 2:
                raise ValueError("Two PDF files are required")
            return ToolRunResult(
                [
                    compare_pdfs(
                        inputs[0],
                        inputs[1],
                        output_path,
                        include_visual_diff=_parse_bool(
                            config.get("includeVisualDiff"), True
                        ),
                    )
                ]
            )
        if tool == "unlock":
            password = config.get("password") if config else ""
            return ToolRunResult([unlock_pdf(inputs[0], output_path, str(password or ""))])
        if tool == "protect":
            password = config.get("password") or ""
            if not str(password).strip():
                raise ValueError("Password is required")
            return ToolRunResult([protect_pdf(inputs[0], output_path, str(password))])
        if tool == "jpg-to-pdf":
            return ToolRunResult([image_to_pdf(inputs, output_path)])
        if tool == "scan-to-pdf":
            return ToolRunResult([scan_to_pdf(inputs, output_path)])
        if tool == "sign-pdf":
            sign_source, signature_image, pkcs12_cert = _select_sign_inputs(inputs)
            text = str(config.get("text") or "").strip()
            x = _parse_int(config.get("x"), 36)
            y = _parse_int(config.get("y"), 36)
            anchor = str(config.get("anchor") or "custom").strip().lower()
            sign_mode = str(config.get("mode") or "visual").strip().lower()
            if sign_mode not in {"visual", "cryptographic"}:
                raise ValueError("Sign mode must be visual or cryptographic")
            pkcs12_password = str(config.get("pkcs12Password") or "")
            sign_output = temp / f"{_strip_input_prefix(sign_source).stem}_signed.pdf"
            return ToolRunResult(
                [
                    sign_pdf(
                        sign_source,
                        sign_output,
                        text,
                        config.get("pages"),
                        float(x),
                        float(y),
                        anchor=anchor,
                        mode=sign_mode,
                        signature_image_path=signature_image,
                        pkcs12_path=pkcs12_cert,
                        pkcs12_password=pkcs12_password,
                        tolerant_ranges=_parse_bool(config.get("tolerantRanges"), False),
                    )
                ]
            )
        if tool == "pdf-to-jpg":
            dpi = _parse_int(config.get("dpi"), 150)
            dpi = min(max(dpi, 72), MAX_DPI)
            images = pdf_to_jpg(inputs[0], temp, dpi)
            base_name = _strip_input_prefix(inputs[0])
            zip_stem = base_name.stem or "output"
            zip_path = temp / f"{zip_stem}.zip"
            return ToolRunResult([zip_outputs(images, zip_path)])
        if tool == "html-to-pdf":
            url = config.get("url")
            if not url:
                raise ValueError("URL is required")
            html_output = _build_html_output_path(str(url), temp)
            render_mode = str(config.get("renderMode") or "browser").strip().lower()
            if render_mode not in {"browser", "text"}:
                raise ValueError("Render mode must be browser or text")
            return ToolRunResult(
                [web_to_pdf(str(url), html_output, render_mode=render_mode)]
            )
        if tool == "word-to-pdf":
            if not inputs:
                raise ValueError("Word document is required")
            converted = word_to_pdf(inputs[0], temp)
            return ToolRunResult([_rename_output(converted, output_path)])
        if tool == "powerpoint-to-pdf":
            if not inputs:
                raise ValueError("PowerPoint document is required")
            converted = powerpoint_to_pdf(inputs[0], temp)
            return ToolRunResult([_rename_output(converted, output_path)])
        if tool == "excel-to-pdf":
            if not inputs:
                raise ValueError("Excel document is required")
            converted = excel_to_pdf(inputs[0], temp)
            return ToolRunResult([_rename_output(converted, output_path)])
        if tool == "pdfa":
            if not inputs:
                raise ValueError("PDF file is required")
            converted, pdfa_result = pdf_to_pdfa(inputs[0], output_path, include_report=True)
            return ToolRunResult([converted], pdfa_result)
        if tool == "pdf-to-word":
            if not inputs:
                raise ValueError("PDF file is required")
            mode = str(config.get("mode") or "auto").strip().lower()
            if mode not in {"auto", "layout", "text", "ocr"}:
                raise ValueError("Mode must be auto, layout, text, or ocr")
            return ToolRunResult(
                [
                    pdf_to_docx(
                        inputs[0],
                        output_path,
                        mode=mode,
                        ocr_profile=str(config.get("ocrProfile") or "balanced"),
                    )
                ]
            )
        if tool == "pdf-to-powerpoint":
            if not inputs:
                raise ValueError("PDF file is required")
            mode = str(config.get("mode") or "visual").strip().lower()
            if mode not in {"visual", "editable"}:
                raise ValueError("Mode must be visual or editable")
            return ToolRunResult([pdf_to_powerpoint(inputs[0], output_path, mode=mode)])
        if tool == "pdf-to-excel":
            if not inputs:
                raise ValueError("PDF file is required")
            mode = str(config.get("mode") or "auto").strip().lower()
            if mode not in {"auto", "table", "text", "ocr"}:
                raise ValueError("Mode must be auto, table, text, or ocr")
            return ToolRunResult(
                [
                    pdf_to_xlsx(
                        inputs[0],
                        output_path,
                        mode=mode,
                        ocr_profile=str(config.get("ocrProfile") or "balanced"),
                    )
                ]
            )
        if tool == "ocr-pdf":
            if not inputs:
                raise ValueError("PDF file is required")
            return ToolRunResult(
                [
                    ocr_pdf(
                        inputs[0],
                        output_path,
                        config.get("lang"),
                        profile=str(
                            config.get("ocrProfile")
                            or config.get("profile")
                            or "balanced"
                        ),
                    )
                ]
            )
        raise RuntimeError(f"Unsupported tool: {tool}")

    def _upload_outputs(
        self,
        job_id: str,
        outputs: List[Path],
        ownership_lost: threading.Event | None = None,
    ) -> List[Dict[str, Any]]:
        """Upload outputs through TTL-tracked, lease-aware pending records."""
        self.upload_journal.ensure_ready()
        self.upload_journal.ensure_capacity(
            required_entries=max(len(outputs), 1),
            required_bytes=max(len(outputs), 1)
            * self.upload_journal.max_entry_bytes,
        )
        payload: List[Dict[str, Any]] = []
        try:
            for output in outputs:
                if ownership_lost is not None:
                    self._require_ownership(ownership_lost)
                size_bytes = output.stat().st_size
                pending = self._mutation(
                    "files:beginWorkerUpload",
                    {
                        "jobId": job_id,
                        "workerId": self.worker_id,
                        "filename": output.name,
                        "sizeBytes": size_bytes,
                        "workerToken": self.worker_token,
                    },
                )
                if not isinstance(pending, dict):
                    raise JobOwnershipLost("Job lease was lost before upload URL issuance")
                upload_url = pending.get("uploadUrl")
                pending_id = pending.get("pendingUploadId")
                if (
                    not isinstance(upload_url, str)
                    or not upload_url.startswith(("https://", "http://"))
                    or not isinstance(pending_id, str)
                    or not pending_id
                ):
                    raise RuntimeError("Upload URL mutation returned an invalid result")
                storage_id = self._upload_one_pending(
                    job_id,
                    output,
                    upload_url,
                    pending_id,
                    pending.get("uploadDeadlineAt"),
                    ownership_lost,
                )
                payload.append(
                    {
                        "storageId": storage_id,
                        "pendingUploadId": pending_id,
                        "filename": output.name,
                        "sizeBytes": size_bytes,
                    }
                )
            return payload
        except SupervisorRestart:
            raise
        except BaseException:
            self._discard_uploaded_outputs(payload)
            raise

    def _upload_one_pending(
        self,
        job_id: str,
        output: Path,
        upload_url: str,
        pending_id: str,
        upload_deadline_at: Any,
        ownership_lost: threading.Event | None,
    ) -> str:
        """Run one POST under a hard deadline, then durably bind its storage ID."""
        configured_seconds = _positive_env_int("ZENPDF_UPLOAD_DEADLINE_SECONDS", 60)
        remaining_seconds = configured_seconds
        if isinstance(upload_deadline_at, (int, float)):
            remaining_seconds = min(
                configured_seconds,
                max(int((upload_deadline_at - time.time() * 1000) / 1000), 1),
            )
        self._require_running()
        start_method = os.environ.get("ZENPDF_UPLOAD_PROCESS_START_METHOD", "spawn")
        context = multiprocessing.get_context(start_method)
        receiver, sender = context.Pipe(duplex=False)
        process = context.Process(
            target=_upload_process_entry,
            args=(output, upload_url, remaining_seconds, sender),
            daemon=False,
        )
        process.start()
        sender.close()
        deadline = time.monotonic() + remaining_seconds
        outcome: tuple[Any, ...] | None = None
        try:
            while time.monotonic() < deadline:
                if self._shutdown_requested.is_set():
                    self._stop_upload_process(process)
                    raise WorkerShutdown("Worker shutdown cancelled output upload")
                if ownership_lost is not None and ownership_lost.is_set():
                    self._stop_upload_process(process)
                    raise JobOwnershipLost("Job ownership changed during output upload")
                if receiver.poll(0.1):
                    outcome = receiver.recv()
                    break
            if outcome is None:
                self._stop_upload_process(process)
                raise RuntimeError("Output upload exceeded its hard total deadline")
        finally:
            try:
                receiver.close()
            finally:
                stopped = (
                    False
                    if process in self._stubborn_upload_processes
                    else self._stop_upload_process(process)
                )
                if stopped:
                    process.close()
                else:
                    try:
                        self._retain_known_upload_for_restart(
                            job_id, pending_id, outcome
                        )
                    finally:
                        raise SupervisorRestart(
                            "Upload child termination requires supervisor restart"
                        ) from None
        if outcome[0] != "ok":
            error_code = _stable_upload_ipc_code(
                outcome[1] if len(outcome) > 1 else None
            )
            raise RuntimeError(f"Output upload failed ({error_code})")
        storage_id = outcome[1]
        if not isinstance(storage_id, str) or not storage_id:
            raise RuntimeError("Upload ended without a storage ID")
        entry = {
            "jobId": job_id,
            "pendingUploadId": pending_id,
            "storageId": storage_id,
            "action": "register",
        }
        self.upload_journal.save(entry)
        self._reconcile_upload_entry(entry)
        if entry.get("action") != "uploaded":
            raise RuntimeError("Uploaded object could not be registered")
        if ownership_lost is not None and ownership_lost.is_set():
            self._discard_pending_upload(pending_id, storage_id)
            raise JobOwnershipLost("Job ownership changed during output upload")
        return storage_id

    def _retain_known_upload_for_restart(
        self,
        job_id: str,
        pending_id: str,
        outcome: tuple[Any, ...] | None,
    ) -> None:
        """Fsync a returned storage ID without making another backend call."""
        if (
            not isinstance(outcome, tuple)
            or len(outcome) < 2
            or outcome[0] != "ok"
            or not isinstance(outcome[1], str)
            or not outcome[1]
        ):
            return
        self.upload_journal.save(
            {
                "jobId": job_id,
                "pendingUploadId": pending_id,
                "storageId": outcome[1],
                "action": "register",
            }
        )

    def _stop_upload_process(self, process: multiprocessing.Process) -> bool:
        """Bound both joins; ask the supervisor to kill an uninterruptible group."""
        join_seconds = min(
            _positive_env_int("ZENPDF_UPLOAD_PROCESS_JOIN_SECONDS", 1), 5
        )
        return self._terminate_process_group(
            process, join_seconds, self._stubborn_upload_processes
        )

    def _discard_pending_upload(
        self, pending_id: str, storage_id: str | None = None
    ) -> None:
        """Reconcile ambiguous completion before persisting deletion intent."""
        entry = self.upload_journal.load(pending_id)
        if entry is not None:
            try:
                self._reconcile_upload_entry(entry)
            except Exception as error:  # noqa: BLE001 - durable intent remains
                error_class = _stable_exception_code(error, "UPLOAD_CLEANUP_DEFERRED")
                print(f"Pending upload cleanup retained for retry ({error_class})")
            return
        if entry is None and storage_id is not None:
            entry = {
                "jobId": "unknown",
                "pendingUploadId": pending_id,
                "storageId": storage_id,
                "action": "discard",
            }
            self.upload_journal.save(entry)
        try:
            args = {
                "pendingUploadId": pending_id,
                "workerId": self.worker_id,
                "workerToken": self.worker_token,
            }
            if storage_id is not None:
                args["storageId"] = storage_id
            discarded = self._mutation(
                "files:discardWorkerUpload",
                args,
            )
            if discarded is True:
                self.upload_journal.remove(pending_id)
        except Exception as error:  # noqa: BLE001 - cleanup must not mask owner error
            error_class = _stable_exception_code(error, "UPLOAD_CLEANUP_DEFERRED")
            print(f"Pending upload cleanup retained for retry ({error_class})")

    def _discard_uploaded_outputs(self, outputs: List[Dict[str, Any]]) -> None:
        for output in outputs:
            pending_id = output.get("pendingUploadId")
            if isinstance(pending_id, str) and pending_id:
                self._discard_pending_upload(pending_id)

    def _confirm_uploaded_outputs(self, outputs: List[Dict[str, Any]]) -> None:
        """Remove recovery entries only after completion is confirmed."""
        for output in outputs:
            pending_id = output.get("pendingUploadId")
            if isinstance(pending_id, str) and pending_id:
                self.upload_journal.remove(pending_id)

    def _reconcile_upload_entry(self, entry: Dict[str, Any]) -> bool:
        """Advance one journal entry through idempotent server operations."""
        pending_id = entry.get("pendingUploadId")
        storage_id = entry.get("storageId")
        job_id = entry.get("jobId")
        if not all(isinstance(value, str) and value for value in (pending_id, storage_id)):
            return False
        action = entry.get("action")
        registration_rejected = False
        if action == "register":
            registered = self._mutation(
                "files:registerWorkerUpload",
                {
                    "pendingUploadId": pending_id,
                    "workerId": self.worker_id,
                    "storageId": storage_id,
                    "workerToken": self.worker_token,
                },
            )
            if registered is True:
                entry["action"] = "uploaded"
                self.upload_journal.save(entry)
                return True
            entry["action"] = "discard"
            self.upload_journal.save(entry)
            action = "discard"
            registration_rejected = True
        if (
            action in {"uploaded", "discard"}
            and not registration_rejected
            and isinstance(job_id, str)
            and job_id != "unknown"
        ):
            state = self._query(
                "files:getWorkerUploadState",
                {
                    "jobId": job_id,
                    "pendingUploadId": pending_id,
                    "workerId": self.worker_id,
                    "storageId": storage_id,
                    "workerToken": self.worker_token,
                },
            )
            if state in {"committed", "deleted"}:
                self.upload_journal.remove(pending_id)
                return True
            if state == "registered":
                entry["action"] = "discard"
                self.upload_journal.save(entry)
                action = "discard"
            elif state in {"orphaned", "mismatch"}:
                return False
        if action == "discard":
            discarded = self._mutation(
                "files:discardWorkerUpload",
                {
                    "pendingUploadId": pending_id,
                    "workerId": self.worker_id,
                    "storageId": storage_id,
                    "workerToken": self.worker_token,
                },
            )
            if discarded is True:
                self.upload_journal.remove(pending_id)
                return True
            return False
        return False

    def _recover_pending_uploads(self, limit: int | None = None) -> int:
        """Retry one bounded batch with persisted exponential backoff."""
        attempted = 0
        now_ms = int(time.time() * 1000)
        batch_limit = min(
            limit or self.upload_journal.batch_size,
            self.upload_journal.batch_size,
        )
        retry_base_ms = _positive_env_int("ZENPDF_UPLOAD_RETRY_BASE_MS", 1000)
        retry_max_ms = _positive_env_int("ZENPDF_UPLOAD_RETRY_MAX_MS", 300_000)
        for entry in self.upload_journal.entries(
            limit=batch_limit, ready_at=now_ms
        ):
            next_attempt_at = entry.get("nextAttemptAt")
            if isinstance(next_attempt_at, (int, float)) and next_attempt_at > now_ms:
                continue
            attempted += 1
            previous_attempts = entry.get("attempts", 0)
            if not isinstance(previous_attempts, int) or previous_attempts < 0:
                previous_attempts = 0
            entry.pop("attempts", None)
            entry.pop("nextAttemptAt", None)
            try:
                if not self._reconcile_upload_entry(entry):
                    raise RuntimeError("UPLOAD_RECOVERY_UNRESOLVED")
            except Exception as error:  # noqa: BLE001 - retry on next poll
                attempts = min(previous_attempts + 1, 30)
                delay_ms = min(retry_base_ms * (2 ** min(attempts - 1, 16)), retry_max_ms)
                entry["attempts"] = attempts
                entry["nextAttemptAt"] = now_ms + delay_ms
                try:
                    self.upload_journal.save(entry)
                except Exception:
                    pass
                error_class = _stable_exception_code(
                    error, "UPLOAD_RECOVERY_FAILED"
                )
                print(f"Upload recovery retained for retry ({error_class})")
        return attempted

    def _drain_upload_recovery(self) -> None:
        """Run a strictly bounded shutdown batch; durable state survives timeout."""
        if self._supervisor_restart_requested.is_set():
            self.upload_journal.entries(limit=1)
            return
        grace_seconds = _positive_env_int("ZENPDF_UPLOAD_SHUTDOWN_GRACE_SECONDS", 30)
        max_operations = _positive_env_int("ZENPDF_UPLOAD_SHUTDOWN_MAX_OPERATIONS", 64)
        deadline = time.monotonic() + grace_seconds
        operations = 0
        while (
            self.upload_journal.has_entries()
            and operations < max_operations
            and time.monotonic() < deadline
        ):
            outcome: Dict[str, Any] = {}
            finished = threading.Event()

            def recover_batch() -> None:
                try:
                    outcome["attempted"] = self._recover_pending_uploads(
                        min(
                            max_operations - operations,
                            self.upload_journal.batch_size,
                        )
                    )
                except BaseException as error:  # noqa: BLE001 - sanitize thread exit
                    outcome["errorCode"] = _stable_exception_code(
                        error, "UPLOAD_DRAIN_FAILED"
                    )
                finally:
                    finished.set()

            recovery_thread = threading.Thread(target=recover_batch, daemon=True)
            recovery_thread.start()
            finished.wait(max(deadline - time.monotonic(), 0.0))
            if not finished.is_set():
                return
            error_code = outcome.get("errorCode")
            if isinstance(error_code, str):
                raise RuntimeError(error_code) from None
            attempted = outcome.get("attempted", 0)
            operations += attempted
            if attempted == 0:
                return

    def _mutation(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a mutation with thread-safe access."""
        with self._client_lock:
            return self.client.mutation(path, args)

    def _query(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a query with thread-safe access."""
        with self._client_lock:
            return self.client.query(path, args)


def _run_supervised(
    worker: ZenPdfWorker, force_exit: Callable[[int], None] = os._exit
) -> None:
    """Sanitize the top boundary, always drain, then honor forced cgroup exit."""
    failed = False
    forced_exit_requested = False
    try:
        worker.run()
    except BaseException as error:  # noqa: BLE001 - final process boundary
        failed = True
        error_code = _stable_exception_code(error, "WORKER_RUN_FAILED")
        _log_stable(f"Worker run stopped ({error_code})")
    finally:
        try:
            worker._drain_upload_recovery()
        except BaseException as error:  # noqa: BLE001 - forced exit must still run
            failed = True
            error_code = _stable_exception_code(error, "UPLOAD_DRAIN_FAILED")
            _log_stable(f"Worker recovery drain stopped ({error_code})")
        if worker._supervisor_force_exit_required:
            forced_exit_requested = True
            _log_stable("Worker supervisor termination required (CHILD_PROCESS_STUCK)")
            force_exit(70)
    if forced_exit_requested:
        return
    if failed:
        raise SystemExit(1) from None


def main() -> None:
    """Entrypoint for the worker process."""
    try:
        convex_url = os.environ.get("ZENPDF_CONVEX_URL")
        if not convex_url:
            raise RuntimeError("ZENPDF_CONVEX_URL is required")
        worker_id = os.environ.get("ZENPDF_WORKER_ID", "worker-local")
        worker_token = os.environ.get("ZENPDF_WORKER_TOKEN")
        if not worker_token:
            raise RuntimeError("ZENPDF_WORKER_TOKEN is required")
        worker = ZenPdfWorker(convex_url, worker_id, worker_token)

        def request_shutdown(_signum: int, _frame: Any) -> None:
            worker.request_shutdown()

        signal.signal(signal.SIGTERM, request_shutdown)
        signal.signal(signal.SIGINT, request_shutdown)
    except BaseException as error:  # noqa: BLE001 - sanitized startup boundary
        error_code = _stable_exception_code(error, "WORKER_STARTUP_FAILED")
        _log_stable(f"Worker startup stopped ({error_code})")
        raise SystemExit(1) from None
    _run_supervised(worker)


if __name__ == "__main__":
    main()
