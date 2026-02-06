# Owner Action Items (Living Checklist)

Last updated: 2026-02-06
Owner: Rohan Patnaik

Use this file as the single source of truth for remaining manual/non-code tasks.
Add new items here whenever something is deferred.

## Payments and Donations

- [ ] Choose and set up a card payment provider for in-modal checkout.
  - Why: `Pay by card` needs a real hosted checkout/embed URL.
  - Note: Card processing without a gateway is not practical (PCI + banking rails). Card fees are unavoidable.

- [ ] Add card embed URL in web env.
  - File: `apps/web/.env.local`
  - Key: `NEXT_PUBLIC_DONATE_CARD_EMBED_URL`
  - Requirement: must be an iframe-embeddable checkout URL from your provider.

- [ ] Configure provider dashboard for embed usage.
  - Allowlist local/dev and production origins if provider requires domain allowlisting.
  - Verify checkout works on both desktop and mobile.

- [ ] Finalize OnlyChai page/account details.
  - Set these web env vars instead of committing personal payment IDs in git:
    - `NEXT_PUBLIC_DONATE_PAYEE_NAME`
    - `NEXT_PUBLIC_DONATE_UPI_ID`
  - Validate that users can complete payment successfully.

- [ ] Keep payment identifiers out of version control.
  - Deployment/CI should provide `NEXT_PUBLIC_DONATE_PAYEE_NAME` and `NEXT_PUBLIC_DONATE_UPI_ID`.
  - Do not commit real personal UPI/name values in tracked docs or source files.

## Donation UX Assets and Content

- [ ] Finalize production icon assets for donate FAB.
  - Expected paths:
    - `apps/web/public/icons/chai-fab-light.png`
    - `apps/web/public/icons/chai-fab-dark.png`
  - Keep square canvas, rounded-corner composition, and good contrast for both themes.

- [ ] (Optional) Use a custom hosted QR image instead of generated QR.
  - File: `apps/web/.env.local`
  - Key: `NEXT_PUBLIC_DONATE_UPI_QR_URL`
  - If not set, app generates QR locally from UPI URI.

## Runtime and Environment

- [ ] Add local dev fallback env for HTML-to-PDF if TLS handshake issues appear.
  - File: `apps/worker/.env`
  - Key: `ZENPDF_WEB_ALLOW_HOSTNAME_FALLBACK=1`
  - Scope: local/dev usage only with `ZENPDF_DEV_MODE=1`.

- [ ] Reinstall worker dependencies after requirement updates.
  - Command: `cd apps/worker && python3.11 -m pip install -r requirements.txt`
  - Note: `fpdf2` pin is now `2.8.4` to avoid install failure.

## Release and Process

- [ ] Before final production PR merge, run full smoke tests on all tools in local/dev.
  - Include upload -> process -> download for representative file types.

- [ ] Keep CodeRabbit pre-push review step in workflow.
  - Rule agreed: run CodeRabbit and address findings before push/PR updates.

- [ ] Confirm production env values in deployment targets.
  - Web: donation-related `NEXT_PUBLIC_*` keys.
  - Worker: runtime keys needed for web-to-pdf and OCR behavior.
