import os
import threading
import time
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import Any, Dict, List

import requests

from .client import ConvexClient, ConvexError
from .tools import (
    compress_pdf,
    image_to_pdf,
    merge_pdfs,
    pdf_to_jpg,
    remove_pages,
    reorder_pages,
    rotate_pdf,
    split_pdf,
    zip_outputs,
)


class ZenPdfWorker:
    def __init__(self, convex_url: str, worker_id: str, worker_token: str) -> None:
        self.client = ConvexClient(convex_url)
        self.worker_id = worker_id
        self.worker_token = worker_token

    def run(self) -> None:
        poll_interval = float(os.environ.get("ZENPDF_POLL_INTERVAL", "5"))
        while True:
            job = self.client.mutation(
                "jobs:claimNextJob", {"workerId": self.worker_id}
            )
            if not job:
                time.sleep(poll_interval)
                continue
            self._process_job(job)

    def _process_job(self, job: Dict[str, Any]) -> None:
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
            self.client.mutation(
                "jobs:completeJob",
                {
                    "jobId": job_id,
                    "workerId": self.worker_id,
                    "outputs": output_payload,
                    "minutesUsed": elapsed_minutes,
                    "bytesProcessed": bytes_processed,
                },
            )
            self._report(job_id, 100)
        except ConvexError as error:
            self._safe_fail(job_id, error.message)
        except Exception as error:  # noqa: BLE001
            self._safe_fail(job_id, str(error))
        finally:
            stop_event.set()
            heartbeat.join(timeout=1)

    def _report(self, job_id: str, progress: int) -> None:
        self.client.mutation(
            "jobs:reportJobProgress",
            {
                "jobId": job_id,
                "workerId": self.worker_id,
                "progress": progress,
            },
        )

    def _fail(self, job_id: str, message: str) -> None:
        print(f"Job {job_id} failed: {message}")
        self.client.mutation(
            "jobs:failJob",
            {
                "jobId": job_id,
                "workerId": self.worker_id,
                "errorCode": "SERVICE_CAPACITY_TEMPORARY",
                "errorMessage": "Processing failed. Please retry.",
            },
        )

    def _safe_fail(self, job_id: str, message: str) -> None:
        try:
            self._fail(job_id, message)
        except Exception as error:  # noqa: BLE001
            print(f"Failed to report job failure for {job_id}: {error}")

    def _heartbeat(
        self, job_id: str, progress: Dict[str, int], stop_event: threading.Event
    ) -> None:
        interval = float(os.environ.get("ZENPDF_WORKER_HEARTBEAT_SECONDS", "25"))
        while not stop_event.wait(interval):
            self._report(job_id, progress["value"])

    def _download_inputs(self, inputs: List[Dict[str, Any]], temp: Path) -> List[Path]:
        paths: List[Path] = []
        for index, item in enumerate(inputs, start=1):
            url = self.client.query(
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
        tool = job["tool"]
        config = job.get("config")
        if not isinstance(config, dict):
            config = {}
        output_path = temp / "output.pdf"
        if tool == "merge":
            return [merge_pdfs(inputs, output_path)]
        if tool == "split":
            outputs = split_pdf(inputs[0], temp, config.get("ranges"))
            zip_path = temp / "split_output.zip"
            return [zip_outputs(outputs, zip_path)]
        if tool == "compress":
            return [compress_pdf(inputs[0], output_path)]
        if tool == "rotate":
            angle = int(config.get("angle") or 90)
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
        if tool == "image-to-pdf":
            return [image_to_pdf(inputs, output_path)]
        if tool == "pdf-to-jpg":
            images = pdf_to_jpg(inputs[0], temp, int(config.get("dpi") or 150))
            zip_path = temp / "pdf_pages.zip"
            return [zip_outputs(images, zip_path)]
        raise RuntimeError(f"Unsupported tool: {tool}")

    def _upload_outputs(self, outputs: List[Path]) -> List[Dict[str, Any]]:
        payload = []
        for output in outputs:
            upload_url = self.client.mutation("files:generateUploadUrl", {})
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


def main() -> None:
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
