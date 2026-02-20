# Implementation Changelog

## 2026-02-20 — Tool Fidelity Remediation (Phases 0-5)

### Scope completed
- Added expected-vs-actual fidelity hardening across worker conversion tools.
- Added stricter input validation behavior for page ranges and rotate angle handling.
- Added mode-based conversion controls for HTML/PDF-to-Word/PDF-to-Excel.
- Added richer comparison and redaction behavior.
- Added OCR/signature/office conversion resilience improvements.
- Removed runtime-dead helper paths to reduce maintenance ambiguity.

### Worker behavior changes
- `html-to-pdf`
  - Added `renderMode` support (`browser`/`text`).
  - Default now uses headless Chromium browser rendering when available.
  - Text extraction rendering remains available as explicit fallback mode.
- `pdf-to-word`
  - Added `mode` support (`auto`/`text`/`ocr`).
  - `auto` uses text-density heuristic to choose OCR when pages are text-light.
- `pdf-to-excel`
  - Added `mode` support (`auto`/`table`/`text`/`ocr`).
  - `auto` prefers table extraction, then OCR for text-light pages, else text extraction.
- `compare`
  - Added `includeVisualDiff` option.
  - Report now includes a visual comparison section (low-resolution delta summary).
- `redact`
  - Added options: `caseSensitive`, `wholeWord`, `regex`, `ocrAssist`.
  - OCR assist can redact scanned pages when direct text match is unavailable.
- `page-numbers`
  - Added `numberingMode` (`documentIndex`/`selectionIndex`).
- `sign-pdf`
  - Added `anchor` presets with bounds-safe placement.
- `repair`
  - Staged repair pipeline: `qpdf` -> `mutool` -> `pypdf` fallback.
- `pdfa`
  - Added post-conversion validation step (`veraPDF` when available; structural fallback otherwise).
- `office-to-pdf`
  - Added adaptive timeout based on input size.
  - Added friendlier error mapping for encrypted/protected files.
- OCR preprocessing
  - Added autocontrast, denoise, threshold before OCR extraction.

### Validation and parsing changes
- Page-range parsing is now strict and deterministic.
- Duplicate page selections are de-duplicated in first-seen order.
- Rotate no longer silently coerces invalid angle values.

### Dead path removals
Removed runtime-unexposed or test-only helper paths:
- `highlight_pdf`
- `remove_pages`
- `reorder_pages`
- `pdf_to_text`
- `pdf_to_docx_ocr`
- `pdf_to_xlsx_ocr`

### UI wiring updates
- Added optional advanced fields in tools UI for new config options:
  - `mode`, `renderMode`, `includeVisualDiff`, redact matching toggles, `numberingMode`, `anchor`.
- Added select field rendering support and boolean config normalization.

### Test updates
- Updated worker tests for new conversion modes and strict parsing.
- Added visual compare assertion coverage.
- Added redaction whole-word mode test.
- Added browser-mode HTML-to-PDF path test with command mocking.
- Added page-number selection-index behavior test.

