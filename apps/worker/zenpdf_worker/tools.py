"""Conversion utilities for the worker process."""

from __future__ import annotations

import ipaddress
import math
import os
import shlex
import shutil
import socket
import subprocess
import time
import uuid
import zipfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from html.parser import HTMLParser
from io import BytesIO
from pathlib import Path
from typing import Callable, Iterable, List, Sequence, Tuple
from urllib.parse import urlparse

import img2pdf
import fitz
import requests
from docx import Document
from fpdf import FPDF
from openpyxl import Workbook
from PIL import Image
from pypdf import PdfReader, PdfWriter
from pypdf.errors import PdfReadError
from requests_toolbelt.adapters.host_header_ssl import HostHeaderSSLAdapter

try:
    import pytesseract
except ImportError:  # pragma: no cover - optional dependency for OCR tools
    pytesseract = None


def _parse_ranges(value: str, total_pages: int) -> List[Tuple[int, int]]:
    """Parse a comma-separated list of page ranges."""
    ranges: List[Tuple[int, int]] = []
    for part in value.split(","):
        cleaned = part.strip()
        if not cleaned:
            continue
        if "-" in cleaned:
            start, end = cleaned.split("-", 1)
        else:
            start, end = cleaned, cleaned
        try:
            start_i = max(1, int(start))
            end_i = min(total_pages, int(end))
        except ValueError:
            continue
        if start_i <= end_i:
            ranges.append((start_i, end_i))
    return ranges


def _parse_page_list(value: str, total_pages: int) -> List[int]:
    """
    Expand a comma-separated page range string into an ordered list of page numbers.
    
    Parameters:
        value (str): A comma-separated string of page numbers and ranges (e.g. "1,3-5,7").
        total_pages (int): Total number of pages in the document; results are clamped to 1..total_pages.
    
    Returns:
        list[int]: Ordered list of page numbers produced by expanding the ranges in `value`.
                   Each page is within 1 and `total_pages` inclusive; overlapping ranges may produce duplicates.
    """
    pages: List[int] = []
    for start, end in _parse_ranges(value, total_pages):
        pages.extend(range(start, end + 1))
    return [page for page in pages if 1 <= page <= total_pages]


def _load_pdf(input_path: Path, allow_encrypted: bool = False) -> PdfReader:
    """Load a PDF and optionally enforce unencrypted input."""
    try:
        reader = PdfReader(str(input_path))
    except PdfReadError as error:
        raise ValueError("PDF appears to be corrupted or unreadable.") from error
    if reader.is_encrypted and not allow_encrypted:
        raise ValueError("PDF is encrypted")
    return reader


def _resolve_page_selection(pages: str | None, total_pages: int) -> set[int] | None:
    """Return a validated set of target pages or None for all."""
    if pages is None:
        return None
    value = str(pages).strip()
    if not value:
        return None
    selection = _parse_page_list(value, total_pages)
    if not selection:
        raise ValueError("No valid pages selected")
    return set(selection)


def _copy_metadata(writer: PdfWriter, reader: PdfReader) -> None:
    """Copy metadata from a PDF reader into a writer."""
    metadata = reader.metadata or {}
    if metadata:
        writer.add_metadata(metadata)


def _assert_fitz_unencrypted(document: fitz.Document) -> None:
    """Raise if a PyMuPDF document is encrypted."""
    is_encrypted = bool(
        getattr(document, "is_encrypted", False)
        or getattr(document, "isEncrypted", False)
    )
    if is_encrypted:
        raise ValueError("PDF is encrypted")


def _parse_margins(value: str) -> Tuple[float, float, float, float]:
    """
    Parse a comma-separated margin string into top, right, bottom, and left values in points.
    
    The input may be a single numeric value (applied to all four margins) or four comma-separated numeric values in the order top, right, bottom, left. Whitespace around values is ignored.
    
    Parameters:
        value (str): Margin specification as "N" or "T,R,B,L".
    
    Returns:
        tuple[float, float, float, float]: (top, right, bottom, left) in points.
    
    Raises:
        ValueError: If any component is not numeric or if the input does not contain 1 or 4 values.
    """
    parts = [part.strip() for part in value.split(",") if part.strip()]
    try:
        numbers = [float(part) for part in parts]
    except ValueError as error:
        raise ValueError("Margins must be numeric") from error
    if len(numbers) == 1:
        top = right = bottom = left = numbers[0]
    elif len(numbers) == 4:
        top, right, bottom, left = numbers
    else:
        raise ValueError("Margins must have 1 or 4 values")
    return (top, right, bottom, left)


def _rotate_page(page, angle: int) -> None:
    """
    Rotate the given PDF page object by the specified angle in degrees, modifying the page in place.
    
    Parameters:
        page: PDF page object that provides a rotation method (one of `rotate_clockwise`, `rotateClockwise`, or `rotate`).
        angle (int): Rotation angle in degrees clockwise.
    """
    if hasattr(page, "rotate_clockwise"):
        page.rotate_clockwise(angle)
    elif hasattr(page, "rotateClockwise"):
        page.rotateClockwise(angle)
    elif hasattr(page, "rotate"):
        page.rotate(angle)


def _points_to_mm(points: float) -> float:
    """
    Convert a length in PDF points to millimeters.
    
    Returns:
        millimeters (float): The length converted from points to millimeters.
    """
    return points * 25.4 / 72


def _build_overlay_page(
    width_points: float,
    height_points: float,
    draw_fn: Callable[[FPDF, float, float], None],
):
    """
    Create a single-page PDF overlay sized to the given page dimensions and rendered by the provided draw callback.
    
    Parameters:
        width_points (float): Page width in PDF points.
        height_points (float): Page height in PDF points.
        draw_fn (Callable[[FPDF, float, float], None]): Callback that draws onto an FPDF instance; called with the FPDF object and the page width and height in millimeters.
    
    Returns:
        page: A single page object from a PdfReader representing the generated overlay.
    """
    width_mm = _points_to_mm(width_points)
    height_mm = _points_to_mm(height_points)
    orientation = "L" if width_mm > height_mm else "P"
    pdf = FPDF(orientation=orientation, unit="mm", format=(width_mm, height_mm))
    pdf.set_margins(0, 0, 0)
    pdf.set_auto_page_break(auto=False)
    pdf.add_page()
    draw_fn(pdf, width_mm, height_mm)
    pdf_output = pdf.output(dest="S")
    if isinstance(pdf_output, str):
        pdf_bytes = pdf_output.encode("latin-1")
    else:
        pdf_bytes = bytes(pdf_output)
    overlay_reader = PdfReader(BytesIO(pdf_bytes))
    return overlay_reader.pages[0]


def merge_pdfs(inputs: Sequence[Path], output_path: Path) -> Path:
    """
    Merge multiple PDF files into a single PDF.
    
    Parameters:
        inputs (Sequence[Path]): Paths to source PDF files, merged in the given order.
        output_path (Path): Destination path for the merged PDF.
    
    Returns:
        Path: The path to the written merged PDF (same as `output_path`).
    """
    writer = PdfWriter()
    for path in inputs:
        reader = PdfReader(str(path))
        for page in reader.pages:
            writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def split_pdf(
    input_path: Path,
    output_dir: Path,
    ranges: str | None,
) -> List[Path]:
    """Split a PDF into multiple files based on ranges."""
    reader = PdfReader(str(input_path))
    total_pages = len(reader.pages)
    output_files: List[Path] = []

    if ranges:
        page_ranges = _parse_ranges(ranges, total_pages)
        if not page_ranges:
            raise ValueError("No valid page ranges provided")
    else:
        page_ranges = [(index, index) for index in range(1, total_pages + 1)]

    for index, (start, end) in enumerate(page_ranges, start=1):
        writer = PdfWriter()
        for page_number in range(start - 1, end):
            writer.add_page(reader.pages[page_number])
        output_path = output_dir / f"split_{index}.pdf"
        with output_path.open("wb") as handle:
            writer.write(handle)
        output_files.append(output_path)
    return output_files


