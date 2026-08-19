# ZenPDF Desktop capability matrix

This matrix tracks ZenPDF Desktop against documented professional PDF workflows. It is a delivery checklist, not a claim of Adobe Acrobat compatibility. Product names are used only to describe interoperability targets.

Allowed states are `not-started`, `partial`, `verified`, `excluded`, and `blocked-by-standard/licensing`. A workflow becomes `verified` only after fixture-driven tests cover save/reopen and undo where applicable, keyboard and accessibility behavior, resource/error handling, and user documentation.

## M0 — foundation

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Native Arch/Wayland application launch | partial | Native target and desktop metadata exist; packaged launch on Omarchy Quattro remains. |
| Multi-document tabbed workspace | partial | Movable/closable tabs exist; document lifecycle and recovery tests remain. |
| System theme and high-contrast bridge | partial | Uses the native Qt platform theme; portal and real high-contrast coverage remain. |
| Persistent local preferences | partial | Window state persists; preference migration and corrupt-settings coverage remain. |
| Structured local diagnostics | partial | Bounded local log is implemented; privacy/error-category tests remain. |
| Crash/session recovery | not-started | Requires atomic journal fixtures and interrupted-write tests. |
| Omarchy menu launch | partial | Launcher reports a missing binary via notification/stderr; device validation remains. |
| Arch package and portable bundle | partial | Development PKGBUILD exists; reproducibility, SBOM, and install/remove tests remain. |

## M1 — reader and organizer

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Open a local PDF | partial | Local open, password retry, drag/drop, duplicate detection, 2 GiB rejection, invalid input, and encrypted fixtures exist; broader hostile corpus remains. |
| Render and scroll pages | partial | Qt PDF continuous rendering works; long-document corpus and render-process isolation remain. |
| Search text | partial | Search results and navigation work; Unicode/no-text fixtures and Qt 6.6+ in-view highlighting remain. |
| Page thumbnails | partial | Lazy 128-pixel thumbnails use a 32 MiB cache; long-document and cancellation coverage remain. |
| Outline/bookmark navigation | partial | Native outline tree navigation works; malformed/nested fixtures remain. |
| Attachment viewing/extraction | not-started | Extraction requires explicit destination and hostile-name handling. |
| Document metadata inspection | partial | Standard fields distinguish absent values; malformed metadata fixtures remain. |
| Recent local files | partial | Bounded SQLite store, menu, missing-file error, and clear behavior exist; privacy UI test remains. |
| Page number navigation | partial | Bounded spin control, labels, previous/next, and result jumps exist; UI automation remains. |
| Zoom, actual size, fit width/page | partial | 25–500% zoom plus fit width/page and shortcuts exist; actual-size preset and UI tests remain. |
| Rotate view | not-started | Rotation must not silently modify the source. |
| Continuous, single, and two-page views | partial | Continuous and single-page modes exist; two-page and position regression coverage remain. |
| Presentation/full-screen reading | partial | F11 full-screen toggle exists; Escape, screen selection, and compositor tests remain. |
| Print | partial | Local range printing is capped at 100 pages and 2048-pixel renders. Cancellation is honored between synchronous Qt PDF page renders; in-render cancellation, permission handling, and real printer/backend tests remain. |
| Insert pages | not-started | Requires undo plus save/reopen fixture. |
| Delete pages | not-started | Prevent empty output and require undo. |
| Reorder pages | not-started | Requires drag/keyboard paths and undo. |
| Persistently rotate pages | partial | qpdf creates a separate rotated output with range validation/cancellation; UI and reopen fixtures remain. |
| Crop pages | not-started | Preserve boxes not intentionally changed. |
| Extract pages | partial | qpdf extraction validates ranges, refuses source overwrite, and is fixture-tested in `apps/desktop/tests/QpdfOperationsTest.cpp`; permission fixtures remain. |
| Replace pages | not-started | Requires page-box and annotation regression coverage. |
| Merge documents | partial | qpdf merge is bounded, cancellable, atomic, and fixture-tested; forms/outlines/mixed-box corpus remains. |
| Split document | not-started | Cover ranges, naming collisions, and atomic outputs. |
| Organizer undo/redo | not-started | Commands must be deterministic and bounded. |
| View AcroForms | not-started | Cover standard field types and appearance streams. |
| Fill basic AcroForms | not-started | Requires save/reopen and appearance regeneration coverage. |
| Preserve annotations on save | not-started | Fixture comparison required. |

## M2 — review and accessibility

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Highlight, underline, and strikeout | not-started | Standard annotation interoperability required. |
| Text note and free-text annotation | not-started | Font fallback and appearance tests required. |
| Ink and shape annotation | not-started | Pointer, stylus, and keyboard alternatives required. |
| Custom stamps | not-started | Only original/user-provided assets. |
| Comment list, filters, replies, and status | not-started | Interoperability varies by producer. |
| Annotation import/export | not-started | XFDF/FDF subset must be documented and tested. |
| Distance, perimeter, and area measurement | not-started | Scale calibration and unit tests required. |
| Visual and text comparison | not-started | False-positive tolerances must be documented. |
| Permission-aware copy and snapshot | not-started | Enforce document permissions. |
| Complete keyboard operation | not-started | Requires audited focus order and shortcut conflicts. |
| Screen-reader labels and announcements | not-started | Requires assistive-technology testing. |
| High-contrast reading | not-started | Must preserve semantic distinction. |
| Reflow experiment | not-started | Fidelity and reading-order limits must be explicit. |

