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
  - Output: job appears in Recent jobs; status goes queued → running → succeeded; output row appears with Download button.

### Downloads
- Output download
  - Input: click Download on a completed job.
  - Output: file downloads via `/api/download`, correct filename and size shown.

### Usage & Capacity
- Usage page
  - Input: visit `/usage-capacity`.
  - Output: current tier, plan limits, and usage bars update after jobs run.

### Workflows (Premium)
- Create workflow
  - Input: name + steps + required config.
  - Output: workflow saved and listed.
- Validation
  - Input: missing required fields or incompatible tool chain.
  - Output: validation error; workflow not saved.

### Teams (Premium)
- Create team
  - Input: name.
  - Output: team created; you are owner.
- Add/remove member
  - Input: member email.
  - Output: membership updated; cannot remove owner.

## Tool-by-tool expectations

### Merge PDFs
- Input: 2+ PDFs.
- Output: `originalname_merged.pdf`.

### Split PDF
- Input: 1 PDF, optional `ranges` (e.g., `1-3,5`).
- Output: `split_output.zip` (multiple PDFs).

### Compress PDF
- Input: 1 PDF.
- Output: `originalname_compressed.pdf` (usually smaller; may not shrink if already optimized).

### Rotate pages
- Input: 1 PDF, `angle` (90/180/270), optional `pages`.
- Output: `originalname_rotated.pdf`.

### Remove pages
- Input: 1 PDF, `pages` (e.g., `2,5-6`).
- Output: `originalname_trimmed.pdf`.

### Reorder pages
- Input: 1 PDF, `order` (e.g., `3,1,2`).
- Output: `originalname_reordered.pdf`.

### Watermark
- Input: 1 PDF, `text`, optional `pages`.
- Output: `originalname_watermarked.pdf`.

### Page numbers
- Input: 1 PDF, optional `start`, optional `pages`.
- Output: `originalname_numbered.pdf`.

### Crop pages
- Input: 1 PDF, `margins` (top,right,bottom,left in points), optional `pages`.
- Output: `originalname_cropped.pdf`.

### Redact text
- Input: 1 PDF, `text`, optional `pages`.
- Output: `originalname_redacted.pdf`.

### Highlight text
- Input: 1 PDF, `text`, optional `pages`.
- Output: `originalname_highlighted.pdf`.

### Compare PDFs
- Input: 2 PDFs.
- Output: `originalname_compare.txt`.

### Unlock PDF
- Input: 1 PDF + current password.
- Output: `originalname_unlocked.pdf`.

### Protect PDF
- Input: 1 PDF + new password.
- Output: `originalname_protected.pdf`.

### Repair PDF
- Input: 1 PDF.
- Output: `originalname_repaired.pdf`.

### Image → PDF
- Input: 1+ images (PNG/JPG).
- Output: `originalname_images.pdf`.

### PDF → JPG
- Input: 1 PDF.
- Output: `pdf_pages.zip`.

### Web → PDF
- Input: URL (no file upload).
- Output: `web_to_pdf.pdf`.

### Office → PDF
- Input: DOCX/XLSX/PPTX.
- Output: `originalname_converted.pdf`.

### PDF → PDF/A (Premium)
- Input: 1 PDF.
- Output: `originalname_pdfa.pdf`.

### PDF → Text
- Input: 1 PDF.
- Output: `originalname_text.txt`.

### PDF → Word
- Input: 1 PDF.
- Output: `originalname_word.docx`.

### PDF → Word (OCR) (Premium)
- Input: 1 PDF.
- Output: `originalname_word_ocr.docx`.

### PDF → Excel
- Input: 1 PDF.
- Output: `originalname_excel.xlsx`.

### PDF → Excel (OCR) (Premium)
- Input: 1 PDF.
- Output: `originalname_excel_ocr.xlsx`.

## Premium + teams
- Premium gates for workflows and teams enforce access.
- Team member invites and removals update shared workflows.

## Error handling
- Friendly errors render for limits, invalid input, and capacity.
- Retry guidance appears for transient failures.

## Worker
- Worker claims jobs, updates progress, and uploads outputs.
- Artifacts expire based on TTL settings.

## Browsers
- Chrome, Safari, and mobile layouts render correctly.
- Keyboard navigation reaches all primary actions.
