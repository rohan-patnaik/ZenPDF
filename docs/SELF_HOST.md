# Local Self-Host Guide

## Requirements
- Node.js 20+
- Python 3.11+
- Docker Desktop
- Clerk application for auth
- Convex project for the backend

## Configure environment files
1. Copy `apps/web/.env.example` to `apps/web/.env.local` and fill in values.
2. Copy `apps/worker/.env.example` to `apps/worker/.env` and fill in values.

When running with Docker, point Convex URLs at the host:
- `NEXT_PUBLIC_CONVEX_URL=http://host.docker.internal:3210`
- `ZENPDF_CONVEX_URL=http://host.docker.internal:3210`

Ensure `ZENPDF_WORKER_TOKEN` matches in both env files.

## Start Convex locally
```bash
cd apps/web
npx convex dev
```

## Run with Docker Compose
```bash
cd /path/to/zenpdf
docker compose up --build
```

## Access the app
- Web UI: <http://localhost:3000>
- Convex dashboard: <http://localhost:3210>

## Notes
- Local development bypass is enabled when `ZENPDF_DEV_MODE=1` and the web app runs in development mode, which disables plan/capacity checks for local runs.
- ZenPDF has no premium-only tools or premium account allowlists.
- For production self-hosting, run `npm run build` and `npm run start` in `apps/web`.
