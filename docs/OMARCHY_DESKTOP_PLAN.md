# ZenPDF Omarchy desktop plan

## Product boundary

ZenPDF Desktop is a private, offline-first PDF workspace for Omarchy Quattro. It extends this repository's existing web tools; it does not remove them. Literal full parity with current Acrobat Pro is not a credible near-term claim because Acrobat includes decades of proprietary editing behavior, print production, accessibility, standards, collaboration, signatures, JavaScript, and enterprise integrations. Progress is measured against a public parity matrix and fixture-driven workflows.

Adobe AI Assistant, PDF Spaces, Express templates, Adobe cloud sharing, account services, and other proprietary cloud workflows are excluded. Local equivalents may be proposed later only as separate, privacy-preserving features.

## Architecture

- Native C++23/Qt 6 desktop app under `apps/desktop`, optimized for Arch/Wayland.
- Qt Widgets for mature document tabs, docking, accessibility, printing, and input handling.
- PDFium for rendering and low-level page inspection; qpdf for structural transformations and encryption; dedicated adapters keep engines replaceable.
- Tesseract/Leptonica for offline OCR; LibreOffice integration is optional and isolated for office conversions.
- RAII, QObject parent ownership, smart pointers, bounded caches, and background jobs with cancellation.
- SQLite for recent files, preferences, indexes, autosave metadata, and audit logs; document content stays in user-selected files.
- Sandboxed or reduced-privilege helper processes for high-risk parsing and conversions where Omarchy supports them.
- Root Omarchy `menu` plugin is a thin launcher/integration surface; the native app remains independently runnable.

## Milestones

### M0 — native foundation and Omarchy integration

- Desktop app skeleton, multi-document workspace, theme bridge, logging, settings, CI, packaging skeleton.
- Root manifest/launcher with clear missing-binary diagnostics and no install-time privilege escalation.
- Security model, threat model, fixture corpus, crash recovery design, performance budgets.

### M1 — reader and organizer

- Open/render/search/navigation, thumbnails, bookmarks, attachments, metadata, recent files.
- Zoom/fit/rotate, continuous/single/two-page views, presentation mode, print.
- Insert/delete/reorder/rotate/crop/extract/replace pages; merge/split; undo/redo.
- Form viewing and basic AcroForm filling; annotations preserved on save.

### M2 — review and annotation

- Highlight/underline/strikeout, text notes, free text, ink, shapes, stamps using original assets.
- Comment list, filters, replies/status, import/export of standard annotation data where interoperable.
- Measurement tools, compare visual/text changes, snapshot/copy with permission checks.
- Full keyboard operation, screen-reader labels, high contrast, reflow experiments.

### M3 — content editing and creation

- Edit/add/move text and images with font substitution warnings and explicit fidelity limits.
- Headers/footers, page numbers, backgrounds, watermarks, links, destinations, bookmarks.
- Scan/import images, deskew/denoise, searchable OCR layer, searchable PDF export.
- Create PDFs from images/text/clipboard/print flow; standards-aware export presets.

### M4 — forms, signatures, and protection

- AcroForm field creation/editing, calculation/validation subset, flattening, import/export data.
- Local signature appearance, certificate-based signing, validation, timestamp integration, audit details.
- Password and certificate encryption, permissions, sanitize, metadata removal.
- True redaction: mark, review, apply, verify content removal, and regression fixtures against recovery.

### M5 — professional and accessibility workflows

- Preflight profiles, PDF/A and PDF/X validation/conversion, output intents, separations/ink preview.
- Object inspector, fixups, flatten transparency, overprint preview, production mark controls.
- Tags tree, reading order, alt text, language, table/header semantics, accessibility checker.
- Portfolio/package support, batch/action wizard, watched folders, stable CLI.

### M6 — compatibility and release quality

- Broad encrypted, damaged, signed, tagged, form-heavy, long, and large-file corpus.
- Parser/converter fuzzing, decompression-bomb limits, malicious attachment handling, safe URL policy.
- Reproducible Arch package/AppImage, signed checksums, SBOM, upgrade/migration policy.
- Omarchy validation on a real Quattro machine and marketplace screenshots/submission.

## Parity matrix

Create `docs/ACROBAT_PARITY.md` with one row per documented workflow and these states: `not-started`, `partial`, `verified`, `excluded`, or `blocked-by-standard/licensing`. A capability reaches `verified` only with fixtures, undo/save/reopen coverage where relevant, accessibility behavior, failure handling, and documentation.

## Definition of done per feature

Every feature requires focused tests, hostile/corrupt input behavior, cancellation/resource bounds for background work, keyboard/accessibility handling, documentation, and a coherent commit pushed after CI passes.

