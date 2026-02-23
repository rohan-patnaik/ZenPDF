# ZenPDF Feature Logic

This file documents how each tool works internally.

Source of truth in code:
- Dispatch and input validation: `apps/worker/zenpdf_worker/worker.py` (`ZenPdfWorker._run_tool`)
- Tool implementations: `apps/worker/zenpdf_worker/tools.py`

## Shared execution flow
1. Web queues a job in Convex.
2. Worker claims the job and downloads inputs.
3. Worker validates tool config and runs the matching tool function.
4. Outputs are uploaded to storage and attached to the job.
5. Job is marked completed or failed with a stable error code.

## Cross-cutting behavior
- Page ranges:
  - Strict parser by default.
  - Optional tolerant mode via `tolerantRanges`.
- User input errors:
  - Invalid user config raises `ValueError` and maps to `USER_INPUT_INVALID`.
- OCR profiles:
  - `fast`, `balanced`, `accurate` alter DPI and preprocess levels.
- HTML-to-PDF network safety:
  - Public-network host validation, no redirect bypass, subresource request blocking.

## Tool internals (27)
- `merge` (Merge PDF): Appends pages from multiple PDFs using `pypdf`.
- `split` (Split PDF): Expands ranges, writes split PDFs, then zips outputs.
- `compress` (Compress PDF): Runs staged compression pipeline and returns compression metadata.

- `pdf-to-word` (PDF to Word):
  - Modes: `auto`, `layout`, `text`, `ocr`.
  - `layout` uses text blocks + tables + images.
  - `auto` uses OCR for text-light PDFs, otherwise layout mode.
- `pdf-to-powerpoint` (PDF to PowerPoint):
  - Modes: `visual`, `editable`.
  - `visual` renders full-page images per slide.
  - `editable` maps extracted text blocks and images into slide objects (best effort).
- `pdf-to-excel` (PDF to Excel):
  - Modes: `auto`, `table`, `text`, `ocr`.
  - Table-first extraction via `pdfplumber`; text/ocr fallbacks write structured sheets.

- `word-to-pdf` (Word to PDF): Validates extension and converts via LibreOffice.
- `powerpoint-to-pdf` (PowerPoint to PDF): Validates extension and converts via LibreOffice.
- `excel-to-pdf` (Excel to PDF): Validates extension and converts via LibreOffice.
  - Office conversion uses adaptive timeout + retry-safe profile and font-related error mapping.

- `edit-pdf` (Edit PDF): Applies structured operations (text, shapes, whiteout, page ops) via `PyMuPDF`.
- `pdf-to-jpg` (PDF to JPG): Renders each page to JPG and packages as ZIP.
- `jpg-to-pdf` (JPG to PDF): Converts one or more images to PDF (`img2pdf`).

- `sign-pdf` (Sign PDF):
  - Modes: `visual`, `cryptographic`.
  - Visual mode supports text and/or image stamps with anchor presets.
  - Cryptographic mode signs with PKCS#12 (`.p12`/`.pfx`) using `pyHanko`.
- `watermark` (Watermark): Creates overlay pages and merges watermark text diagonally.
- `rotate` (Rotate PDF): Rotates selected pages; angle must be 90/180/270.
- `html-to-pdf` (HTML to PDF):
  - Modes: `browser`, `text`.
  - `browser` uses Playwright/Chromium with SSRF-safe request policy.
  - `text` fetches HTML safely and writes extracted text into a PDF.

- `unlock` (Unlock PDF): Attempts decrypt pipeline (`qpdf` first, then `pypdf` fallback).
- `protect` (Protect PDF): Encrypts PDF with user password via `pypdf`.
- `organize-pdf` (Organize PDF): Applies reorder/delete/rotate in one combined operation.

- `pdfa` (PDF to PDF/A):
  - Converts with Ghostscript (PDF/A-2b settings).
  - Verifies output via `veraPDF` when available; otherwise structural fallback check.
  - Returns conformance info in `toolResult`.
- `repair` (Repair PDF): Staged repair: `qpdf` -> `mutool` -> `pypdf` rewrite fallback.
- `page-numbers` (Page numbers): Adds centered footers with modes:
  - `selectionIndex` (default): sequential over selected pages.
  - `documentIndex`: absolute document index numbering.

- `scan-to-pdf` (Scan to PDF): Image inputs routed through image-to-PDF path.
- `ocr-pdf` (OCR PDF): Uses `ocrmypdf` when available, otherwise Tesseract-based searchable PDF fallback.
- `compare` (Compare PDF):
  - Text diff report.
  - Optional visual diff summary with changed-region bounding boxes.
- `redact` (Redact PDF):
  - Supports `caseSensitive`, `wholeWord`, `regex`, `ocrAssist`.
  - OCR assist can derive redaction boxes for scanned pages.
  - Saves with non-incremental clean settings to keep redactions irreversible.
- `crop` (Crop PDF): Adjusts page boxes from margins, with optional page selection.

## Notes for contributors
When changing tool behavior, update these together:
- `apps/worker/zenpdf_worker/worker.py`
- `apps/worker/zenpdf_worker/tools.py`
- `apps/web/src/app/tools/page.tsx`
- `apps/worker/tests/test_tools.py`
- this file
