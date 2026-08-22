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
  - `ZENPDF_TOOL_PROCESS_JOIN_SECONDS` (per-stage tool TERM/KILL join bound; default 2, hard maximum 5)
  - `ZENPDF_HEARTBEAT_RETRIES` and `ZENPDF_HEARTBEAT_RETRY_SECONDS`
  - `ZENPDF_UPLOAD_DEADLINE_SECONDS` (hard total output POST deadline; default 60)
  - `ZENPDF_UPLOAD_PROCESS_JOIN_SECONDS` (per-stage TERM/KILL join bound; default 1, hard maximum 5)
  - `ZENPDF_UPLOAD_JOURNAL_DIR` (durable recovery volume; default `/var/lib/zenpdf-worker/upload-recovery`)
  - `ZENPDF_UPLOAD_JOURNAL_MAX_ENTRIES`, `ZENPDF_UPLOAD_JOURNAL_MAX_BYTES`, and `ZENPDF_UPLOAD_JOURNAL_MAX_ENTRY_BYTES` (hard recovery-spool limits; defaults 1024, 8 MiB, and 4 KiB)
  - `ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_ENTRIES`, `ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_BYTES`, and `ZENPDF_UPLOAD_JOURNAL_SCAN_MAX_MS` (hard whole-directory inspection budgets; defaults 2048, 16 MiB, and 100 ms)
  - `ZENPDF_UPLOAD_JOURNAL_TRANSIENT_CLEANUP_BATCH` and `ZENPDF_UPLOAD_JOURNAL_TRANSIENT_STALE_SECONDS` (bounded cleanup for stale, exact-grammar, private, single-link, same-UID probe/temp remnants whose contents authenticate their purpose; defaults 32 and 300)
  - `ZENPDF_UPLOAD_RECOVERY_BATCH_SIZE` (maximum entries attempted per poll; default 32)
  - `ZENPDF_UPLOAD_RETRY_BASE_MS` and `ZENPDF_UPLOAD_RETRY_MAX_MS` (persisted exponential retry bounds; defaults 1000 and 300000)
  - `ZENPDF_UPLOAD_SHUTDOWN_GRACE_SECONDS` and `ZENPDF_UPLOAD_SHUTDOWN_MAX_OPERATIONS` (hard graceful-drain bounds; defaults 30 and 64)

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
- Mount `ZENPDF_UPLOAD_JOURNAL_DIR` on storage that survives worker instance replacement. The Compose stack provides the `worker-upload-recovery` named volume; a production deployment must provide an equivalent durable mount.
- Treat worker exit code 70 as a forced supervisor/cgroup restart request. It means a tool or upload child remained alive (or its state became uncertain) after bounded TERM and KILL joins. Dedicated restart state immediately unwinds the active workflow and prevents any later claim, failure report, registration, or completion. If an upload storage ID already reached the parent, its `register` intent is fsynced first; the forced-exit drain performs only bounded local journal validation and makes no backend transition. Unresolved and unreadable records remain on the durable volume, then the worker uses immediate exit so Python cannot wait indefinitely for a non-daemon child; the supervisor must terminate the whole container/cgroup before restart.
- Resolve the remaining unknown-storage-ID release decision in `docs/UPLOAD_ORPHAN_DECISION.md`; the worker journal cannot identify an object when the upload response never reaches the parent process.

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
- Each tool executes in its own process group with CPU, memory, output, core-dump, and wall-clock limits. Lease loss or shutdown sends TERM then KILL under bounded joins; an uncertain live handle is retained until the supervisor terminates the container/cgroup. The same cancellation applies to an in-flight output process even if the HTTP client ignores close. Upload URLs require current lease ownership. Returned storage IDs are fsynced before registration, and registration/deletion intent is retried from the durable journal until the server confirms the transition.
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
