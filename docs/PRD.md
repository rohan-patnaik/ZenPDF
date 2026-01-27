# ZenPDF Product Requirements

## Overview
ZenPDF is an open-source web app that delivers feature parity with iLovePDF's Standard and Premium toolsets (functionality only, no UI/branding parity). It runs on Next.js, Convex, and a Cloud Run worker to keep heavy PDF processing out of the web tier.

## Goals
- Provide a reliable, friendly PDF toolbox for individuals and teams.
- Offer clear plan limits and visible capacity status.
- Keep costs within free-tier budgets by enforcing strict limits.
- Preserve a tactile, editorial UI theme across core flows.

## Target Users
- Individuals who need occasional PDF utilities.
- Small teams sharing presets and workflows.
- OSS-friendly users who may run the stack locally.

## Core Features
Standard (limited):
- Merge, split, compress
- Image <-> PDF, PDF <-> JPG
- Office -> PDF, PDF -> Word/Excel (non-OCR)
- Rotate, remove pages, reorder
- Watermark, page numbers, crop
- Repair, unlock, protect
- Web to PDF
- Basic annotations (highlight text)
- Redact, compare, copy utility (PDF → Text)
- OCR to searchable PDF (limited)

Premium (feature-flagged, no billing yet):
- PDF to Word/Excel (OCR for scans)
- PDF/A conversion
- Workflows (multi-step presets)
- Teams (org + shared presets/templates)
- Higher limits and concurrency
- Ads-free flag (hide supporter banner)

Premium access is currently enabled through environment allowlists (no billing yet).

## Plans & Limits
- Tiers: ANON, FREE_ACCOUNT, PREMIUM.
- Limits are config-driven via Convex tables and env overrides.
- Enforced server-side for every job and tool.
- Budget controls disable heavy tools first, then respond with friendly errors.

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
- USER_LIMIT_DAILY_JOBS
- USER_LIMIT_DAILY_MINUTES
- USER_INPUT_INVALID
- USER_SESSION_REQUIRED
- USER_LIMIT_PREMIUM_REQUIRED
- SERVICE_CAPACITY_TEMPORARY
- SERVICE_CAPACITY_MONTHLY_BUDGET

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
- Payment integration (supporter mode may be added later).
