# Release Checklist

## Pre-release
- Confirm Epic tasks are complete in `docs/ROADMAP.md`.
- Run web tests: `npm run lint` and `npm test` in `apps/web`.
- Run worker tests: `pytest` in `apps/worker`.
- Run fixture-based fidelity checks for all 27 tools (`apps/worker/tests/fixtures` matrix).
- Ensure Convex schema changes are deployed.
- Verify donation link/QR env variables are set (if enabled).
- Confirm docs are synced:
  - `docs/QA.md`
  - `docs/TOOL_TECHNIQUES.md`
  - `docs/IMPLEMENTATION_CHANGELOG.md`

## Deploy
- Deploy the web app to Vercel.
- Deploy the worker container to Cloud Run.
- Verify Convex deployment and environment variables.

## Post-release
- Run smoke tests on core tools and downloads.
- Validate mode-specific behavior for:
  - HTML to PDF (`browser` and `text`)
  - PDF to Word (`auto`, `text`, `ocr`)
  - PDF to Excel (`auto`, `table`, `text`, `ocr`)
- Validate Usage & Capacity and tools pages.
- Monitor logs and metrics for 24 hours.
