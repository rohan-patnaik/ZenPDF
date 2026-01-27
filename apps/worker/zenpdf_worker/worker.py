"""Worker runtime for executing PDF tools."""

import os
import threading
import time
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any, Dict, List

import requests

from .client import ConvexClient, ConvexError
from .tools import (
    compare_pdfs,
    compress_pdf,
    crop_pdf,
    highlight_pdf,
    image_to_pdf,
    merge_pdfs,
    office_to_pdf,
    page_numbers_pdf,
    pdf_to_pdfa,
    pdf_to_docx,
    pdf_to_docx_ocr,
    pdf_to_text,
    pdf_to_xlsx,
    pdf_to_xlsx_ocr,
    pdf_to_jpg,
    protect_pdf,
    repair_pdf,
    redact_pdf,
    remove_pages,
    reorder_pages,
    rotate_pdf,
    split_pdf,
    unlock_pdf,
    watermark_pdf,
    web_to_pdf,
    zip_outputs,
)

TOOL_OUTPUT_SUFFIXES = {
    "compress": ("compressed", None),
    "repair": ("repaired", None),
    "rotate": ("rotated", None),
    "remove-pages": ("trimmed", None),
    "reorder-pages": ("reordered", None),
    "watermark": ("watermarked", None),
    "page-numbers": ("numbered", None),
    "crop": ("cropped", None),
    "redact": ("redacted", None),
    "highlight": ("highlighted", None),
    "unlock": ("unlocked", None),
    "protect": ("protected", None),
    "pdfa": ("pdfa", None),
    "pdf-to-word": ("word", ".docx"),
    "pdf-to-text": ("text", ".txt"),
    "compare": ("compare", ".txt"),
    "pdf-to-word-ocr": ("word_ocr", ".docx"),
    "pdf-to-excel": ("excel", ".xlsx"),
    "pdf-to-excel-ocr": ("excel_ocr", ".xlsx"),
    "office-to-pdf": ("converted", ".pdf"),
    "image-to-pdf": ("images", ".pdf"),
    "web-to-pdf": ("web", ".pdf"),
}


def _strip_input_prefix(path: Path) -> Path:
    name = path.name
    if "_" in name:
        prefix, remainder = name.split("_", 1)
        if prefix.isdigit():
            name = remainder
    return Path(name)


def _normalize_extension(extension: str) -> str:
    return extension if extension.startswith(".") else f".{extension}"


def _build_output_path(tool: str, inputs: List[Path], temp: Path) -> Path:
    if tool == "web-to-pdf":
        return temp / "web_to_pdf.pdf"
    if tool not in TOOL_OUTPUT_SUFFIXES or not inputs:
        return temp / "output.pdf"
    base_path = _strip_input_prefix(inputs[0])
    stem = base_path.stem or "output"
    suffix, extension = TOOL_OUTPUT_SUFFIXES[tool]
    resolved_extension = extension or (base_path.suffix or ".pdf")
    resolved_extension = _normalize_extension(resolved_extension)
    return temp / f"{stem}_{suffix}{resolved_extension}"


def _rename_output(source: Path, target: Path) -> Path:
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


MAX_DPI = 300


