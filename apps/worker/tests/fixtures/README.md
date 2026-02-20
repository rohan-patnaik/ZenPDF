# Worker Test Fixtures

This folder tracks the expected-vs-actual fixture matrix for the 27 ZenPDF tools.

## Strategy
- Keep fixtures deterministic and lightweight.
- Prefer generated fixtures in tests for simple PDFs/images.
- Add static binary fixtures only when behavior cannot be reproduced deterministically at runtime.

## Matrix file
- `expected_matrix.json` defines baseline coverage targets and assertion signals per tool.

