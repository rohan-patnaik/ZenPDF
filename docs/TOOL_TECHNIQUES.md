# ZenPDF Tool Techniques

Last Updated: 2026-02-04  
Version: 1.2

This document summarizes how each tool is implemented and what technique(s) are used under the hood.
It captures the current execution path for each feature so troubleshooting and tuning are easier and records the decision rationale behind those choices.

## Decision Criteria

- Prefer stable OSS libraries with active maintenance.
- Favor deterministic, reproducible outputs for core operations.
- Use fallbacks for malformed inputs and flaky tooling.
- Balance throughput, quality, and cost for worker execution.

## Core PDF tools

- Merge PDFs — Technique: pypdf `PdfReader` + `PdfWriter` to append pages.
- Split PDF — Technique: pypdf page slicing into individual PDFs, then zip for multi-output.
- Compress PDF — Technique: staged pipeline with fallbacks (see detailed section below).
- Repair PDF — Technique: pypdf rewrite (rebuilds structure).
- Rotate pages — Technique: pypdf rotate selected pages.
- Remove pages — Technique: pypdf rebuilds a PDF without specified pages.
- Reorder pages — Technique: pypdf page reordering.
- Watermark — Technique: FPDF overlays text onto each page, merged via pypdf.
- Page numbers — Technique: FPDF overlays numbers onto each page, merged via pypdf.
- Crop pages — Technique: pypdf updates crop/trim/media boxes.
- Redact text — Technique: PyMuPDF (`fitz`) text search + redaction annotations + apply.
- Highlight text — Technique: PyMuPDF (`fitz`) text search + highlight annotations.
- Compare PDFs — Technique: pypdf text extraction; outputs a plain-text diff summary.
- Unlock PDF — Technique: pypdf decrypts with password and rewrites unencrypted output.
- Protect PDF — Technique: pypdf encrypts with user/owner password.

Decision Rationale:
- Use pypdf for structural edits because it is lightweight and reliable for page-level operations.
- Use PyMuPDF for text search/highlight/redaction because it provides accurate text search and annotation APIs.

Trade-offs:
- pypdf does not re-render content, so it cannot fix rendering issues or reduce image sizes.
- PyMuPDF adds a heavier dependency but is required for reliable text geometry operations.

## Compression pipeline (detail)

**Goals**
- Improve compression on image-heavy PDFs while maintaining reliability and friendly errors.
- Keep determinism where feasible, without breaking encrypted PDFs or malformed inputs.
- Choose the smallest valid output and report `no_change` when savings are insignificant.

**Pipeline**
1. Preflight check:
   - Reject encrypted PDFs early.
   - Attempt parse with `pypdf` and `PyMuPDF` (best-effort).
2. Normalize/repair (best-effort; first success wins):
   - `mutool clean -gg` (preferred if installed).
   - `qpdf --object-streams=generate --compress-streams=y --recompress-flate`.
   - Fallback: pypdf rewrite.
3. Image-heavy detector (PyMuPDF; samples up to 10 pages):
   - Counts images per page and text chars per page.
   - Marks image-heavy if `images_per_page >= 1.0` or `(text_chars_per_page < 500 and images_per_page > 0.5)`.
4. If image-heavy:
   - Run Ghostscript early with profile-specific downsampling (balanced by default).
   - Follow with `qpdf` structural optimization.
5. Lossless structural optimization:
   - `qpdf` recompress + deterministic ID.
   - Optional `mutool merge -O compress` pass.
6. Optional image optimization (qpdf):
   - `qpdf --optimize-images` with quality/min-size guards.
7. Optional `pdfsizeopt` (env flagged):
   - Can enable JBIG2 if `jbig2` is available.
8. If not image-heavy:
   - Run Ghostscript as a late-stage candidate.
9. Validate candidates:
   - `qpdf --check` (if available).
   - PyMuPDF render of first page and page-count match.
10. Select smallest valid output:
    - If savings below thresholds, return `no_change` and keep original.
