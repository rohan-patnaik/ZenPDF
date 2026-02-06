# Release Checklist

## Pre-release
- Confirm Epic tasks are complete in `docs/ROADMAP.md`.
- Run web tests: `npm run lint` and `npm test` in `apps/web`.
- Run worker tests: `pytest` in `apps/worker`.
- Ensure Convex schema changes are deployed.
- Verify donation link/QR env variables are set (if enabled).

## Deploy
- Deploy the web app to Vercel.
- Deploy the worker container to Cloud Run.
- Verify Convex deployment and environment variables.

## Post-release
- Run smoke tests on core tools and downloads.
- Validate Usage & Capacity and tools pages.
- Monitor logs and metrics for 24 hours.
