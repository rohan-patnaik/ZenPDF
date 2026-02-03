#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

PYTHON=""
if command -v python3.11 >/dev/null 2>&1; then
  PYTHON="python3.11"
elif command -v python3 >/dev/null 2>&1; then
  PYTHON="python3"
elif command -v python >/dev/null 2>&1; then
  if python --version 2>&1 | grep -q "Python 3"; then
    PYTHON="python"
  else
    echo "Error: no Python 3 interpreter found. Install Python 3.11+ and ensure it's on PATH." >&2
    exit 1
  fi
else
  echo "Error: no Python interpreter found. Install Python 3.11+ and ensure it's on PATH." >&2
  exit 1
fi

pids=()

cleanup() {
  for pid in "${pids[@]:-}"; do
    if kill -0 "$pid" >/dev/null 2>&1; then
      kill "$pid" >/dev/null 2>&1 || true
    fi
  done
}

trap cleanup EXIT INT TERM

start() {
  local name="$1"
  shift
  echo "Starting ${name}..."
  "$@" &
  pids+=("$!")
}

start convex bash -lc "cd apps/web && npx convex dev"
# Force webpack to avoid Turbopack root confusion when multiple lockfiles exist.
start web bash -lc "cd apps/web && npm run dev -- --webpack"
start worker bash -lc "cd apps/worker && set -a && . .env && set +a && ${PYTHON} -m watchfiles --filter python '${PYTHON} main.py' zenpdf_worker main.py"

wait
