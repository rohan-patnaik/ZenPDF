# Final QA Checklist

## Core flows
- Sign in/out and anonymous sessions behave as expected.
- Tool selection, validation, upload, and job queueing succeed.
- Job status updates and progress indicators refresh correctly.
- Output downloads stream with the expected filename and size.
- Unauthorized or expired download requests are rejected; authorized downloads succeed.
- Usage & Capacity reflects real usage and limits.

## Feature test catalog

### Navigation & auth
- Start with a file
  - Input: click "Start with a file".
  - Output: `/tools` opens (or redirects to `/sign-in?redirect_url=/tools` when auth is enabled).
- Sign-in flow (Clerk)
  - Input: sign in with Google.
  - Output: returns to the original page; signed-in state appears in header.
- Dev auth bypass (local only)
  - Input: set `ZENPDF_DISABLE_AUTH=1`, restart dev server.
  - Output: protected pages load without sign-in.

### Tools: job lifecycle
- Queue a job
  - Input: select tool, upload file(s), fill required fields, click Queue job.
  - Output: job appears in Recent jobs; status goes queued -> running -> succeeded; output row appears with Download button.

### Downloads
- Output download
  - Input: click Download on a completed job.
  - Output: file downloads via `/api/download`, correct filename and size shown.

### Usage & Capacity
- Usage page
  - Input: visit `/usage-capacity`.
  - Output: current tier, plan limits, and usage bars update after jobs run.

## Tool-by-tool expectations

### Merge PDF
- Input: 2+ PDFs.
- Output: `*_merged.pdf`.

### Split PDF
- Input: 1 PDF, optional `ranges` (e.g., `1-3,5`).
- Output: `split_output.zip`.

### Compress PDF
- Input: 1 PDF.
- Output: `*_compressed.pdf` (may return no change when already optimized).

### PDF to Word
- Input: 1 PDF.
- Output: `*_word.docx`.

### PDF to PowerPoint
- Input: 1 PDF.
- Output: `*_powerpoint.pptx`.

### PDF to Excel
- Input: 1 PDF.
- Output: `*_excel.xlsx`.

### Word to PDF
- Input: `.doc`/`.docx`.
- Output: `*_word.pdf`.

### PowerPoint to PDF
- Input: `.ppt`/`.pptx`.
- Output: `*_powerpoint.pdf`.

### Excel to PDF
- Input: `.xls`/`.xlsx`.
- Output: `*_excel.pdf`.

### Edit PDF
- Input: 1 PDF + `operations` JSON.
- Output: `*_edited.pdf`.

### PDF to JPG
- Input: 1 PDF.
- Output: `<pdf_stem>.zip` containing `<pdf_stem>_1.jpg`, `<pdf_stem>_2.jpg`, ...

### JPG to PDF
- Input: 1+ images.
- Output: `*_images.pdf`.

### Sign PDF
- Input: 1 PDF + signature text.
- Output: `*_signed.pdf`.

### Watermark
- Input: 1 PDF + watermark text.
- Output: `*_watermarked.pdf`.

### Rotate PDF
- Input: 1 PDF, `angle`, optional `pages`.
- Output: `*_rotated.pdf`.

### HTML to PDF
- Input: URL.
- Output: `<domain>_<timestamp>_html.pdf`.

### Unlock PDF
- Input: 1 PDF; prompt for password only when the file is encrypted.
- Output: `*_unlocked.pdf`.

### Protect PDF
- Input: 1 PDF + new password.
- Output: `*_protected.pdf`.

### Organize PDF
- Input: 1 PDF + optional `order`, `delete`, `rotate` map.
- Output: `*_organized.pdf`.

### PDF to PDF/A
- Input: 1 PDF.
- Output: `*_pdfa.pdf`.

### Repair PDF
- Input: 1 PDF.
- Output: `*_repaired.pdf`.

### Page numbers
- Input: 1 PDF + optional `start`, `pages`.
- Output: `*_numbered.pdf`.

### Scan to PDF
- Input: 1+ images.
- Output: `*_scan.pdf`.

### OCR PDF
- Input: 1 PDF + optional OCR language.
- Output: `*_ocr.pdf`.

### Compare PDF
- Input: 2 PDFs.
- Output: `*_compare.txt`.

### Redact PDF
- Input: 1 PDF + text + optional pages.
- Output: `*_redacted.pdf`.

### Crop PDF
- Input: 1 PDF + margins + optional pages.
- Output: `*_cropped.pdf`.

## Error handling
- Friendly errors render for limits, invalid input, and capacity.
- Retry guidance appears for transient failures.

## Worker
- Worker claims jobs, updates progress, and uploads outputs.
- Artifacts expire based on TTL settings.

## Browsers
- Chrome, Safari, and mobile layouts render correctly.
- Keyboard navigation reaches all primary actions.
