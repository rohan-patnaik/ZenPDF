# ZenPDF Product Requirements

## Overview
ZenPDF is an open-source web app that delivers feature parity with iLovePDF's toolset (functionality only, no UI/branding parity). It runs on Next.js, Convex, and a Cloud Run worker to keep heavy PDF processing out of the web tier.

## Goals
- Provide a reliable, friendly PDF toolbox aligned to the iLovePDF tool catalog.
- Offer clear plan limits and visible capacity status.
- Keep costs within free-tier budgets by enforcing strict limits.
- Preserve a tactile, editorial UI theme across core flows.

## Target Users
- Individuals who need occasional PDF utilities.
- Power users who need predictable tool behavior and file naming.
- OSS-friendly users who may run the stack locally.

## Core Features
ZenPDF exposes a strict 27-tool parity set:
- Merge PDF, Split PDF, Compress PDF
- PDF to Word, PDF to PowerPoint, PDF to Excel
- Word to PDF, PowerPoint to PDF, Excel to PDF
- Edit PDF, PDF to JPG, JPG to PDF
- Sign PDF, Watermark, Rotate PDF, HTML to PDF
- Unlock PDF, Protect PDF, Organize PDF
- PDF to PDF/A, Repair PDF, Page numbers
- Scan to PDF, OCR PDF, Compare PDF, Redact PDF, Crop PDF

All tools are available across plans. There are no premium-only tools.

## Plans & Limits
- Tiers: ANON and FREE_ACCOUNT.
- Limits are config-driven via Convex tables and env overrides.
- Enforced server-side for every job and tool.
- Per-user/per-anon plan limits cap daily jobs/minutes for the current actor.
- Global pooled limits cap aggregate daily jobs/minutes and concurrent jobs across all actors.
- Monthly budget caps track aggregate platform cost over the current month.
- Enforcement order: validate user-level constraints first, then global pooled daily capacity/concurrency, then monthly budget checks before enqueueing.
- Budget controls disable heavy tools first, then respond with friendly errors.

## Heavy Tool Definition
- Heavy tools are OCR PDF (`ocr-pdf`) and PDF/A conversion (`pdfa`).
- A tool is classified as heavy when it has materially higher compute time/cost than baseline utilities (for example, OCR inference passes or archival conversion pipelines).
- When monthly budget pressure increases, heavy tools are paused first while core tools remain available.

## Usage & Capacity Page
- Non-technical descriptions of limits and capacity state.
- Progress bars: jobs/day, max file size, max files/job, heavy tools availability.
- Service status: Available, Limited, At Capacity.
- Examples of friendly errors and next steps.
- Guidance for running ZenPDF locally.

## Offline & Self-Hosted
- Provide a local self-host option via git clone + local services.
- Use Docker Compose or scripts to run Next.js, Convex, and the worker.
- A packaged desktop app (exe) is out of scope.

## Friendly Error Catalog
- USER_LIMIT_FILE_TOO_LARGE
- USER_LIMIT_SIZE_REQUIRED
- USER_LIMIT_DAILY_JOBS (per-user/per-anon daily jobs cap reached)
- USER_LIMIT_DAILY_MINUTES (per-user/per-anon daily processing-minutes cap reached)
- USER_INPUT_INVALID
- USER_SESSION_REQUIRED
- SERVICE_CAPACITY_TEMPORARY (global pooled daily jobs/minutes/concurrency reached, or heavy tools paused)
- SERVICE_CAPACITY_MONTHLY_BUDGET (global monthly budget/cost cap reached)

Each error must include a plain-language explanation and what to do next.

## UX Direction
- Tactile Corporate Dossier theme.
- Warm antique beige paper texture background.
- Paper-on-paper layering with soft multi-layer shadows.
- Deep forest green for CTAs and hero accents.
- High-contrast serif headings + readable printed body font.
- Heavy corner rounding and subtle hand-sketched icon style.

## Non-Goals
- Mobile app.
- On-device heavy PDF processing.
- Subscription billing or premium-only feature gating.