def compress_pdf(input_path: Path, output_path: Path) -> tuple[Path, dict]:
    """Compress a PDF using a staged pipeline with early image-heavy handling."""
    size_bytes = input_path.stat().st_size
    size_mb = max(1, math.ceil(size_bytes / (1024 * 1024)))
    warnings: list[str] = []
    steps: list[dict] = []
    candidates: list[dict] = []
    run_id = uuid.uuid4().hex[:8]

    def _env_int(name: str, default: int) -> int:
        value = os.environ.get(name)
        if value is None or value == "":
            return default
        try:
            return int(value)
        except ValueError:
            return default

    def _env_float(name: str, default: float) -> float:
        value = os.environ.get(name)
        if value is None or value == "":
            return default
        try:
            return float(value)
        except ValueError:
            return default

    def _env_bool(name: str, default: bool = False) -> bool:
        value = os.environ.get(name)
        if value is None:
            return default
        return value.strip().lower() in {"1", "true", "yes", "on", "y"}

    def _env_str(name: str, default: str) -> str:
        value = os.environ.get(name)
        return value.strip() if value else default

    def _run_cmd(cmd: list[str], timeout_s: int, env: dict | None = None) -> dict:
        start = time.perf_counter()
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=False,
                timeout=timeout_s,
                env=env,
            )
            elapsed_ms = int((time.perf_counter() - start) * 1000)
            return {
                "ok": result.returncode == 0,
                "returncode": result.returncode,
                "stdout": (result.stdout or "").strip(),
                "stderr": (result.stderr or "").strip(),
                "timeout": False,
                "ms": elapsed_ms,
            }
        except subprocess.TimeoutExpired:
            elapsed_ms = int((time.perf_counter() - start) * 1000)
            return {
                "ok": False,
                "returncode": None,
                "stdout": "",
                "stderr": "",
                "timeout": True,
                "ms": elapsed_ms,
            }

    def _truncate(value: str, limit: int = 300) -> str:
        if len(value) <= limit:
            return value
        return f"{value[:limit]}..."

    def _make_step_entry(name: str, result: dict | None, notes: str | None = None) -> dict:
        if result is None:
            return {"name": name, "ok": False, "ms": 0, "notes": notes or ""}
        entry = {"name": name, "ok": bool(result.get("ok")), "ms": int(result.get("ms", 0))}
        if notes:
            entry["notes"] = _truncate(notes)
        elif result.get("timeout"):
            entry["notes"] = "timeout"
        elif not result.get("ok") and result.get("stderr"):
            entry["notes"] = _truncate((result.get("stderr") or "").strip())
        return entry

    def _record_step(name: str, result: dict | None, notes: str | None = None) -> None:
        steps.append(_make_step_entry(name, result, notes))

    def _safe_page_count(path: Path) -> int:
        try:
            reader = PdfReader(str(path))
            if reader.is_encrypted:
                raise ValueError("PDF is encrypted")
            return max(1, len(reader.pages))
        except ValueError:
            raise
        except (PdfReadError, OSError, EOFError) as error:
            warnings.append(f"Could not count pages: {error}")
        except Exception:
            pass
        try:
            with fitz.open(str(path)) as document:
                return max(1, document.page_count)
        except Exception:
            return 1

    def _validate_candidate(path: Path, expected_pages: int) -> bool:
        if not path.exists() or path.stat().st_size == 0:
            return False
        if shutil.which("qpdf"):
            result = _run_cmd(["qpdf", "--check", str(path)], timeout_s=20)
            if not result["ok"]:
                return False
        try:
            with fitz.open(str(path)) as document:
                if document.page_count != expected_pages:
                    return False
                if document.page_count > 0:
                    page = document.load_page(0)
                    _ = page.get_pixmap(matrix=fitz.Matrix(0.5, 0.5))
        except Exception:
            return False
        return True

    def _add_candidate(path: Path, method: str, label: str, expected_pages: int) -> bool:
        if not _validate_candidate(path, expected_pages):
            warnings.append(f"{label} output invalid")
            return False
        candidates.append(
            {
                "path": path,
                "method": method,
                "label": label,
                "size": path.stat().st_size,
            }
        )
        return True

    def _rewrite_pdf(source: Path, target: Path) -> bool:
        try:
            reader = PdfReader(str(source))
            if reader.is_encrypted:
                raise ValueError("PDF is encrypted")
            writer = PdfWriter()
            for page in reader.pages:
                writer.add_page(page)
            compress = getattr(writer, "compress_content_streams", None)
            if callable(compress):
                compress()
            raw_metadata = reader.metadata or {}
            safe_metadata = {}
            for key, value in raw_metadata.items():
                if value is None:
                    continue
                safe_metadata[str(key)] = str(value)
            if safe_metadata:
                writer.add_metadata(safe_metadata)
            with target.open("wb") as handle:
                writer.write(handle)
            return True
        except Exception as error:
            warnings.append(f"pypdf rewrite failed: {error}")
            return False

    def _detect_image_heavy(path: Path) -> dict:
        metrics = {
            "pages": 0,
            "sampled_pages": 0,
            "image_count": 0,
            "text_chars": 0,
            "image_heavy": False,
        }
        try:
            with fitz.open(str(path)) as document:
                pages = document.page_count
                metrics["pages"] = pages
                if pages == 0:
                    return metrics
                sample_pages = min(pages, 10)
                step = max(1, pages // sample_pages)
                sampled = 0
                image_count = 0
                text_chars = 0
                for index in range(0, pages, step):
                    if sampled >= sample_pages:
                        break
                    page = document.load_page(index)
                    image_count += len(page.get_images(full=True))
                    text_chars += len(page.get_text("text") or "")
                    sampled += 1
                metrics["sampled_pages"] = sampled
                if sampled > 0:
                    images_per_page = image_count / sampled
                    text_per_page = text_chars / sampled
                else:
                    images_per_page = 0
                    text_per_page = 0
                metrics["image_count"] = int(round(images_per_page * pages))
                metrics["text_chars"] = int(round(text_per_page * pages))
                metrics["images_per_page"] = images_per_page
                metrics["text_chars_per_page"] = text_per_page
                image_heavy = metrics["image_count"] >= pages * 1.0 or (
                    (text_per_page < 500) and (images_per_page > 0.5)
                )
                metrics["image_heavy"] = image_heavy
        except Exception as error:
            warnings.append(f"image-heavy detector failed: {error}")
        return metrics

    def _tmp_path(name: str) -> Path:
        return output_path.parent / f"{input_path.stem}_{run_id}_{name}"

    try:
        _load_pdf(input_path, allow_encrypted=False)
    except ValueError as error:
        if str(error) == "PDF is encrypted":
            raise
        warnings.append(f"preflight read failed: {error}")
    except Exception as error:  # noqa: BLE001
        warnings.append(f"preflight read failed: {error}")

    timeout_override = _env_int("ZENPDF_COMPRESS_TIMEOUT_SECONDS", 0)
    if timeout_override > 0:
        timeout_seconds = timeout_override
    else:
        base_timeout = _env_int("ZENPDF_COMPRESS_TIMEOUT_BASE_SECONDS", 120)
        per_mb_timeout = _env_int("ZENPDF_COMPRESS_TIMEOUT_PER_MB_SECONDS", 3)
        per_page_timeout = _env_float("ZENPDF_COMPRESS_TIMEOUT_PER_PAGE_SECONDS", 1.5)
        max_timeout = _env_int("ZENPDF_COMPRESS_TIMEOUT_MAX_SECONDS", 900)
        pages_for_timeout = _safe_page_count(input_path)
        timeout_seconds = min(
            max_timeout,
            int(base_timeout + (size_mb * per_mb_timeout) + (pages_for_timeout * per_page_timeout)),
        )

    probe_pages = max(1, min(_safe_page_count(input_path), _env_int("ZENPDF_COMPRESS_TIMEOUT_PROBE_PAGES", 5)))
    probe_timeout = min(
        _env_int("ZENPDF_COMPRESS_TIMEOUT_PROBE_MAX_SECONDS", 30),
        max(10, int(timeout_seconds * 0.25)),
    )

    profile = _env_str("ZENPDF_COMPRESS_PROFILE", "balanced").lower()
    if profile not in {"light", "balanced", "strong"}:
        profile = "balanced"

    auto_image_heavy = _env_bool("ZENPDF_COMPRESS_AUTO_IMAGE_HEAVY", True)
    pass_through_env = _env_bool("ZENPDF_COMPRESS_GS_PASSTHROUGH_JPEG", False)
    use_zopfli = _env_bool("ZENPDF_COMPRESS_USE_ZOPFLI", False)
    enable_image_opt = _env_bool("ZENPDF_COMPRESS_ENABLE_IMAGE_OPT", False)
    enable_pdfsizeopt = _env_bool("ZENPDF_COMPRESS_ENABLE_PDFSIZEOPT", False)
    enable_jbig2 = _env_bool("ZENPDF_COMPRESS_ENABLE_JBIG2", False)
    qpdf_keep_inline = _env_bool("ZENPDF_QPDF_OI_KEEP_INLINE_IMAGES", False)
    gs_min_size_mb = _env_int("ZENPDF_COMPRESS_GS_MIN_SIZE_MB", 5)
    gs_preset = _env_str("ZENPDF_COMPRESS_GS_PRESET", "").lower()
    gs_extra_flags = _env_bool("ZENPDF_COMPRESS_GS_EXTRA_FLAGS", False)
    mutool_object_streams = _env_bool("ZENPDF_MUTOOL_OBJECT_STREAMS", False)
    parallelism = max(1, _env_int("ZENPDF_COMPRESS_PARALLELISM", 1))
    qpdf_quality = _env_int("ZENPDF_QPDF_OI_QUALITY", 75)
    qpdf_min_width = _env_int("ZENPDF_QPDF_OI_MIN_WIDTH", 128)
    qpdf_min_height = _env_int("ZENPDF_QPDF_OI_MIN_HEIGHT", 128)
    qpdf_min_area = _env_int("ZENPDF_QPDF_OI_MIN_AREA", 16384)
    pdfsizeopt_args = shlex.split(_env_str("ZENPDF_COMPRESS_PDFSIZEOPT_ARGS", ""))

    savings_threshold_env = os.environ.get("ZENPDF_COMPRESS_SAVINGS_THRESHOLD_PCT")
    if savings_threshold_env:
        try:
            savings_threshold = float(savings_threshold_env)
        except ValueError:
            savings_threshold = 0.08
    else:
        legacy_percent = os.environ.get("ZENPDF_COMPRESS_MIN_SAVINGS_PERCENT")
        if legacy_percent:
            try:
                savings_threshold = float(legacy_percent) / 100.0
            except ValueError:
                savings_threshold = 0.08
        else:
            savings_threshold = 0.08
    min_savings_bytes = _env_int("ZENPDF_COMPRESS_MIN_SAVINGS_BYTES", 200000)

    expected_pages = _safe_page_count(input_path)
    image_metrics = _detect_image_heavy(input_path)
    image_heavy = auto_image_heavy and image_metrics.get("image_heavy", False)

    mutool = shutil.which("mutool")
    qpdf = shutil.which("qpdf")
    ghostscript = shutil.which("gs")
    pdfsizeopt = shutil.which("pdfsizeopt")
    jbig2 = shutil.which("jbig2")

    if _validate_candidate(input_path, expected_pages):
        _add_candidate(input_path, "original", "original", expected_pages)

    normalized_path = input_path
    base_path = input_path
    gs_input_path = input_path
    temp_paths: list[Path] = []

    if mutool:
        normalized_path = _tmp_path("normalized.pdf")
        cmd = ["mutool", "clean", "-gggg", "-z", "-i", "-f", "-t"]
        if mutool_object_streams:
            cmd.append("-Z")
        cmd.extend([str(input_path), str(normalized_path)])
        result = _run_cmd(cmd, timeout_seconds)
        _record_step("normalize_mutool", result)
        temp_paths.append(normalized_path)
        if result["ok"]:
            _add_candidate(normalized_path, "mutool", "normalize", expected_pages)
            base_path = normalized_path
            gs_input_path = normalized_path
    else:
        _record_step("normalize_mutool", None, "skipped: mutool not available")

    if normalized_path == input_path:
        if qpdf:
            qpdf_norm = _tmp_path("qpdf_norm.pdf")
            cmd = [
                "qpdf",
                "--warning-exit-0",
                "--object-streams=generate",
                "--compress-streams=y",
                "--recompress-flate",
                str(input_path),
                str(qpdf_norm),
            ]
            result = _run_cmd(cmd, timeout_seconds)
            _record_step("normalize_qpdf", result)
            temp_paths.append(qpdf_norm)
            if result["ok"]:
                normalized_path = qpdf_norm
                _add_candidate(qpdf_norm, "qpdf", "normalize", expected_pages)
                base_path = qpdf_norm
                gs_input_path = qpdf_norm
        else:
            _record_step("normalize_qpdf", None, "skipped: qpdf not available")

    if normalized_path == input_path:
        rewrite_path = _tmp_path("rewrite.pdf")
        start = time.perf_counter()
        ok = _rewrite_pdf(input_path, rewrite_path)
        elapsed_ms = int((time.perf_counter() - start) * 1000)
        result = {"ok": ok, "ms": elapsed_ms, "stderr": "" if ok else "rewrite failed"}
        _record_step("normalize_pypdf", result)
        temp_paths.append(rewrite_path)
        if ok:
            normalized_path = rewrite_path
            _add_candidate(rewrite_path, "pypdf", "normalize", expected_pages)
            base_path = rewrite_path
            gs_input_path = rewrite_path

    if qpdf:
        optimized_path = _tmp_path("optimized.pdf")
        cmd = [
            "qpdf",
            "--warning-exit-0",
            "--object-streams=generate",
            "--compress-streams=y",
            "--recompress-flate",
            str(base_path),
            str(optimized_path),
        ]
        result = _run_cmd(cmd, timeout_seconds)
        _record_step("optimize_qpdf", result)
        temp_paths.append(optimized_path)
        if result["ok"]:
            _add_candidate(optimized_path, "qpdf", "optimize", expected_pages)
            base_path = optimized_path
            gs_input_path = optimized_path
    else:
        _record_step("optimize_qpdf", None, "skipped: qpdf not available")

    if mutool:
        mutool_opt_path = _tmp_path("mutool_opt.pdf")
        cmd = [
            "mutool",
            "merge",
            "-o",
            str(mutool_opt_path),
            "-O",
            "compress",
            str(base_path),
        ]
        result = _run_cmd(cmd, timeout_seconds)
        _record_step("optimize_mutool", result)
        temp_paths.append(mutool_opt_path)
        if result["ok"]:
            _add_candidate(mutool_opt_path, "mutool", "mutool_opt", expected_pages)
    else:
        _record_step("optimize_mutool", None, "skipped: mutool not available")

    def _ghostscript_settings(override_preset: str | None = None) -> tuple[str, str, str]:
        preset = override_preset or ("screen" if profile == "strong" else "ebook")
        if preset == "screen":
            return "/screen", "100", "100"
        return "/ebook", "150", "150"

    def _run_gs_basic(source: Path, dest: Path, label: str, preset: str | None = None) -> dict:
        if not ghostscript:
            return {"ok": False, "stderr": "ghostscript missing", "timeout": False, "ms": 0}
        pdfsettings, color_res, gray_res = _ghostscript_settings(preset)
        pass_through = pass_through_env or profile == "light"
        cmd = [
            ghostscript,
            "-dSAFER",
            "-dBATCH",
            "-dNOPAUSE",
            "-sDEVICE=pdfwrite",
            "-dCompatibilityLevel=1.5",
            f"-dPDFSETTINGS={pdfsettings}",
            f"-dPassThroughJPEGImages={'true' if pass_through else 'false'}",
            "-dDownsampleColorImages=true",
            f"-dColorImageResolution={color_res}",
            "-dColorImageDownsampleType=/Bicubic",
            "-dDownsampleGrayImages=true",
            f"-dGrayImageResolution={gray_res}",
            "-dGrayImageDownsampleType=/Bicubic",
            "-dDownsampleMonoImages=true",
            "-dMonoImageResolution=300",
            "-dAutoFilterColorImages=false",
            "-dColorImageFilter=/DCTEncode",
            "-dAutoFilterGrayImages=false",
            "-dGrayImageFilter=/DCTEncode",
        ]
        if gs_extra_flags:
            cmd.append("-dDetectDuplicateImages=true")
        cmd.extend([f"-sOutputFile={dest}", str(source)])
        result = _run_cmd(cmd, timeout_seconds)
        _record_step(label, result)
        return result

    if image_heavy and ghostscript and size_mb >= gs_min_size_mb:
        gs_output = _tmp_path("gs_early.pdf")
        temp_paths.append(gs_output)
        preset_override = "screen" if profile == "strong" else "ebook"
        result = _run_gs_basic(normalized_path, gs_output, "ghostscript_early", preset_override)
        if result["ok"]:
            _add_candidate(gs_output, "ghostscript", "ghostscript_early", expected_pages)
            if qpdf:
                qpdf_after_gs = _tmp_path("gs_qpdf.pdf")
                temp_paths.append(qpdf_after_gs)
                result = _run_cmd(
                    [
                        "qpdf",
                        "--object-streams=generate",
                        "--compress-streams=y",
                        "--recompress-flate",
                        "--compression-level=9",
                        "--deterministic-id",
                        str(gs_output),
                        str(qpdf_after_gs),
                    ],
                    timeout_seconds,
                )
                _record_step("qpdf_after_gs", result)
                if result["ok"]:
                    _add_candidate(qpdf_after_gs, "qpdf_after_gs", "qpdf_after_gs", expected_pages)

    def _current_best_savings() -> tuple[int, float]:
        if not candidates or size_bytes == 0:
            return 0, 0.0
        best = min(candidates, key=lambda item: item["size"])
        savings = max(size_bytes - best["size"], 0)
        return savings, savings / size_bytes

    def _should_run_heavy_steps() -> bool:
        savings_bytes, savings_pct = _current_best_savings()
        return savings_bytes < min_savings_bytes or savings_pct < savings_threshold

    def _empty_outcome() -> dict:
        return {"steps": [], "candidates": [], "temp_paths": [], "warnings": []}

    def _run_image_opt_task() -> dict:
        outcome = _empty_outcome()
        if not enable_image_opt:
            return outcome
        if not qpdf:
            outcome["steps"].append(_make_step_entry("optimize_images_qpdf", None, "skipped: qpdf not available"))
            return outcome
        image_opt_path = _tmp_path("image_opt.pdf")
        outcome["temp_paths"].append(image_opt_path)
        cmd = [
            "qpdf",
            "--warning-exit-0",
            "--optimize-images",
            f"--oi-quality={qpdf_quality}",
            f"--oi-min-width={qpdf_min_width}",
            f"--oi-min-height={qpdf_min_height}",
            f"--oi-min-area={qpdf_min_area}",
            "--object-streams=generate",
            "--compress-streams=y",
            "--recompress-flate",
            "--compression-level=9",
            "--deterministic-id",
            str(base_path),
            str(image_opt_path),
        ]
        if qpdf_keep_inline:
            cmd.insert(3, "--keep-inline-images")
        result = _run_cmd(cmd, timeout_seconds)
        outcome["steps"].append(_make_step_entry("optimize_images_qpdf", result))
        if result["ok"]:
            outcome["candidates"].append((image_opt_path, "qpdf_optimize_images", "image_opt"))
        return outcome

    def _run_pdfsizeopt_task(should_run: bool) -> dict:
        outcome = _empty_outcome()
        if not (enable_pdfsizeopt or enable_jbig2):
            return outcome
        if enable_jbig2 and not jbig2:
            outcome["steps"].append(_make_step_entry("optimize_pdfsizeopt", None, "skipped: jbig2enc not available"))
            return outcome
        if not pdfsizeopt:
            outcome["steps"].append(_make_step_entry("optimize_pdfsizeopt", None, "skipped: pdfsizeopt not available"))
            return outcome
        if not should_run:
            outcome["steps"].append(_make_step_entry("optimize_pdfsizeopt", None, "skipped: already reduced"))
            return outcome
        pdfsizeopt_path = _tmp_path("pdfsizeopt.pdf")
        outcome["temp_paths"].append(pdfsizeopt_path)
        cmd = [pdfsizeopt]
        if enable_jbig2:
            cmd.append("--use-image-optimizer=jbig2")
        if pdfsizeopt_args:
            cmd.extend(pdfsizeopt_args)
        cmd.extend([str(base_path), str(pdfsizeopt_path)])
        result = _run_cmd(cmd, timeout_seconds)
        outcome["steps"].append(_make_step_entry("optimize_pdfsizeopt", result))
        if result["ok"]:
            method = "pdfsizeopt_jbig2" if enable_jbig2 else "pdfsizeopt"
            outcome["candidates"].append((pdfsizeopt_path, method, "pdfsizeopt"))
        return outcome

    def _run_ghostscript_task(should_run: bool) -> dict:
        outcome = _empty_outcome()
        if image_heavy:
            outcome["steps"].append(_make_step_entry("ghostscript_full", None, "skipped: image-heavy handled"))
            return outcome
        if not ghostscript:
            outcome["steps"].append(_make_step_entry("ghostscript_full", None, "skipped: ghostscript not available"))
            return outcome
        if size_mb < gs_min_size_mb:
            outcome["steps"].append(_make_step_entry("ghostscript_full", None, "skipped: below size threshold"))
            return outcome
        if not should_run:
            outcome["steps"].append(_make_step_entry("ghostscript_full", None, "skipped: already reduced"))
            return outcome

        preset_override = None
        if gs_preset in {"screen", "ebook"}:
            preset_override = gs_preset

        pdfsettings, color_res, gray_res = _ghostscript_settings(preset_override)
        pass_through = pass_through_env or profile == "light"
        gs_flags = [
            "-dSAFER",
            "-dBATCH",
            "-dNOPAUSE",
            "-sDEVICE=pdfwrite",
            "-dCompatibilityLevel=1.5",
            f"-dPDFSETTINGS={pdfsettings}",
            f"-dPassThroughJPEGImages={'true' if pass_through else 'false'}",
            "-dDownsampleColorImages=true",
            f"-dColorImageResolution={color_res}",
            "-dColorImageDownsampleType=/Bicubic",
            "-dDownsampleGrayImages=true",
            f"-dGrayImageResolution={gray_res}",
            "-dGrayImageDownsampleType=/Bicubic",
            "-dDownsampleMonoImages=true",
            "-dMonoImageResolution=300",
            "-dAutoFilterColorImages=false",
            "-dColorImageFilter=/DCTEncode",
            "-dAutoFilterGrayImages=false",
            "-dGrayImageFilter=/DCTEncode",
        ]
        if gs_extra_flags:
            gs_flags.append("-dDetectDuplicateImages=true")

        def _gs_cmd(
            source: Path,
            target: Path,
            first_page: int | None = None,
            last_page: int | None = None,
            newpdf: bool | None = None,
        ) -> list[str]:
            cmd = [ghostscript, *gs_flags]
            if first_page is not None and last_page is not None:
                cmd.extend([f"-dFirstPage={first_page}", f"-dLastPage={last_page}"])
            if newpdf is False:
                cmd.append("-dNEWPDF=false")
            cmd.append(f"-sOutputFile={target}")
            cmd.append(str(source))
            return cmd

        probe_output = _tmp_path("gs_probe.pdf")
        gs_output = _tmp_path("gs.pdf")
        outcome["temp_paths"].extend([probe_output, gs_output])

        probe_result = _run_cmd(
            _gs_cmd(gs_input_path, probe_output, 1, probe_pages),
            probe_timeout,
        )
        probe_ok = probe_result["ok"]
        probe_notes = None
        if probe_ok and probe_pages > 0:
            estimated_ms = int((probe_result["ms"] / probe_pages) * expected_pages)
            if estimated_ms > timeout_seconds * 1000:
                probe_ok = False
                probe_notes = "probe too slow, skipping full run"
        outcome["steps"].append(_make_step_entry("ghostscript_probe", probe_result, probe_notes))
        if probe_ok:
            full_result = _run_cmd(_gs_cmd(gs_input_path, gs_output), timeout_seconds)
            outcome["steps"].append(_make_step_entry("ghostscript_full", full_result))
            if full_result["ok"]:
                outcome["candidates"].append((gs_output, "ghostscript", "ghostscript"))
            else:
                retry_result = _run_cmd(_gs_cmd(gs_input_path, gs_output, newpdf=False), timeout_seconds)
                outcome["steps"].append(_make_step_entry("ghostscript_retry", retry_result))
                if retry_result["ok"]:
                    outcome["candidates"].append((gs_output, "ghostscript", "ghostscript_retry"))
        else:
            outcome["steps"].append(_make_step_entry("ghostscript_full", None, "skipped: probe failed"))
        return outcome

    should_run_heavy = _should_run_heavy_steps()

    task_specs: list[tuple[str, Callable[[], dict]]] = []
    if enable_image_opt:
        task_specs.append(("image_opt", _run_image_opt_task))
    if enable_pdfsizeopt or enable_jbig2:
        task_specs.append(("pdfsizeopt", lambda: _run_pdfsizeopt_task(should_run_heavy)))
    if ghostscript:
        task_specs.append(("ghostscript", lambda: _run_ghostscript_task(should_run_heavy)))

    task_results: dict[str, dict] = {}
    if parallelism > 1 and len(task_specs) > 1:
        max_workers = min(parallelism, len(task_specs))
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            future_map = {executor.submit(task): name for name, task in task_specs}
            for future in as_completed(future_map):
                name = future_map[future]
                try:
                    task_results[name] = future.result()
                except Exception as error:  # noqa: BLE001
                    task_results[name] = {
                        "steps": [
                            _make_step_entry(
                                name, {"ok": False, "ms": 0, "stderr": str(error)}
                            )
                        ],
                        "candidates": [],
                        "temp_paths": [],
                        "warnings": [f"{name} failed: {error}"],
                    }
    else:
        for name, task in task_specs:
            task_results[name] = task()

    for name, _ in task_specs:
        outcome = task_results.get(name, _empty_outcome())
        steps.extend(outcome.get("steps", []))
        for warning in outcome.get("warnings", []):
            warnings.append(warning)
        for path, method, label in outcome.get("candidates", []):
            _add_candidate(path, method, label, expected_pages)
        temp_paths.extend(outcome.get("temp_paths", []))

    if not candidates:
        raise ValueError(
            "Could not compress this PDF due to malformed structure; try Repair PDF first."
        )

    best = min(candidates, key=lambda item: item["size"])
    method = best["method"]
    if method == "original":
        method = "passthrough"
        warnings.append("No smaller output found; preserving original content.")

    savings_bytes = max(size_bytes - best["size"], 0)
    savings_pct = (savings_bytes / size_bytes) if size_bytes else 0.0

    status = "success"
    if savings_bytes < min_savings_bytes or savings_pct < savings_threshold:
        status = "no_change"
        best = next((c for c in candidates if c["path"] == input_path), best)

    final_path = best["path"]
    if final_path != output_path:
        if final_path == input_path:
            shutil.copy2(final_path, output_path)
        else:
            if output_path.exists():
                output_path.unlink()
            final_path.replace(output_path)
        final_path = output_path

    if qpdf:
        deterministic_out = _tmp_path("deterministic.pdf")
        result = _run_cmd(
            [
                "qpdf",
                "--object-streams=generate",
                "--compress-streams=y",
                "--recompress-flate",
                "--compression-level=9",
                "--deterministic-id",
                str(final_path),
                str(deterministic_out),
            ],
            min(timeout_seconds, 120),
        )
        _record_step("deterministic_id", result)
        temp_paths.append(deterministic_out)
        if result["ok"] and _validate_candidate(deterministic_out, expected_pages):
            if output_path.exists():
                output_path.unlink()
            deterministic_out.replace(output_path)
            final_path = output_path

    if use_zopfli and qpdf:
        zopfli_out = _tmp_path("zopfli.pdf")
        env = os.environ.copy()
        env["QPDF_ZOPFLI"] = "enabled"
        result = _run_cmd(
            [
                "qpdf",
                "--object-streams=generate",
                "--compress-streams=y",
                "--recompress-flate",
                "--compression-level=9",
                "--deterministic-id",
                str(final_path),
                str(zopfli_out),
            ],
            timeout_seconds,
            env=env,
        )
        _record_step("qpdf_zopfli", result)
        temp_paths.append(zopfli_out)
        if result["ok"] and _validate_candidate(zopfli_out, expected_pages):
            zopfli_size = zopfli_out.stat().st_size
            zopfli_savings = max(size_bytes - zopfli_size, 0)
            if zopfli_savings >= min_savings_bytes and (
                zopfli_savings / size_bytes
            ) >= savings_threshold:
                if output_path.exists():
                    output_path.unlink()
                zopfli_out.replace(output_path)
                final_path = output_path

    for path in temp_paths:
        if path in {input_path, output_path}:
            continue
        try:
            if path.exists():
                path.unlink()
        except OSError:
            pass

    output_bytes = final_path.stat().st_size
    savings_bytes = max(size_bytes - output_bytes, 0)
    savings_percent = round((savings_bytes / size_bytes) * 100, 2) if size_bytes else 0.0

    result_payload = {
        "status": status,
        "method": method,
        "profile": profile,
        "original_bytes": size_bytes,
        "output_bytes": output_bytes,
        "savings_bytes": savings_bytes,
        "savings_percent": savings_percent,
        "steps": steps,
        "warnings": warnings,
        "image_metrics": image_metrics,
    }

    print(
        "Compression metrics:",
        {
            "profile": profile,
            "image_heavy": image_metrics.get("image_heavy"),
            "images": image_metrics.get("image_count"),
            "text_chars": image_metrics.get("text_chars"),
            "savings_pct": savings_percent,
        },
    )

    return final_path, result_payload

def repair_pdf(input_path: Path, output_path: Path) -> Path:
    """Rewrite a PDF to rebuild its internal structure."""
    reader = PdfReader(str(input_path))
    if reader.is_encrypted:
        raise ValueError("PDF is encrypted")
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    writer.add_metadata(reader.metadata or {})
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def rotate_pdf(
    input_path: Path,
    output_path: Path,
    angle: int,
    pages: str | None,
) -> Path:
    """Rotate selected pages by the provided angle."""
    reader = _load_pdf(input_path)
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = _resolve_page_selection(pages, total_pages)
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            _rotate_page(page, angle)
        writer.add_page(page)
    _copy_metadata(writer, reader)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def remove_pages(
    input_path: Path,
    output_path: Path,
    pages: str,
) -> Path:
    """Remove pages listed in the range string."""
    reader = PdfReader(str(input_path))
    total_pages = len(reader.pages)
    remove_set = set(_parse_page_list(pages, total_pages))
    writer = PdfWriter()
    for index, page in enumerate(reader.pages, start=1):
        if index not in remove_set:
            writer.add_page(page)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def reorder_pages(
    input_path: Path,
    output_path: Path,
    order: str,
) -> Path:
    """
    Reorder pages of a PDF file according to a page-order specification.
    
    The `order` string specifies individual pages and ranges (for example, "1,3-5"); page numbers are 1-indexed. If the parsed order is empty, the original page order is preserved. The reordered PDF is written to `output_path`.
    
    Parameters:
        order (str): A comma-separated page list and ranges defining the desired page order.
    
    Returns:
        Path: The path to the written output PDF.
    """
    reader = PdfReader(str(input_path))
    total_pages = len(reader.pages)
    order_list = _parse_page_list(order, total_pages) or list(
        range(1, total_pages + 1)
    )
    writer = PdfWriter()
    for page_index in order_list:
        writer.add_page(reader.pages[page_index - 1])
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def watermark_pdf(
    input_path: Path,
    output_path: Path,
    text: str,
    pages: str | None,
) -> Path:
    """
    Apply a centered text watermark to selected pages of a PDF.
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the watermarked PDF will be written.
        text (str): Watermark text to place centered on each target page.
        pages (str | None): Page selection expressed as a range string (e.g. "1-3,5"); pages are 1-based.
            If None, the watermark is applied to every page.
    
    Returns:
        Path: The same as `output_path` after the watermarked PDF has been written.
    """
    reader = _load_pdf(input_path)
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = _resolve_page_selection(pages, total_pages)
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            width = float(page.mediabox.width)
            height = float(page.mediabox.height)

            def _draw(pdf: FPDF, width_mm: float, height_mm: float) -> None:
                """
                Render centered overlay text onto the provided PDF page area.
                
                Calculates a font size from the smaller of the page width and height (clamped to the range 18-48), configures a unicode-capable font if required, sets a medium-gray text color, and writes the overlay text centered horizontally at the vertical midpoint of the page.
                
                Parameters:
                    pdf (FPDF): The FPDF instance representing the overlay page to draw on.
                    width_mm (float): Page width in millimeters.
                    height_mm (float): Page height in millimeters.
                """
                font_size = min(max(int(min(width_mm, height_mm) * 0.12), 18), 48)
                _set_overlay_font(pdf, text, font_size)
                pdf.set_text_color(160, 160, 160)
                pdf.set_xy(0, height_mm / 2)
                pdf.cell(width_mm, 10, text, align="C")

            overlay = _build_overlay_page(width, height, _draw)
            page.merge_page(overlay)
        writer.add_page(page)
    _copy_metadata(writer, reader)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def page_numbers_pdf(
    input_path: Path,
    output_path: Path,
    start: int,
    pages: str | None,
) -> Path:
    """
    Add sequential page numbers as footers to selected pages of a PDF.
    
    Parameters:
        input_path (Path): Path to the source PDF file.
        output_path (Path): Path where the resulting PDF will be written.
        start (int): Starting page number to apply to the first page (incremented per page).
        pages (str | None): Optional page selection string (e.g., "1-3,5") determining which pages receive numbers; if None, all pages are numbered.
    
    Returns:
        Path: The path to the written PDF file (same as output_path).
    """
    reader = _load_pdf(input_path)
    writer = PdfWriter()
    total_pages = len(reader.pages)
    target_pages = _resolve_page_selection(pages, total_pages)
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            width = float(page.mediabox.width)
            height = float(page.mediabox.height)
            number = start + index - 1

            def _draw(pdf: FPDF, width_mm: float, height_mm: float) -> None:
                """
                Draws a right-aligned numeric footer near the bottom edge of the overlay page.
                
                Positions and renders the page number (captured from the surrounding scope) as a footer using a font size chosen to fit the page: the size is proportional to the smaller page dimension and clamped to the range 8-16 points. The rendered text is right-aligned with a 10 mm right/bottom margin and uses a muted gray color.
                
                Parameters:
                    pdf (FPDF): The FPDF instance used to draw on the overlay page.
                    width_mm (float): Page width in millimeters.
                    height_mm (float): Page height in millimeters.
                
                Returns:
                    None
                """
                font_size = min(max(int(min(width_mm, height_mm) * 0.04), 8), 16)
                _set_overlay_font(pdf, str(number), font_size)
                pdf.set_text_color(60, 60, 60)
                margin = 10
                pdf.set_xy(0, height_mm - margin)
                pdf.cell(width_mm - margin, 6, str(number), align="R")

            overlay = _build_overlay_page(width, height, _draw)
            page.merge_page(overlay)
        writer.add_page(page)
    _copy_metadata(writer, reader)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def crop_pdf(
    input_path: Path,
    output_path: Path,
    margins: str,
    pages: str | None,
) -> Path:
    """
    Crop selected pages of a PDF by the specified margins (measured in PDF points).
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the cropped PDF will be written.
        margins (str): Margin specification in points; either a single numeric value applied to all sides or four comma-separated values in the order top,right,bottom,left.
        pages (str | None): Optional page selection string (e.g., "1-3,5") specifying which pages to crop; when None, all pages are processed.
    
    Returns:
        Path: The path to the written cropped PDF.
    
    Raises:
        ValueError: If the provided margins would remove an entire page or if margin parsing fails.
    """
    reader = _load_pdf(input_path)
    writer = PdfWriter()
    total_pages = len(reader.pages)
    top, right, bottom, left = _parse_margins(margins)
    if any(value < 0 for value in (top, right, bottom, left)):
        raise ValueError("Margins must be zero or positive")
    target_pages = _resolve_page_selection(pages, total_pages)
    for index, page in enumerate(reader.pages, start=1):
        if target_pages is None or index in target_pages:
            lower_left_x = float(page.mediabox.lower_left[0]) + left
            lower_left_y = float(page.mediabox.lower_left[1]) + bottom
            upper_right_x = float(page.mediabox.upper_right[0]) - right
            upper_right_y = float(page.mediabox.upper_right[1]) - top
            if upper_right_x <= lower_left_x or upper_right_y <= lower_left_y:
                raise ValueError("Crop margins remove the entire page")
            page.mediabox.lower_left = (lower_left_x, lower_left_y)
            page.mediabox.upper_right = (upper_right_x, upper_right_y)
            page.cropbox.lower_left = (lower_left_x, lower_left_y)
            page.cropbox.upper_right = (upper_right_x, upper_right_y)
            page.trimbox.lower_left = (lower_left_x, lower_left_y)
            page.trimbox.upper_right = (upper_right_x, upper_right_y)
        writer.add_page(page)
    _copy_metadata(writer, reader)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def unlock_pdf(input_path: Path, output_path: Path, password: str) -> Path:
    """
    Remove password protection from the PDF at input_path and write the unlocked PDF to output_path.
    
    Preserves document pages and metadata. Raises ValueError if the PDF is encrypted and the provided password fails to decrypt it.
    
    Parameters:
        input_path (Path): Path to the source PDF (may be encrypted).
        output_path (Path): Path where the unlocked PDF will be written.
        password (str): Password to use for decryption.
    
    Returns:
        output_path (Path): The path to the written unlocked PDF.
    
    Raises:
        ValueError: If the PDF is encrypted and the provided password does not unlock it.
    """
    reader = PdfReader(str(input_path))
    if reader.is_encrypted:
        result = reader.decrypt(password)
        if result == 0:
            raise ValueError("Unable to unlock PDF")
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    writer.add_metadata(reader.metadata or {})
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def protect_pdf(input_path: Path, output_path: Path, password: str) -> Path:
    """
    Encrypts an unencrypted PDF file with the specified password.
    
    Sets the same value as both the user and owner password and enables 128-bit encryption; preserves the input PDF's metadata.
    
    Parameters:
        input_path (Path): Path to the source PDF file (must be unencrypted).
        output_path (Path): Path where the encrypted PDF will be written.
        password (str): Password to apply as both the user and owner password.
    
    Returns:
        Path: The path to the written encrypted PDF (the provided output_path).
    
    Raises:
        ValueError: If the input PDF is already encrypted.
    """
    reader = PdfReader(str(input_path))
    if reader.is_encrypted:
        raise ValueError("PDF is already encrypted")
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)
    writer.add_metadata(reader.metadata or {})
    writer.encrypt(user_password=password, owner_password=password, use_128bit=True)
    with output_path.open("wb") as handle:
        writer.write(handle)
    return output_path


