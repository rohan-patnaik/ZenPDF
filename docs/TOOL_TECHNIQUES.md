# ZenPDF Tool Techniques

Last Updated: 2026-02-20
Version: 3.0

This document records the current implementation strategy for the strict 27-tool catalog aligned with iLovePDF naming.

## Fidelity baseline
- Expected-vs-actual fixture matrix lives at `apps/worker/tests/fixtures/expected_matrix.json`.
- New tool changes should add/update assertion signals in the matrix and test coverage in `apps/worker/tests/test_tools.py`.

## Core approach
- Structural PDF operations: `pypdf`.
- Text/annotation and raster operations: `PyMuPDF` (`fitz`).
- Overlay rendering: `fpdf2`.
- HTML browser rendering: Chromium headless `--print-to-pdf`.
- Office conversion: LibreOffice (`soffice --headless --convert-to pdf`).
- OCR: `ocrmypdf` (primary, when available), `pytesseract` fallback.
- Table extraction for spreadsheet conversion: `pdfplumber` (when available).
- Validation and repair helpers: `qpdf`, `mutool`, Ghostscript where applicable.

## Decision rationale
- Selection criteria:
  - Prefer actively maintained OSS libraries with permissive licensing.
  - Prefer deterministic outputs and predictable failure modes for worker retries.
  - Prefer tools available in containerized Linux runtimes without proprietary dependencies.
- `pypdf` for structural edits:
  - Chosen for pure-Python portability and reliable page/object manipulation.
  - Alternatives like `PyPDF2`/`pdfrw` were not selected due to older maintenance posture or narrower APIs for this pipeline.
- `PyMuPDF` for text search and rasterization:
  - Chosen for fast text geometry APIs and robust rendering performance.
  - Trade-off: heavier binary dependency than pure-Python libraries.
- `fpdf2` for overlays:
  - Chosen for lightweight text/shape overlays with straightforward composition into existing PDFs.
- `ocrmypdf` primary with `pytesseract` fallback:
  - `ocrmypdf` provides best quality and metadata-preserving OCR when present.
  - Fallback keeps OCR available in constrained environments where `ocrmypdf` is unavailable.
- `qpdf`/`mutool`/Ghostscript for validation and conversion:
  - Chosen as complementary tools: structure checks (`qpdf`), repair/cleanup (`mutool`), standards conversion and compression candidates (Ghostscript).
  - Trade-off: additional runtime dependencies, but better resilience on malformed files and broad PDF compatibility.

## Tool matrix (27 only)
- Merge PDF: `pypdf` append pages.
- Split PDF: `pypdf` range split -> ZIP output.
- Compress PDF: staged compression pipeline (normalize/repair + image-heavy branch + candidate selection).
- PDF to Word: mode-based pipeline (`auto`/`text`/`ocr`) with text-density heuristic.
- PDF to PowerPoint: render each page with `PyMuPDF`, place as full-slide image with `python-pptx`.
- PDF to Excel: mode-based pipeline (`auto`/`table`/`text`/`ocr`) with table-first extraction.
- Word to PDF: LibreOffice conversion with `.doc/.docx` extension guard.
- PowerPoint to PDF: LibreOffice conversion with `.ppt/.pptx` extension guard.
- Excel to PDF: LibreOffice conversion with `.xls/.xlsx` extension guard.
- Edit PDF: structured operations via `PyMuPDF` (text, shapes, whiteout, page delete/insert).
- PDF to JPG: `PyMuPDF` page rasterization, deterministic naming, ZIP archive.
- JPG to PDF: `img2pdf`.
- Sign PDF: visible text signature stamp with anchor presets and bounds-safe placement.
- Watermark: diagonal overlay merged with `pypdf`.
- Rotate PDF: page rotation with `pypdf`.
- HTML to PDF: URL guard + preflight + browser render mode, with text extraction fallback mode.
- Unlock PDF: lazy password flow (`qpdf` first, `pypdf` fallback).
- Protect PDF: `pypdf` encryption.
- Organize PDF: single operation combining delete/reorder/rotate.
- PDF to PDF/A: Ghostscript conversion plus post-conversion conformance validation path.
- Repair PDF: staged repair (`qpdf` -> `mutool` -> `pypdf` fallback).
- Page numbers: centered footer overlay with `documentIndex`/`selectionIndex` behavior.
- Scan to PDF: image capture files routed to `img2pdf`.
- OCR PDF: `ocrmypdf` primary; fallback builds searchable page PDFs from Tesseract and merges.
- Compare PDF: text-diff report with optional visual-delta summary.
- Redact PDF: configurable matching (`caseSensitive`/`wholeWord`/`regex`) + optional OCR assist.
- Crop PDF: box adjustment with `pypdf`.

## Local mode behavior
- `ZENPDF_DEV_MODE=1` enables local development bypass for plan limits in job creation.
- SSL fallback for HTML-to-PDF can be enabled in local/dev mode for self-signed environments.

## Key env flags
- `ZENPDF_DEV_MODE=1`
- `ZENPDF_OCR_USE_OCRMYPDF=1`
- `ZENPDF_WEB_ALLOW_INSECURE_SSL=1` (dev only)
- `ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK=1`
- `ZENPDF_BROWSER_PATH` (optional override for Chromium binary)
- `ZENPDF_OFFICE_TIMEOUT_BASE_SECONDS`, `ZENPDF_OFFICE_TIMEOUT_PER_MB_SECONDS`, `ZENPDF_OFFICE_TIMEOUT_MAX_SECONDS`
- `ZENPDF_OCR_TEXT_DENSITY_THRESHOLD`, `ZENPDF_OCR_PREPROCESS_THRESHOLD`
- Compression tuning flags remain documented in `apps/worker/.env.example`.