## M3 — editing and creation

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Add, edit, and move text | not-started | Font substitution warnings and fidelity fixtures required. |
| Add, replace, and move images | not-started | Preserve color/profile behavior where possible. |
| Headers, footers, and page numbers | not-started | Undo and range presets required. |
| Backgrounds and watermarks | not-started | Layering and opacity fixtures required. |
| Links and named destinations | not-started | Safe URL policy and keyboard editing required. |
| Create and edit bookmarks | not-started | Nested outline save/reopen required. |
| Import scans and images | not-started | Resource and image-dimension bounds required. |
| Deskew and denoise | not-started | Non-destructive preview and cancellation required. |
| Offline OCR and searchable layer | not-started | Language packs, confidence, and placement fixtures required. |
| Create PDF from local inputs | not-started | Images, text, clipboard, and print flow tracked separately in tests. |
| Standards-aware export presets | not-started | Validation engine and conformance reports required. |

## M4 — forms, signatures, and protection

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Create and edit AcroForm fields | not-started | Field hierarchy and appearance fixtures required. |
| Form calculation and validation subset | not-started | JavaScript is not implicitly trusted or executed. |
| Flatten forms and annotations | not-started | Visual regression and irreversible-action warning required. |
| Import/export form data | not-started | Supported FDF/XFDF subset must be explicit. |
| Local signature appearance | not-started | Appearance is not presented as cryptographic proof. |
| Certificate signing and validation | not-started | Trust model, revocation, and algorithm policy required. |
| Trusted timestamp integration | not-started | Optional network access must be explicit and scoped. |
| Password encryption and permissions | not-started | Algorithm interoperability fixtures required. |
| Certificate encryption | not-started | Key-store integration and recovery documentation required. |
| Sanitize and remove hidden information | not-started | Recovery-focused regression corpus required. |
| Remove metadata | not-started | Verify incremental-history implications. |
| Mark and apply true redaction | not-started | Content, resources, OCR, and incremental history must be unrecoverable. |

## M5 — professional and accessibility workflows

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Preflight profiles and fixups | not-started | Profile provenance and deterministic reports required. |
| PDF/A validation and conversion | not-started | Conformance-level fixtures required. |
| PDF/X validation and conversion | not-started | Output intent and print-production fixtures required. |
| Separations, ink, and overprint preview | not-started | Color-management validation required. |
| Object inspector | not-started | Must bound recursive object traversal. |
| Transparency flattening | not-started | Visual and print regression fixtures required. |
| Tags tree and reading order | not-started | Save/reopen and assistive-technology validation required. |
| Alt text, language, and table semantics | not-started | Standards-aware validation required. |
| Accessibility checker | not-started | Ruleset/version and limitations must be visible. |
| Portfolio/package support | not-started | Attachment isolation and safe extraction required. |
| Batch/action wizard | not-started | Dry-run, rollback boundaries, and audit log required. |
| Watched folders | not-started | Symlink, recursion, and partial-write behavior required. |
| Stable local CLI | not-started | Versioned output and exit-code contract required. |

## M6 — compatibility and release quality

| Workflow | State | Verification evidence / remaining work |
| --- | --- | --- |
| Encrypted/damaged/signed/tagged compatibility corpus | not-started | Legal, redistributable fixtures only. |
| Long and large-document performance corpus | not-started | Enforce documented budgets on reference hardware. |
| Parser and converter fuzzing | not-started | Sanitizer CI and minimized regressions required. |
| Decompression-bomb defenses | not-started | Memory, output-size, recursion, and time bounds required. |
| Malicious attachment handling | not-started | Never auto-open; safe-name and type validation required. |
| Safe external URL policy | not-started | Visible destination and explicit consent required. |
| Reproducible Arch package/AppImage | not-started | Signed checksums and clean-build comparison required. |
| SBOM and dependency license review | not-started | Release artifact must include both. |
| Settings/database migration | not-started | Forward upgrade and safe failure required. |
| Real Omarchy Quattro validation | not-started | Blocked until compatible hardware/session is available. |

## Explicit exclusions

| Workflow | State | Reason |
| --- | --- | --- |
| Adobe account and cloud storage | excluded | ZenPDF Desktop is local-first and account-free. |
| Adobe AI Assistant and PDF Spaces | excluded | Proprietary cloud services are outside the product boundary. |
| Adobe Express templates/assets | excluded | Proprietary service and expressive assets are outside scope. |
| Adobe cloud sharing and collaboration | excluded | No mandatory upload or hosted document workflow. |
| Adobe proprietary UI/assets/wording | blocked-by-standard/licensing | ZenPDF uses an original interface and assets. |

## Updating the matrix

Change a row in the same commit that changes the capability. Link the focused test or fixture by repository path in the evidence column. If only part of a workflow is production-ready, use `partial` and state the exact boundary. Do not infer compatibility from the underlying PDF engine alone.
