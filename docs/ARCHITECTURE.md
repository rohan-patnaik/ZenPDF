# ZenPDF Architecture

## System Overview
- Web: Next.js (TypeScript) + Tailwind on Vercel.
- Auth: Clerk with Google sign-in only.
- Backend: Convex as system of record.
- Worker: Cloud Run container `pdf-worker` (Python) for heavy PDF processing.

## Repo Layout
- `apps/web`: Next.js app (Vercel root directory).
- `apps/worker`: Cloud Run worker for PDF processing.
- `docs`: product and architecture docs.

## Local Self-Hosted
- Run the web app, Convex dev, and worker locally.
- Provide Docker Compose or scripts for a one-command local stack.
- Desktop installers are out of scope.

## Data Model (Convex)
- Users: tier, ads-free flag, auth metadata, org membership.
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
5. Worker downloads inputs via Convex-generated URLs and uploads outputs.
6. Job completes or returns a stable error code with friendly message.
7. Downloads stream through a Next.js route that validates access.

## Storage
- Default: Convex File Storage.
- Optional: Cloudflare R2 via Convex component.
- TTL cleanup removes input/output after configured time.

## Capacity & Budget Controls
- Enforce per-user and global caps server-side.
- Heavy tools disabled first during budget pressure.
- Friendly errors returned for capacity or budget limits.
- Premium tier access is allowlisted via ZENPDF_PREMIUM_EMAILS / ZENPDF_PREMIUM_CLERK_IDS.

## Error Mapping
Stable error codes are mapped to friendly UI messages:
- USER_LIMIT_FILE_TOO_LARGE
- USER_LIMIT_DAILY_JOBS
- USER_LIMIT_DAILY_MINUTES
- USER_INPUT_INVALID
- USER_LIMIT_PREMIUM_REQUIRED
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
- E2E: core tools, premium gating, Usage & Capacity page.
- CI: lint + unit + integration on PR; E2E on main/nightly.