def redact_pdf(
    input_path: Path,
    output_path: Path,
    text: str,
    pages: str | None,
) -> Path:
    """
    Redact all occurrences of a given text in a PDF, optionally restricted to specific pages.
    
    Searches each targeted page for exact occurrences of `text`, adds black redact annotations over matches, applies the redactions, and writes the modified PDF to `output_path`.
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the redacted PDF will be written.
        text (str): Text to search for and redact; matches are searched as exact occurrences.
        pages (str | None): Optional page selection string (e.g., "1-3,5") restricting which pages to process; if `None`, all pages are searched.
    
    Returns:
        Path: The `output_path` of the saved redacted PDF.
    """
    with fitz.open(str(input_path)) as document:
        _assert_fitz_unencrypted(document)
        total_pages = document.page_count
        target_pages = _resolve_page_selection(pages, total_pages)
        for index in range(total_pages):
            page_number = index + 1
            if target_pages is not None and page_number not in target_pages:
                continue
            page = document.load_page(index)
            rectangles = page.search_for(text)
            if not rectangles:
                continue
            for rect in rectangles:
                page.add_redact_annot(rect, fill=(0, 0, 0))
            page.apply_redactions()
        document.save(str(output_path), deflate=True)
    return output_path


def highlight_pdf(
    input_path: Path,
    output_path: Path,
    text: str,
    pages: str | None,
) -> Path:
    """
    Highlight occurrences of a given text in a PDF, optionally restricted to specific pages.
    
    Searches each targeted page for exact occurrences of `text`, adds highlight annotations over matches, and writes the modified PDF to `output_path`.
    
    Parameters:
        input_path (Path): Path to the source PDF.
        output_path (Path): Path where the highlighted PDF will be written.
        text (str): Text to search for and highlight; matches are searched as exact occurrences.
        pages (str | None): Optional page selection string (e.g., "1-3,5") restricting which pages to process; if `None`, all pages are searched.
    
    Returns:
        Path: The `output_path` of the saved highlighted PDF.
    """
    with fitz.open(str(input_path)) as document:
        _assert_fitz_unencrypted(document)
        total_pages = document.page_count
        target_pages = _resolve_page_selection(pages, total_pages)
        for index in range(total_pages):
            page_number = index + 1
            if target_pages is not None and page_number not in target_pages:
                continue
            page = document.load_page(index)
            rectangles = page.search_for(text)
            if not rectangles:
                continue
            for rect in rectangles:
                page.add_highlight_annot(rect)
        document.save(str(output_path), deflate=True)
    return output_path


