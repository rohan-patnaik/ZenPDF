#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

npx concurrently -k -n convex,web,worker \
  "cd apps/web && npx convex dev" \
  "cd apps/web && npm run dev" \
  "cd apps/worker && set -a && . .env && set +a && python -m watchfiles --filter python 'python main.py' zenpdf_worker main.py"