11. Determinism:
    - Final `qpdf --deterministic-id` pass.
    - Optional Zopfli (env flagged, slow) on the final output.

**Ghostscript presets**
- `balanced` (default): `/ebook`, 150 dpi color/gray.
- `strong`: `/screen`, 100 dpi color/gray (higher loss).
- `light`: `/ebook` with JPEG pass-through (less loss, smaller gains).

**Validation checklist**
- Output exists and size > 0.
- `qpdf --check` passes (if available).
- PyMuPDF can open and render the first page.
- Page count unchanged.

## Conversion tools

- Image → PDF — Technique: `img2pdf` converts images to a single PDF.
- PDF → JPG — Technique: PyMuPDF rasterizes each page to JPG at a given DPI.
- PDF → PDF/A — Technique: Ghostscript PDF/A-2b conversion with version checks.
- PDF → Text — Technique: pypdf text extraction to .txt.
- PDF → Word — Technique: `pdf2docx` conversion.
- PDF → Word (OCR) — Technique: PyMuPDF renders pages + Tesseract OCR + `python-docx`.
- PDF → Excel — Technique: text extraction into `openpyxl` worksheet.
- PDF → Excel (OCR) — Technique: OCR + `openpyxl`.
- Office → PDF — Technique: LibreOffice headless conversion (`soffice --convert-to pdf`).
- Web → PDF — Technique: fetch HTML over HTTP(S), extract text, render with FPDF.

Decision Rationale:
- Use specialized tools for each conversion (LibreOffice for Office, Tesseract for OCR) to maximize fidelity.
- Keep conversions deterministic and avoid heavy rendering engines when possible.

Trade-offs:
- Some converters (OCR, Office) are slower and have larger dependency footprints.
- Basic HTML-to-PDF rendering is fast but not a full browser engine, so complex layouts may not match.

## Notes on compression tuning

- The compression pipeline is intentionally staged to handle malformed PDFs and avoid timeouts.
- `pdfsizeopt` and `jbig2enc` are optional due to heavier dependencies and more aggressive (lossy) behavior.
- Ghostscript uses profile-driven downsampling; use `light` for conservative outputs and `strong` for aggressive compression.
- The worker returns a structured compression result to the UI: status, method, savings, and step timings.

## Relevant configuration (worker env)

- `ZENPDF_COMPRESS_PROFILE=balanced`
- `ZENPDF_COMPRESS_AUTO_IMAGE_HEAVY=1`
- `ZENPDF_COMPRESS_USE_ZOPFLI=0`
- `ZENPDF_COMPRESS_GS_PASSTHROUGH_JPEG=0`
- `ZENPDF_COMPRESS_SAVINGS_THRESHOLD_PCT=0.08`
- `ZENPDF_COMPRESS_MIN_SAVINGS_BYTES=200000`
- `ZENPDF_COMPRESS_TIMEOUT_BASE_SECONDS=120`
- `ZENPDF_COMPRESS_TIMEOUT_PER_MB_SECONDS=3`
- `ZENPDF_COMPRESS_TIMEOUT_PER_PAGE_SECONDS=1.5`
- `ZENPDF_COMPRESS_TIMEOUT_MAX_SECONDS=900`
- `ZENPDF_COMPRESS_TIMEOUT_SECONDS=`
- `ZENPDF_COMPRESS_ENABLE_IMAGE_OPT=0`
- `ZENPDF_QPDF_OI_QUALITY=75`
- `ZENPDF_QPDF_OI_MIN_WIDTH=128`
- `ZENPDF_QPDF_OI_MIN_HEIGHT=128`
- `ZENPDF_QPDF_OI_MIN_AREA=16384`
- `ZENPDF_COMPRESS_ENABLE_PDFSIZEOPT=0`
- `ZENPDF_COMPRESS_ENABLE_JBIG2=0`
- `ZENPDF_COMPRESS_PDFSIZEOPT_ARGS=`

See `apps/worker/.env.example` for the full list of compression controls.