def compare_pdfs(first_path: Path, second_path: Path, output_path: Path) -> Path:
    """
    Produce a plain-text comparison report summarizing page counts and per-page text differences between two PDFs.
    
    The report includes the input filenames, their page counts, and entries for pages that are missing from one file or whose extracted text differs; if no differences are found the report records that fact. The report is written to output_path using UTF-8.
    
    Parameters:
        first_path (Path): Path to the first PDF file (File A).
        second_path (Path): Path to the second PDF file (File B).
        output_path (Path): Destination path for the generated text report.
    
    Returns:
        Path: The path to the written report (output_path).
    """
    reader_a = _load_pdf(first_path)
    reader_b = _load_pdf(second_path)
    pages_a = len(reader_a.pages)
    pages_b = len(reader_b.pages)
    lines = [
        "ZenPDF comparison report",
        f"File A: {first_path.name}",
        f"File B: {second_path.name}",
        f"Pages: {pages_a} vs {pages_b}",
        "",
    ]
    differences: List[str] = []
    if pages_a != pages_b:
        differences.append("Page counts differ.")
    for index in range(max(pages_a, pages_b)):
        if index >= pages_a:
            differences.append(f"Page {index + 1}: missing from file A")
            continue
        if index >= pages_b:
            differences.append(f"Page {index + 1}: missing from file B")
            continue
        text_a = (reader_a.pages[index].extract_text() or "").strip()
        text_b = (reader_b.pages[index].extract_text() or "").strip()
        if text_a != text_b:
            differences.append(f"Page {index + 1}: text differs")
    if not differences:
        lines.append("No text differences detected.")
    else:
        lines.extend(differences)
    output_path.write_text("\n".join(lines), encoding="utf-8")
    return output_path


