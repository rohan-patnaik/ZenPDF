# ZenPDF Agent Guide

This repository is designed for humans and autonomous agents working together.
Follow this guide to keep work safe, reviewable, and aligned with ZenPDF goals.

## Principles
- Keep main green and avoid breaking changes without coordination.
- Follow YAGNI, SOLID, DRY, and KISS.
- Never exceed free-tier budgets; enforce strict limits server-side.
- Prefer small, reviewable changes with clear tests.

## Workflow
- Break work into Epic -> Subtask -> PR-sized units.
- One PR per subtask whenever possible.
- Run lint/tests before requesting review.
- Document decisions in `/docs` when they affect product or architecture.

## Stack Locks
- Web: Next.js (TypeScript) + Tailwind.
- Auth: Clerk (Google sign-in only).
- Backend/DB: Convex.
- Worker: Cloud Run container with OSS PDF tooling.

## Safety & Compliance
- No secrets committed.
- Provide friendly, non-technical errors to end users.
- Expose capacity and limits via the Usage & Capacity page.
