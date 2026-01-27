# ZenPDF Roadmap

Each epic is broken into PR-sized subtasks. Update this file as work ships.

## Epic 0 — Scaffold
- [x] 0.1 Project docs + planning files
- [x] 0.2 Repo scaffold (Next.js + Tailwind)
- [x] 0.3 Dossier UI theme baseline + landing shell
- [x] 0.4 Clerk Google sign-in wiring
- [x] 0.5 Convex setup (schema, jobs, artifacts, usage/budget tables)
- [x] 0.6 Usage & Capacity page skeleton
- [x] 0.7 CI pipeline + smoke tests

## Epic 1 — Core Job System
- [x] 1.1 Job status/progress model + TTL cleanup
- [x] 1.2 Safe claim + retries + idempotency
- [x] 1.3 Per-user and global caps with friendly errors

## Epic 2 — Standard Tools v1
- [x] 2.1 Merge, split, compress
- [x] 2.2 Rotate, remove, reorder
- [x] 2.3 Image <-> PDF, PDF <-> JPG

## Epic 3 — Conversions v1
- [ ] 3.1 Web to PDF
- [ ] 3.2 Office to PDF
- [ ] 3.3 PDF to office (non-OCR)

## Epic 4 — Editor & Utilities
- [ ] 4.1 Watermark, page numbers, crop
- [ ] 4.2 Unlock, protect
- [ ] 4.3 Redact, compare

## Epic 5 — Tiers Polish
- [ ] 5.1 ANON/FREE/PREMIUM UX clarity
- [ ] 5.2 Usage & Capacity page fully implemented

## Epic 6 — Premium
- [ ] 6.1 OCR conversions to Word/Excel
- [ ] 6.2 PDF/A conversion
- [ ] 6.3 Premium limits + ads-free flag

## Epic 7 — Workflows + Teams
- [ ] 7.1 Workflow compiler + presets
- [ ] 7.2 Teams with shared templates

## Epic 8 — Hardening
- [ ] 8.1 Security doc + logging/metrics
- [ ] 8.2 Streaming downloads + final QA
- [ ] 8.3 Release checklist
- [ ] 8.4 Local self-host guide + docker compose