def image_to_pdf(inputs: Sequence[Path], output_path: Path) -> Path:
    """
    Combine one or more image files into a single PDF file.
    
    Parameters:
        inputs (Sequence[Path]): Paths to image files to include, in order.
        output_path (Path): Destination path for the generated PDF.
    
    Returns:
        Path: The path to the written PDF (same object as `output_path`).
    
    Raises:
        ValueError: If image rendering to PDF fails.
    """
    pdf_bytes = img2pdf.convert([str(path) for path in inputs])
    if pdf_bytes is None:
        raise ValueError("Failed to render images to PDF")
    output_path.write_bytes(pdf_bytes)
    return output_path


def pdf_to_jpg(input_path: Path, output_dir: Path, dpi: int = 150) -> List[Path]:
    """Render each PDF page to a JPG image."""
    with fitz.open(str(input_path)) as document:
        _assert_fitz_unencrypted(document)
        scale = dpi / 72
        matrix = fitz.Matrix(scale, scale)
        outputs: List[Path] = []
        for index in range(document.page_count):
            page = document.load_page(index)
            render = getattr(page, "get_pixmap", None)
            if not callable(render):
                raise ValueError("PDF renderer unavailable")
            pix = render(matrix=matrix)
            output_path = output_dir / f"page_{index + 1}.jpg"
            saver = getattr(pix, "save", None)
            if not callable(saver):
                raise ValueError("Rendered page cannot be saved")
            saver(str(output_path))
            outputs.append(output_path)
        return outputs


