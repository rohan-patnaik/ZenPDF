# ZenPDF Operations

This document is the single operational runbook for local runs, deploys, security, and monitoring.

## Runtime requirements
- Node.js 20+
- Python 3.11+
- Docker Desktop (optional)
- Chromium (for `html-to-pdf` browser mode)
- LibreOffice, Ghostscript, qpdf, mutool, OCR tooling (installed in worker image)

## Environment basics
### Web (`apps/web/.env.local`)
- Clerk keys and JWT issuer/audience
- Convex URL (`NEXT_PUBLIC_CONVEX_URL`)
- `ZENPDF_WORKER_TOKEN`
- Optional donate/feedback env vars as needed

### Worker (`apps/worker/.env`)
- `ZENPDF_CONVEX_URL`
- `ZENPDF_WORKER_TOKEN`
- Optional runtime knobs:
  - `ZENPDF_BROWSER_PATH`
  - `ZENPDF_WEB_ALLOW_INSECURE_SSL=1` (dev only)
  - `ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK=1` (dev only)
  - `ZENPDF_OCR_PROFILE` (`fast|balanced|accurate`)
  - `ZENPDF_OFFICE_TIMEOUT_BASE_SECONDS`
  - `ZENPDF_OFFICE_TIMEOUT_PER_MB_SECONDS`
  - `ZENPDF_OFFICE_TIMEOUT_MAX_SECONDS`
  - `ZENPDF_JOB_WALL_SECONDS` (hard wall clock for an entire tool run; default 600)
  - `ZENPDF_JOB_CPU_SECONDS` (kernel CPU limit for a tool process; default 300)
  - `ZENPDF_JOB_MEMORY_BYTES` (tool-process address-space limit; default 4 GiB)
  - `ZENPDF_JOB_OUTPUT_BYTES` (tool-process file-size limit; default 2 GiB)
  - `ZENPDF_HEARTBEAT_RETRIES` and `ZENPDF_HEARTBEAT_RETRY_SECONDS`

## Local run
1. `cd apps/web && npx convex dev`
2. `cd apps/web && npm install && npm run dev`
3. `cd apps/worker && python -m pip install -r requirements.txt && python main.py`

App URL: `http://localhost:3000`

## Docker compose local stack
- Export `NEXT_PUBLIC_CONVEX_URL` and `NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY` in the invoking shell (Next.js embeds these public values at build time), then run `docker compose up --build`.
- Use host Convex URLs in env when running in containers:
  - `NEXT_PUBLIC_CONVEX_URL=http://host.docker.internal:3210`
  - `ZENPDF_CONVEX_URL=http://host.docker.internal:3210`

## Release checklist
### Pre-release
- `cd apps/web && npm run lint && npm test`
- `cd apps/worker && pytest`
- Confirm worker can process representative files for all major tool categories.
- Verify env values in deploy targets.

### Deploy
- Web: Vercel
- Worker: Cloud Run container
- Backend: Convex deployment with matching env values

### Post-release
- Run smoke tests: upload -> process -> download across core tools.
- Validate PDF/A toolResult reporting in UI.
- Monitor failures, queue latency, and worker health for 24h.

## Security baseline
- Use least-privilege secrets in Vercel/Convex/Cloud Run secret managers.
- Never commit secrets.
- Worker runs as non-root.
- Do not log file contents or PII.
- Enforce SSRF guardrails in HTML-to-PDF (public-network only, redirect restrictions).
- Each tool executes in its own process group with CPU, memory, output, core-dump, and wall-clock limits. Lease loss terminates that group and prevents stale upload/completion.
- OCR calls must support a per-call timeout; legacy pytesseract APIs are rejected instead of falling back to an unbounded invocation.

## Observability baseline
### Log fields
- `jobId`, `tool`, `tier`, `requestId`, status transitions, stable error code.

### Metrics
- Jobs created/completed/failed by tool
- Queue time, processing time, total duration
- Worker retries/concurrency
- Capacity and budget pressure signals

### Alerts
- Failure rate > 5% sustained
- Queue latency > 2x baseline
- Crash loops / zero active workers
- Budget threshold pressure

## Incident response
1. Triage by error code, tool, and requestId correlation.
2. If abuse/budget spike occurs, disable heavy tools first.
3. Rotate exposed credentials/tokens immediately.
4. Restore service and capture follow-up actions in an incident note.
