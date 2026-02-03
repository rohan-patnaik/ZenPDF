# ZenPDF Tool Techniques

Last Updated: 2026-02-03  
Version: 1.1

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
- Compress PDF — Technique: staged pipeline with fallbacks.
The pipeline order is: normalize/repair, lossless-ish optimize, optional image optimization, optional pdfsizeopt/JBIG2, optional Ghostscript, then smallest-valid output selection.
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

- Normalize/repair — Preferred: `mutool clean`; fallback: `qpdf --object-streams=generate --compress-streams=y --recompress-flate`; final fallback: pypdf rewrite.
- Lossless-ish optimize — `qpdf` object streams + recompress flate, plus `mutool merge -O compress` stream compression.
- Image optimization (optional, lossy) — `qpdf --optimize-images --jpeg-quality=<quality>` with min-width/height/area thresholds.
- Heavy pipeline (optional, lossy) — `pdfsizeopt` with optional `jbig2enc` for bi-level images.
- Ghostscript (optional, lossy) — run only if size threshold + savings threshold not met; probe run guards timeouts; preset defaults to `/ebook`.
- Output selection — pick smallest valid output; return `no_change` if savings < threshold.

Decision Rationale:
- Start with repairs to handle malformed PDFs before expensive compression.
- Use lossless optimization first to avoid quality loss when possible.
- Run heavier/losssy stages only when savings are meaningful.

Trade-offs:
- More stages increases complexity and intermediate file I/O.
- Aggressive compression can reduce fidelity, especially for image-heavy or scanned PDFs.

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
- Ghostscript is guarded by a probe run and skips full compression if the estimate is too slow.
- The worker returns a structured compression result to the UI: status, method, savings, and step timings.

## Relevant configuration (worker env)

- `ZENPDF_COMPRESS_ENABLE_IMAGE_OPT=1`
- `ZENPDF_COMPRESS_ENABLE_PDFSIZEOPT=1`
- `ZENPDF_COMPRESS_ENABLE_JBIG2=1`
- `ZENPDF_QPDF_OI_QUALITY=75`
- `ZENPDF_COMPRESS_GS_PRESET=ebook`
- `ZENPDF_COMPRESS_MIN_SAVINGS_PERCENT=1.0`

See `apps/worker/.env.example` for full list of compression controls.