def zip_outputs(outputs: Iterable[Path], zip_path: Path) -> Path:
    """Zip multiple output files into a single archive."""
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for item in outputs:
            archive.write(item, arcname=item.name)
    return zip_path


MAX_WEB_BYTES = 2 * 1024 * 1024
UNICODE_FONT_PATHS = (
    Path(__file__).resolve().parent / "assets" / "DejaVuSans.ttf",
    Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
    Path("/usr/share/fonts/DejaVuSans.ttf"),
)


def _resolve_unicode_font_path() -> Path | None:
    """
    Locate a Unicode-compatible TrueType font file if one is available.
    
    Checks the ZENPDF_TTF_PATH environment variable first, then falls back to known candidate paths.
    
    Returns:
        Path | None: Path to the font file if found, `None` otherwise.
    """
    env_path = os.getenv("ZENPDF_TTF_PATH")
    if env_path:
        candidate = Path(env_path)
        if candidate.is_file():
            return candidate
    for candidate in UNICODE_FONT_PATHS:
        if candidate.is_file():
            return candidate
    return None


def _set_overlay_font(pdf: FPDF, text: str, size: int) -> None:
    """
    Selects and configures an appropriate font on the given FPDF instance for rendering overlay text.
    
    Attempts to load a Unicode-capable DejaVu Sans from the environment or known paths; if unavailable, verifies whether the provided text can be encoded in Latin-1 and falls back to Helvetica. If the text requires Unicode and no Unicode font is available, raises RuntimeError.
    
    Parameters:
        pdf (FPDF): The FPDF instance to configure.
        text (str): Sample text to test whether a Unicode font is required.
        size (int): Font size to set on the PDF.
    
    Raises:
        RuntimeError: If the text contains characters that require a Unicode font but no Unicode font path is available.
    """
    font_path = _resolve_unicode_font_path()
    if font_path:
        pdf.add_font("DejaVuSans", fname=str(font_path), uni=True)
        pdf.set_font("DejaVuSans", size=size)
        return
    try:
        text.encode("latin-1")
    except UnicodeEncodeError as error:
        raise RuntimeError(
            "Unicode font unavailable. Set ZENPDF_TTF_PATH to a DejaVuSans.ttf path."
        ) from error
    pdf.set_font("Helvetica", size=size)


