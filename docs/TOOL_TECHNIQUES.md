# ZenPDF Tool Techniques

Last Updated: 2026-02-05
Version: 2.0

This document records the current implementation strategy for the strict 27-tool catalog aligned with iLovePDF naming.

## Core approach
- Structural PDF operations: `pypdf`.
- Text/annotation and raster operations: `PyMuPDF` (`fitz`).
- Overlay rendering: `fpdf2`.
- Office conversion: LibreOffice (`soffice --headless --convert-to pdf`).
- OCR: `ocrmypdf` (primary, when available), `pytesseract` fallback.
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
- PDF to Word: `python-docx` from extracted text.
- PDF to PowerPoint: render each page with `PyMuPDF`, place as full-slide image with `python-pptx`.
- PDF to Excel: extracted text rows into `openpyxl`.
- Word to PDF: LibreOffice conversion with `.doc/.docx` extension guard.
- PowerPoint to PDF: LibreOffice conversion with `.ppt/.pptx` extension guard.
- Excel to PDF: LibreOffice conversion with `.xls/.xlsx` extension guard.
- Edit PDF: structured operations via `PyMuPDF` (text, shapes, whiteout, page delete/insert).
- PDF to JPG: `PyMuPDF` page rasterization, deterministic naming, ZIP archive.
- JPG to PDF: `img2pdf`.
- Sign PDF: visible text signature stamp with `PyMuPDF`.
- Watermark: diagonal overlay merged with `pypdf`.
- Rotate PDF: page rotation with `pypdf`.
- HTML to PDF: URL fetch + SSRF guard + text render with `fpdf2`.
- Unlock PDF: lazy password flow (`qpdf` first, `pypdf` fallback).
- Protect PDF: `pypdf` encryption.
- Organize PDF: single operation combining delete/reorder/rotate.
- PDF to PDF/A: Ghostscript PDF/A conversion.
- Repair PDF: PDF rewrite/repair path.
- Page numbers: centered footer overlay with `fpdf2` + `pypdf` merge.
- Scan to PDF: image capture files routed to `img2pdf`.
- OCR PDF: `ocrmypdf` primary; fallback builds searchable page PDFs from Tesseract and merges.
- Compare PDF: text extraction and plain-text diff report.
- Redact PDF: text search + redaction annotations in `PyMuPDF`.
- Crop PDF: box adjustment with `pypdf`.

## Local mode behavior
- `ZENPDF_DEV_MODE=1` enables local development bypass for plan limits in job creation.
- SSL fallback for HTML-to-PDF can be enabled in local/dev mode for self-signed environments.

## Key env flags
- `ZENPDF_DEV_MODE=1`
- `ZENPDF_OCR_USE_OCRMYPDF=1`
- `ZENPDF_WEB_ALLOW_INSECURE_SSL=1` (dev only)
- `ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK=1`
- Compression tuning flags remain documented in `apps/worker/.env.example`.