class ZenPdfWorker:
    """Poll Convex for jobs and execute PDF tools."""

    def __init__(self, convex_url: str, worker_id: str, worker_token: str) -> None:
        """Initialize the worker with Convex configuration."""
        self.client = ConvexClient(convex_url)
        self.worker_id = worker_id
        self.worker_token = worker_token
        self._client_lock = threading.Lock()

    def run(self) -> None:
        """Run the worker polling loop."""
        poll_interval = float(os.environ.get("ZENPDF_POLL_INTERVAL", "5"))
        while True:
            job = self._mutation(
                "jobs:claimNextJob",
                {"workerId": self.worker_id, "workerToken": self.worker_token},
            )
            if not job:
                time.sleep(poll_interval)
                continue
            self._process_job(job)

    def _process_job(self, job: Dict[str, Any]) -> None:
        """Process a single job from Convex."""
        job_id = job["_id"]
        started = time.time()
        progress = {"value": 10}
        stop_event = threading.Event()
        heartbeat = threading.Thread(
            target=self._heartbeat, args=(job_id, progress, stop_event), daemon=True
        )
        heartbeat.start()
        try:
            self._report(job_id, 10)
            with TemporaryDirectory() as temp:
                temp_path = Path(temp)
                inputs = self._download_inputs(job["inputs"], temp_path)
                progress["value"] = 40
                self._report(job_id, 40)
                outputs = self._run_tool(job, inputs, temp_path)
                progress["value"] = 75
                self._report(job_id, 75)
                output_payload = self._upload_outputs(outputs)
            elapsed_minutes = max((time.time() - started) / 60, 0.01)
            bytes_processed = sum(item.get("sizeBytes", 0) for item in job["inputs"])
            self._mutation(
                "jobs:completeJob",
                {
                    "jobId": job_id,
                    "workerId": self.worker_id,
                    "outputs": output_payload,
                    "minutesUsed": elapsed_minutes,
                    "bytesProcessed": bytes_processed,
                    "workerToken": self.worker_token,
                },
            )
            self._report(job_id, 100)
        except ValueError as error:
            self._safe_fail(job_id, "USER_INPUT_INVALID", str(error), str(error))
        except ConvexError as error:
            self._safe_fail(
                job_id,
                "SERVICE_CAPACITY_TEMPORARY",
                "Processing failed. Please retry.",
                error.message,
            )
        except Exception as error:  # noqa: BLE001
            self._safe_fail(
                job_id,
                "SERVICE_CAPACITY_TEMPORARY",
                "Processing failed. Please retry.",
                str(error),
            )
        finally:
            stop_event.set()
            heartbeat.join(timeout=1)

    def _report(self, job_id: str, progress: int) -> None:
        """Update job progress and renew the lease."""
        self._mutation(
            "jobs:reportJobProgress",
            {
                "jobId": job_id,
                "workerId": self.worker_id,
                "progress": progress,
                "workerToken": self.worker_token,
            },
        )

    def _fail(
        self,
        job_id: str,
        error_code: str,
        error_message: str,
        log_message: str | None = None,
    ) -> None:
        """Report a failed job with a friendly error."""
        print(f"Job {job_id} failed: {log_message or error_message}")
        self._mutation(
            "jobs:failJob",
            {
                "jobId": job_id,
                "workerId": self.worker_id,
                "errorCode": error_code,
                "errorMessage": error_message,
                "workerToken": self.worker_token,
            },
        )

    def _safe_fail(
        self,
        job_id: str,
        error_code: str,
        error_message: str,
        log_message: str | None = None,
    ) -> None:
        """Attempt to report a failure without crashing the worker."""
        try:
            self._fail(job_id, error_code, error_message, log_message)
        except Exception as error:  # noqa: BLE001
            print(f"Failed to report job failure for {job_id}: {error}")

    def _heartbeat(
        self, job_id: str, progress: Dict[str, int], stop_event: threading.Event
    ) -> None:
        """Heartbeat loop that renews the job lease."""
        interval = float(os.environ.get("ZENPDF_WORKER_HEARTBEAT_SECONDS", "25"))
        while not stop_event.wait(interval):
            self._report(job_id, progress["value"])

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

    def _run_tool(self, job: Dict[str, Any], inputs: List[Path], temp: Path) -> List[Path]:
        """
        Dispatches and executes the PDF tool specified by a job and returns the generated output file paths.
        
        The function reads tool and config from `job`, validates required config fields for each tool, invokes the corresponding PDF helper, and returns a list of filesystem Paths pointing to produced output files (single files or archive files for multi-file outputs).
        
        Returns:
            List[Path]: Paths to the generated output file(s).
        
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
            return [merge_pdfs(inputs, output_path)]
        if tool == "split":
            outputs = split_pdf(inputs[0], temp, config.get("ranges"))
            zip_path = temp / "split_output.zip"
            return [zip_outputs(outputs, zip_path)]
        if tool == "compress":
            return [compress_pdf(inputs[0], output_path)]
        if tool == "repair":
            return [repair_pdf(inputs[0], output_path)]
        if tool == "rotate":
            angle = _parse_int(config.get("angle"), 90)
            if angle not in (90, 180, 270):
                angle = 90
            return [rotate_pdf(inputs[0], output_path, angle, config.get("pages"))]
        if tool == "remove-pages":
            pages = config.get("pages") or ""
            if not pages.strip():
                return [merge_pdfs([inputs[0]], output_path)]
            return [remove_pages(inputs[0], output_path, pages)]
        if tool == "reorder-pages":
            order = config.get("order") or ""
            if not order.strip():
                return [merge_pdfs([inputs[0]], output_path)]
            return [reorder_pages(inputs[0], output_path, order)]
        if tool == "watermark":
            text = config.get("text") or ""
            if not str(text).strip():
                raise ValueError("Watermark text is required")
            return [
                watermark_pdf(inputs[0], output_path, str(text), config.get("pages"))
            ]
        if tool == "page-numbers":
            start = _parse_int(config.get("start"), 1)
            return [
                page_numbers_pdf(inputs[0], output_path, start, config.get("pages"))
            ]
        if tool == "crop":
            margins = config.get("margins") or ""
            if not str(margins).strip():
                raise ValueError("Margins are required")
            return [crop_pdf(inputs[0], output_path, str(margins), config.get("pages"))]
        if tool == "redact":
            text = config.get("text") or ""
            if not str(text).strip():
                raise ValueError("Text to redact is required")
            return [
                redact_pdf(inputs[0], output_path, str(text), config.get("pages"))
            ]
        if tool == "compare":
            if len(inputs) != 2:
                raise ValueError("Two PDF files are required")
            return [compare_pdfs(inputs[0], inputs[1], output_path)]
        if tool == "highlight":
            if not inputs:
                raise ValueError("PDF file is required")
            text = config.get("text") or ""
            if not str(text).strip():
                raise ValueError("Text to highlight is required")
            return [
                highlight_pdf(inputs[0], output_path, str(text), config.get("pages"))
            ]
        if tool == "unlock":
            password = config.get("password") or ""
            if not str(password).strip():
                raise ValueError("Password is required")
            return [unlock_pdf(inputs[0], output_path, str(password))]
        if tool == "protect":
            password = config.get("password") or ""
            if not str(password).strip():
                raise ValueError("Password is required")
            return [protect_pdf(inputs[0], output_path, str(password))]
        if tool == "image-to-pdf":
            return [image_to_pdf(inputs, output_path)]
        if tool == "pdf-to-jpg":
            dpi = _parse_int(config.get("dpi"), 150)
            dpi = min(max(dpi, 72), MAX_DPI)
            images = pdf_to_jpg(inputs[0], temp, dpi)
            zip_path = temp / "pdf_pages.zip"
            return [zip_outputs(images, zip_path)]
        if tool == "web-to-pdf":
            url = config.get("url")
            if not url:
                raise ValueError("URL is required")
            return [web_to_pdf(str(url), output_path)]
        if tool == "office-to-pdf":
            if not inputs:
                raise ValueError("Office file is required")
            converted = office_to_pdf(inputs[0], temp)
            return [_rename_output(converted, output_path)]
        if tool == "pdfa":
            if not inputs:
                raise ValueError("PDF file is required")
            return [pdf_to_pdfa(inputs[0], output_path)]
        if tool == "pdf-to-word":
            if not inputs:
                raise ValueError("PDF file is required")
            return [pdf_to_docx(inputs[0], output_path)]
        if tool == "pdf-to-text":
            if not inputs:
                raise ValueError("PDF file is required")
            return [pdf_to_text(inputs[0], output_path)]
        if tool == "pdf-to-word-ocr":
            if not inputs:
                raise ValueError("PDF file is required")
            return [pdf_to_docx_ocr(inputs[0], output_path, config.get("lang"))]
        if tool == "pdf-to-excel":
            if not inputs:
                raise ValueError("PDF file is required")
            return [pdf_to_xlsx(inputs[0], output_path)]
        if tool == "pdf-to-excel-ocr":
            if not inputs:
                raise ValueError("PDF file is required")
            return [pdf_to_xlsx_ocr(inputs[0], output_path, config.get("lang"))]
        raise RuntimeError(f"Unsupported tool: {tool}")

    def _upload_outputs(self, outputs: List[Path]) -> List[Dict[str, Any]]:
        """Upload output files to Convex storage."""
        payload = []
        for output in outputs:
            upload_url = self._mutation(
                "files:generateUploadUrl", {"workerToken": self.worker_token}
            )
            with output.open("rb") as handle:
                response = requests.post(
                    upload_url,
                    data=handle,
                    headers={"Content-Type": "application/octet-stream"},
                    timeout=120,
                )
                response.raise_for_status()
                storage_id = response.json()["storageId"]
            payload.append(
                {
                    "storageId": storage_id,
                    "filename": output.name,
                    "sizeBytes": output.stat().st_size,
                }
            )
        return payload

    def _mutation(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a mutation with thread-safe access."""
        with self._client_lock:
            return self.client.mutation(path, args)

    def _query(self, path: str, args: Dict[str, Any]) -> Any:
        """Execute a query with thread-safe access."""
        with self._client_lock:
            return self.client.query(path, args)


def main() -> None:
    """Entrypoint for the worker process."""
    convex_url = os.environ.get("ZENPDF_CONVEX_URL")
    if not convex_url:
        raise RuntimeError("ZENPDF_CONVEX_URL is required")
    worker_id = os.environ.get("ZENPDF_WORKER_ID", "worker-local")
    worker_token = os.environ.get("ZENPDF_WORKER_TOKEN")
    if not worker_token:
        raise RuntimeError("ZENPDF_WORKER_TOKEN is required")
    ZenPdfWorker(convex_url, worker_id, worker_token).run()


if __name__ == "__main__":
    main()