class _HTMLTextExtractor(HTMLParser):
    """Minimal HTML to text extractor."""

    def __init__(self) -> None:
        """Initialize the parser."""
        super().__init__()
        self._parts: List[str] = []

    def handle_data(self, data: str) -> None:
        """Capture text content from HTML."""
        text = data.strip()
        if text:
            self._parts.append(text)

    def text(self) -> str:
        """Return the collected text content."""
        return "\n".join(self._parts)


def html_to_pdf(html: str, output_path: Path) -> Path:
    """Render basic HTML text into a PDF."""
    parser = _HTMLTextExtractor()
    parser.feed(html)
    text = parser.text() or "(no content)"

    pdf = FPDF(orientation="P", unit="mm", format="A4")
    pdf.set_margins(15, 15, 15)
    pdf.set_auto_page_break(auto=True, margin=15)
    pdf.add_page()
    font_path = _resolve_unicode_font_path()
    if font_path:
        pdf.add_font("DejaVuSans", fname=str(font_path), uni=True)
        pdf.set_font("DejaVuSans", size=12)
    else:
        try:
            text.encode("latin-1")
        except UnicodeEncodeError as error:
            raise RuntimeError(
                "Unicode font unavailable. Set ZENPDF_TTF_PATH to a DejaVuSans.ttf path."
            ) from error
        pdf.set_font("Helvetica", size=12)
    max_width = pdf.w - pdf.l_margin - pdf.r_margin
    for line in text.splitlines():
        if line.strip():
            pdf.multi_cell(max_width, 6, line)
        else:
            pdf.ln(5)
    pdf.output(str(output_path))
    return output_path


def web_to_pdf(url: str, output_path: Path) -> Path:
    """Fetch a URL and convert its HTML to PDF."""
    parsed = urlparse(url)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError("Only http/https URLs are supported")
    target_ip = _resolve_public_ip(parsed.hostname)
    is_ipv6 = isinstance(ipaddress.ip_address(target_ip), ipaddress.IPv6Address)
    host = f"[{target_ip}]" if is_ipv6 else target_ip
    netloc = f"{host}:{parsed.port}" if parsed.port is not None else host
    try:
        hostname_ip = ipaddress.ip_address(parsed.hostname)
        hostname_is_ipv6 = isinstance(hostname_ip, ipaddress.IPv6Address)
    except ValueError:
        hostname_is_ipv6 = False

    host_header = (
        f"[{parsed.hostname}]" if hostname_is_ipv6 else parsed.hostname
    )
    if parsed.port is not None:
        host_header = f"{host_header}:{parsed.port}"
    target_url = parsed._replace(netloc=netloc).geturl()

    def _fetch_html(
        session: requests.Session,
        fetch_url: str,
        header: str | None,
    ) -> tuple[bytearray, str]:
        body = bytearray()
        headers = {"Host": header} if header else None
        with session.get(
            fetch_url,
            timeout=20,
            allow_redirects=False,
            stream=True,
            headers=headers,
        ) as response:
            if 300 <= response.status_code < 400:
                raise ValueError("Redirects are not allowed")
            response.raise_for_status()
            for chunk in response.iter_content(chunk_size=64 * 1024):
                if not chunk:
                    continue
                body.extend(chunk)
                if len(body) > MAX_WEB_BYTES:
                    raise ValueError("Web response too large")
            encoding = response.encoding or "utf-8"
        return body, encoding

    body: bytearray
    encoding: str
    with requests.Session() as session:
        if parsed.scheme == "https":
            session.mount("https://", HostHeaderSSLAdapter())

        try:
            body, encoding = _fetch_html(session, target_url, host_header)
        except requests.exceptions.SSLError:
            allow_fallback = (
                parsed.scheme == "https"
                and os.getenv("ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK") == "1"
            )
            if not allow_fallback:
                raise
            # Re-validate hostname before falling back to hostname-based HTTPS.
            _resolve_public_ip(parsed.hostname)
            with requests.Session() as fallback_session:
                body, encoding = _fetch_html(fallback_session, parsed.geturl(), None)

    html = body.decode(encoding, errors="replace")
    return html_to_pdf(html, output_path)


