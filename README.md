# ZenPDF

ZenPDF is an open-source PDF tools web app with a Next.js frontend, Convex backend, and Python worker.

## Live app
- https://thezenpdf.vercel.app

## Preview

![ZenPDF homepage](docs/images/homepage-preview.png)

## Tool scope (27)
- Merge PDF, Split PDF, Compress PDF
- PDF to Word, PDF to PowerPoint, PDF to Excel
- Word to PDF, PowerPoint to PDF, Excel to PDF
- Edit PDF, PDF to JPG, JPG to PDF
- Sign PDF, Watermark, Rotate PDF, HTML to PDF
- Unlock PDF, Protect PDF, Organize PDF
- PDF to PDF/A, Repair PDF, Page numbers
- Scan to PDF, OCR PDF, Compare PDF, Redact PDF, Crop PDF

## Repository layout
- `apps/web`: Next.js app (UI, API routes, Convex client)
- `apps/worker`: background worker for PDF processing
- `docs`: product, architecture, feature internals, and operations

## Quick start (local)
1. Copy environment files:
   - `apps/web/.env.example` -> `apps/web/.env.local`
   - `apps/worker/.env.example` -> `apps/worker/.env`
2. Ensure `ZENPDF_WORKER_TOKEN` matches in both env files.
3. Start Convex:
   - `cd apps/web && npx convex dev`
4. Start web app:
   - `cd apps/web && npm install && npm run dev`
5. Start worker:
   - `cd apps/worker && python -m pip install -r requirements.txt && python main.py`

Open `http://localhost:3000`.

## Core docs
- Product scope: `docs/PRD.md`
- System design: `docs/ARCHITECTURE.md`
- Per-feature internal logic: `docs/FEATURE_LOGIC.md`
- Security, deploy, monitoring, self-host ops: `docs/OPERATIONS.md`
- Contributor workflow: `CONTRIBUTING.md`
