# Owner Action Items (Current)

Last validated: 2026-02-23  
Owner: Rohan Patnaik

Use this file for remaining owner-level tasks before production hardening and open-source release.

## Production-critical

- [ ] Configure card checkout provider and set `NEXT_PUBLIC_DONATE_CARD_EMBED_URL`.
  - File: `apps/web/.env.local` and production web env.
  - Requirement: iframe-embeddable checkout URL and provider domain allowlist for local + production origins.

- [ ] Set donation identity via environment, not code defaults.
  - Keys:
    - `NEXT_PUBLIC_DONATE_PAYEE_NAME`
    - `NEXT_PUBLIC_DONATE_UPI_ID`
    - Optional: `NEXT_PUBLIC_DONATE_UPI_NOTE`

- [ ] Remove personal fallback donation values from source before release.
  - Current hardcoded defaults exist in `apps/web/src/components/DonateBookmark.tsx`.

- [ ] Configure feedback-board admins for production.
  - Keys:
    - `ZENPDF_FEEDBACK_ADMIN_CLERK_IDS`
    - `ZENPDF_FEEDBACK_ADMIN_EMAILS`
  - Behavior today: if both are empty, access to resolve feedback is only allowed in non-production.

- [ ] Run full smoke tests before final production merge.
  - Include upload -> process -> download across representative tools and formats.

- [ ] Confirm production env values are set in deployment targets.
  - Web: donation + feedback admin env keys.
  - Worker: browser/OCR/office/runtime keys required by current tool behavior.

## Runtime hardening

- [ ] Ensure Chromium is available in every worker runtime.
  - Local check: `chromium --version` or `google-chrome --version`.
  - Optional override: `ZENPDF_BROWSER_PATH`.
  - Note: project Dockerfile already installs Chromium; verify non-Docker runtimes separately.

- [ ] Decide PDF/A validation strategy for production.
  - Option A: install `veraPDF` in worker images for stricter conformance validation.
  - Option B: keep current structural fallback only.

- [ ] Use hostname fallback only for local/dev TLS issues.
  - Key: `ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK=1`
  - Scope: only with `ZENPDF_DEV_MODE=1`.

## Config tuning and docs alignment

- [ ] Tune OCR and office conversion knobs for expected workload.
  - OCR:
    - `ZENPDF_OCR_PROFILE`
    - `ZENPDF_OCR_TEXT_DENSITY_THRESHOLD`
    - `ZENPDF_OCR_PREPROCESS_THRESHOLD`
  - Office:
    - `ZENPDF_OFFICE_TIMEOUT_BASE_SECONDS`
    - `ZENPDF_OFFICE_TIMEOUT_PER_MB_SECONDS`
    - `ZENPDF_OFFICE_TIMEOUT_MAX_SECONDS`

- [ ] Add missing runtime knobs to `apps/worker/.env.example` for operator discoverability.
  - Add at least:
    - `ZENPDF_BROWSER_PATH`
    - `ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK`
    - `ZENPDF_WEB_ALLOW_INSECURE_SSL` (dev only)
    - `ZENPDF_OCR_PROFILE`
    - `ZENPDF_OCR_TEXT_DENSITY_THRESHOLD`
    - `ZENPDF_OCR_PREPROCESS_THRESHOLD`
    - `ZENPDF_OFFICE_TIMEOUT_BASE_SECONDS`
    - `ZENPDF_OFFICE_TIMEOUT_PER_MB_SECONDS`
    - `ZENPDF_OFFICE_TIMEOUT_MAX_SECONDS`

- [ ] Finalize donate FAB icon strategy.
  - Default currently uses `/icons/chai.png`.
  - Optional themed overrides:
    - `NEXT_PUBLIC_DONATE_ICON_LIGHT`
    - `NEXT_PUBLIC_DONATE_ICON_DARK`
  - Optional hosted QR override:
    - `NEXT_PUBLIC_DONATE_UPI_QR_URL`