def office_to_pdf(input_path: Path, output_dir: Path) -> Path:
    """Convert an Office document to PDF using LibreOffice."""
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        raise RuntimeError("LibreOffice is required for office-to-pdf")

    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        result = subprocess.run(
            [
                soffice,
                "--headless",
                "--convert-to",
                "pdf",
                "--outdir",
                str(output_dir),
                str(input_path),
            ],
            capture_output=True,
            text=True,
            check=False,
            timeout=120,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Office conversion timed out") from error
    if result.returncode != 0:
        raise RuntimeError(result.stderr or result.stdout or "Office conversion failed")

    output_path = output_dir / f"{input_path.stem}.pdf"
    if not output_path.exists():
        raise RuntimeError("Office conversion produced no output")
    return output_path


PDF_A_TIMEOUT_SEC = 120
PDF_A_VERSION_TIMEOUT_SEC = 10
PDF_A_MIN_VERSION = (10, 3, 1)


def _parse_version_tuple(raw: str) -> Tuple[int, int, int]:
    """Parse a version string into a comparable tuple."""
    token = raw.strip().split()[0] if raw.strip() else ""
    parts = [part for part in token.split(".") if part]
    numbers: List[int] = []
    for part in parts:
        if not part.isdigit():
            break
        numbers.append(int(part))
    if not numbers:
        raise ValueError("Unable to parse version")
    while len(numbers) < 3:
        numbers.append(0)
    return (numbers[0], numbers[1], numbers[2])


def pdf_to_pdfa(input_path: Path, output_path: Path) -> Path:
    """
    Convert a PDF into PDF/A-2b using Ghostscript.
    
    Parameters:
        input_path (Path): Path to the source PDF file.
        output_path (Path): Destination path for the PDF/A file.
    
    Returns:
        Path: The output_path of the generated PDF/A file.
    
    Raises:
        RuntimeError: If Ghostscript is missing, times out, or fails the conversion.
        ValueError: If the input PDF is encrypted.
    """
    reader = PdfReader(str(input_path))
    if reader.is_encrypted:
        raise ValueError("Encrypted PDFs are not supported for PDF/A conversion")

    ghostscript = shutil.which("gs")
    if not ghostscript:
        raise RuntimeError("Ghostscript is required for PDF/A conversion")

    version_result = subprocess.run(
        [ghostscript, "--version"],
        capture_output=True,
        text=True,
        check=False,
        timeout=PDF_A_VERSION_TIMEOUT_SEC,
    )
    if version_result.returncode != 0:
        raise RuntimeError("Ghostscript version check failed")
    version_output = (version_result.stdout or version_result.stderr or "").strip()
    try:
        version = _parse_version_tuple(version_output)
    except ValueError as error:
        raise RuntimeError("Ghostscript >= 10.03.1 is required for PDF/A conversion") from error
    if version < PDF_A_MIN_VERSION:
        raise RuntimeError("Ghostscript >= 10.03.1 is required for PDF/A conversion")

    output_path.parent.mkdir(parents=True, exist_ok=True)

    command = [
        ghostscript,
        "-dSAFER",
        "-dPDFA=2",
        "-dBATCH",
        "-dNOPAUSE",
        "-dNOOUTERSAVE",
        "-sDEVICE=pdfwrite",
        "-dPDFACompatibilityPolicy=1",
        "-sProcessColorModel=DeviceRGB",
        "-sColorConversionStrategy=RGB",
        "-dUseCIEColor",
        f"-sOutputFile={output_path}",
        str(input_path),
    ]

    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            check=False,
            timeout=PDF_A_TIMEOUT_SEC,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("PDF/A conversion timed out") from error

    if result.returncode != 0:
        raise RuntimeError(result.stderr or result.stdout or "PDF/A conversion failed")

    if not output_path.exists():
        raise RuntimeError("PDF/A conversion produced no output")

    return output_path


def pdf_to_docx(input_path: Path, output_path: Path) -> Path:
    """Convert a PDF into a Word document by extracting text."""
    reader = _load_pdf(input_path)
    document = Document()
    for index, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        if index > 1:
            document.add_page_break()
        if text.strip():
            for line in text.splitlines():
                if line.strip():
                    document.add_paragraph(line)
        else:
            document.add_paragraph("")
    document.save(str(output_path))
    return output_path


def pdf_to_text(input_path: Path, output_path: Path) -> Path:
    """Extract PDF text into a plain UTF-8 text file."""
    reader = _load_pdf(input_path)
    lines: List[str] = []
    for index, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        if index > 1:
            lines.append("")
        if text.strip():
            lines.extend(line.rstrip() for line in text.splitlines())
        else:
            lines.append("")
    output_path.write_text("\n".join(lines).rstrip() + "\n", encoding="utf-8")
    return output_path


OCR_DPI = 300
DEFAULT_OCR_LANG = os.getenv("ZENPDF_OCR_LANG", "eng")


def _render_page_image(page: fitz.Page, dpi: int) -> Image.Image:
    """Render a PDF page to a PIL image for OCR."""
    scale = dpi / 72
    matrix = fitz.Matrix(scale, scale)
    pix = page.get_pixmap(matrix=matrix, alpha=False)
    image = Image.open(BytesIO(pix.tobytes("png")))
    return image.convert("RGB")


def _ocr_image(image: Image.Image, lang: str) -> str:
    """Run OCR on a PIL image using Tesseract."""
    if pytesseract is None:
        raise RuntimeError("pytesseract is required for OCR conversions")
    if not shutil.which("tesseract"):
        raise RuntimeError("Tesseract is required for OCR conversions")
    return pytesseract.image_to_string(image, lang=lang)


def _ocr_pdf_pages(input_path: Path, lang: str, dpi: int) -> List[str]:
    """Extract OCR text for each page in a PDF."""
    document = fitz.open(str(input_path))
    page_texts: List[str] = []
    try:
        for index in range(document.page_count):
            page = document.load_page(index)
            image = _render_page_image(page, dpi)
            page_texts.append(_ocr_image(image, lang).strip())
    finally:
        document.close()
    return page_texts


def pdf_to_docx_ocr(input_path: Path, output_path: Path, lang: str | None = None) -> Path:
    """Convert a PDF into a Word document using OCR."""
    language = (lang or DEFAULT_OCR_LANG).strip() or DEFAULT_OCR_LANG
    page_texts = _ocr_pdf_pages(input_path, language, OCR_DPI)
    document = Document()
    for index, text in enumerate(page_texts, start=1):
        if index > 1:
            document.add_page_break()
        if text.strip():
            for line in text.splitlines():
                if line.strip():
                    document.add_paragraph(line)
        else:
            document.add_paragraph("")
    document.save(str(output_path))
    return output_path


def pdf_to_xlsx(input_path: Path, output_path: Path) -> Path:
    """Convert a PDF into an Excel workbook by extracting text."""
    reader = _load_pdf(input_path)
    workbook = Workbook()
    sheet = workbook.active
    row = 1
    for index, page in enumerate(reader.pages, start=1):
        text = page.extract_text() or ""
        lines = [line for line in text.splitlines() if line.strip()]
        if not lines:
            sheet.cell(row=row, column=1, value=f"Page {index}")
            row += 1
        else:
            for line in lines:
                sheet.cell(row=row, column=1, value=line)
                row += 1
        row += 1
    workbook.save(str(output_path))
    return output_path


def pdf_to_xlsx_ocr(input_path: Path, output_path: Path, lang: str | None = None) -> Path:
    """Convert a PDF into an Excel workbook using OCR."""
    language = (lang or DEFAULT_OCR_LANG).strip() or DEFAULT_OCR_LANG
    page_texts = _ocr_pdf_pages(input_path, language, OCR_DPI)
    workbook = Workbook()
    sheet = workbook.active
    row = 1
    for index, text in enumerate(page_texts, start=1):
        lines = [line for line in text.splitlines() if line.strip()]
        if not lines:
            sheet.cell(row=row, column=1, value=f"Page {index}")
            row += 1
        else:
            for line in lines:
                sheet.cell(row=row, column=1, value=line)
                row += 1
        row += 1
    workbook.save(str(output_path))
    return output_path


def _resolve_public_ip(hostname: str) -> str:
    """Resolve a hostname to a public IP address."""
    try:
        infos = socket.getaddrinfo(hostname, None)
    except socket.gaierror as error:
        raise ValueError("Unable to resolve host") from error

    public_ips: List[str] = []
    for info in infos:
        address = str(info[4][0])
        if _is_public_ip(address):
            public_ips.append(address)

    if public_ips:
        ipv4_candidates: List[str] = []
        for address in public_ips:
            try:
                ip = ipaddress.ip_address(address)
            except ValueError:
                continue
            if isinstance(ip, ipaddress.IPv4Address):
                ipv4_candidates.append(address)
        if ipv4_candidates:
            return ipv4_candidates[0]
        return public_ips[0]
    raise ValueError("URL host is not allowed")


def _is_public_ip(address: str) -> bool:
    """Return True if the address is a public IP."""
    try:
        ip = ipaddress.ip_address(address)
    except ValueError:
        return False
    return not (
        ip.is_private
        or ip.is_loopback
        or ip.is_link_local
        or ip.is_multicast
        or ip.is_reserved
        or ip.is_unspecified
    )
