# ZenPDF Architecture

## System Overview
- Web: Next.js (TypeScript) + Tailwind on Vercel.
- Auth: Clerk with Google sign-in only.
- Backend: Convex as system of record.
- Worker: Cloud Run container `pdf-worker` (Python) for heavy PDF processing.
- Desktop: C++23 + Qt 6 Widgets, independently runnable and entirely local-first.

## Repo Layout
- `apps/web`: Next.js app (Vercel root directory).
- `apps/worker`: Cloud Run worker for PDF processing.
- `apps/desktop`: native Arch/Wayland application with its own build and tests.
- `docs`: product and architecture docs.

## Local Self-Hosted Web Product
- Run the web app, Convex dev, and worker locally.
- Provide Docker Compose or scripts for a one-command local stack.

## Native Desktop Boundary

- Desktop state is local SQLite/preferences data under the platform application-data directory.
- PDF document content is never sent to the web app, Convex, the worker, or telemetry.
- PDF rendering and structural-editing engines are behind desktop-only adapters so risky work can move into bounded helper processes without coupling the web product.
- The first reader adapter uses Qt PDF's PDFium-backed widget API. Structural page operations invoke qpdf with argument arrays (never a shell), bounded inputs/time, cancellation, and atomic new-file publication.
- The root Omarchy plugin only launches the independently installed `zenpdf` binary and reports a missing binary without escalating privileges.

## Data Model (Convex)
- Users: tier and auth metadata.
- Jobs: tool type, status, progress, inputs, outputs, error codes.
- Artifacts: file metadata, storageId, TTL.
- Usage counters: per-user, per-anon, and global limits.
- Budget state: monthly cap, capacity status, heavy tool flags.
- Plan limits: config-driven tier caps with env overrides.
- Global limits: system-wide concurrency and job caps.

## Job Lifecycle
1. Client requests tool with inputs.
2. Convex validates tier limits and capacity.
3. Job is created with status `queued`.
4. Worker claims job via safe-claim mutation and updates progress.
5. Worker downloads inputs via Convex-generated URLs. Output upload URLs are issued only to the current lease owner and each upload is tracked as a pending record before bytes are sent. The output POST runs in a killable process under a hard deadline shorter than the pending-record recovery lifetime.
6. A returned storage ID is atomically fsynced to the worker recovery volume before registration. Registration and deletion are idempotent, retryable transitions; job completion atomically promotes registered pending uploads to artifacts, after which the worker removes the journal entry.
7. Downloads stream through a Next.js route that validates access.

## Storage
- Default: Convex File Storage.
- Optional: Cloudflare R2 via Convex component.
- TTL cleanup removes input/output after configured time.
- Browser uploads use server-issued, identity-bound, expiring reservations. Job creation validates and consumes those reservations while inserting normalized storage references in the same transaction.
- A scheduled, paginated `_storage` sweep protects job references, artifacts, live pending worker uploads, and live browser reservations. Legacy reference backfill must complete before deletion is possible.
- Destructive cleanup uses a durable candidate transition and a separate serialized final recheck/delete mutation. Tombstones reject late binding or registration. The scheduled action defaults to dry-run and requires an explicit operator flag for deletion.

## Capacity & Budget Controls
- Enforce per-user and global caps server-side.
- Heavy tools disabled first during budget pressure.
- Friendly errors returned for capacity or budget limits.
- No premium tier or premium-only tools.

## Error Mapping
Stable error codes are mapped to friendly UI messages:
- USER_LIMIT_FILE_TOO_LARGE
- USER_LIMIT_DAILY_JOBS
- USER_LIMIT_DAILY_MINUTES
- USER_INPUT_INVALID
- SERVICE_CAPACITY_TEMPORARY
- SERVICE_CAPACITY_MONTHLY_BUDGET

## Security
- Clerk-based auth for protected routes.
- Signed upload URLs for files.
- Least-privilege worker tokens.
- Run the worker container as non-root and enforce a restrictive seccomp/AppArmor policy (or Cloud Run sandboxing) for LibreOffice conversions.

## Testing & CI
- Unit: limits, job state machine, error mapping.
- Integration: Convex functions and worker with fixtures.
- E2E: core tools and Usage & Capacity page.
- CI: lint + unit + integration on PR; E2E on main/nightly.
