# Contributing to ZenPDF

This guide is for developers joining ZenPDF with zero prior context.

## Prerequisites
- Node.js 20+
- Python 3.11+
- Docker Desktop (optional, for compose-based local stack)
- Convex account/project
- Clerk application (Google sign-in)

## Local setup
1. Copy env templates:
   - `apps/web/.env.example` -> `apps/web/.env.local`
   - `apps/worker/.env.example` -> `apps/worker/.env`
2. Set the same `ZENPDF_WORKER_TOKEN` in both files.
3. Start services:
   - Convex: `cd apps/web && npx convex dev`
   - Web: `cd apps/web && npm install && npm run dev`
   - Worker: `cd apps/worker && python -m pip install -r requirements.txt && python main.py`

## Test and quality gates
Run before opening a PR.

- Web lint: `cd apps/web && npm run lint`
- Web tests: `cd apps/web && npm test`
- Worker tests: `cd apps/worker && pytest`

## Change expectations
- Keep the 27-tool scope intact unless intentionally changing product scope.
- Enforce limits server-side (web/Convex/worker paths), not only in UI.
- Do not commit secrets or personal payment identifiers.
- Keep error messages user-friendly and non-technical.

## If you change tool behavior
Update all of the following in the same PR:
- Worker dispatch and implementation:
  - `apps/worker/zenpdf_worker/worker.py`
  - `apps/worker/zenpdf_worker/tools.py`
- Tools UI config:
  - `apps/web/src/app/tools/page.tsx`
- Worker tests:
  - `apps/worker/tests/test_tools.py`
- Feature docs:
  - `docs/FEATURE_LOGIC.md`

## Pull request checklist
- Scope is small and reviewable.
- Tests pass locally.
- Backward compatibility and user-facing errors are considered.
- Docs are updated when behavior changes.
