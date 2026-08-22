# ZenPDF Desktop local capability plan

This is the governing local capability contract for ZenPDF Desktop. It is a delivery checklist, not a claim of Adobe Acrobat compatibility. M0-M6 identify the historical delivery workstream that owns a row; they do not imply that the row shipped with that milestone.

Exactly 76 local capability rows are tracked below. Allowed statuses are `Not started`, `Partial`, `Verified`, and `Blocked`. `Verified` requires linked automated tests plus save/reopen, error/resource, accessibility, documentation, and interoperability evidence where those concerns apply. A green engine test alone is not parity evidence.

## Product boundary

- Core desktop use is offline, local-first, account-free, telemetry-free, and never uploads a document.
- Network use is optional and off by default. It may be added only for an explicit user action such as a trusted timestamp, certificate revocation check, or update check; the destination and transmitted metadata must be disclosed, and local workflows must remain usable when it fails.
- Offline signing uses a user-selected local key and requires no network. Embedded signatures and timestamps are validated locally; when revocation data is unavailable offline, the result must say `revocation unchecked`, never `invalid` solely because no network was used.
- Adobe accounts, cloud storage/sharing, hosted review/collaboration, cloud e-sign request/tracking, third-party cloud integrations, AI Assistant, PDF Spaces, Express assets/templates, proprietary services, and proprietary UI/assets/wording are excluded rather than local capability rows.
- Interoperability means standards-based round trips with legally redistributable fixtures and independent readers. It does not mean copying another product's implementation or presentation.

## Published implementation baseline

This source-revision prerequisite starts from published branch commit `76619334bcdf15b2baad364f151642f1cf66dd13`, tree `b28641dae250f6049aa13c3d1dcf42c5329ef4d5`. Exact-tip [CI run 32589928577](https://github.com/rohan-patnaik/ZenPDF/actions/runs/32589928577) passed governance, web, worker, desktop Arch package, and both product-container builds. Installed exact-SHA Omarchy/Wayland acceptance was recorded for that baseline, including the remediated focused-thumbnail Space activation. Native chooser assistive-technology gaps, a live cancellation announcement capture, persistent save/recovery, and long-document reader isolation/resource evidence remain. Those results validate the published baseline only; they are not evidence for this later source-revision implementation.

## Counts

| Status | Count |
| --- | ---: |
| Not started | 45 |
| Partial | 31 |
| Verified | 0 |
| Blocked | 0 |
| **Total** | **76** |

## Capability matrix

`Owner` is the historical milestone/workstream responsible for delivery. `Deps` names decision or infrastructure prerequisites, not adopted packages. `Tests` is the minimum regression evidence; `Interop` is the external round-trip evidence required for verification.

| ID | Local capability | Owner | Deps | Status | Tests | Interop evidence |
| --- | --- | --- | --- | --- | --- | --- |
| L001 | Native Arch/Wayland application launch | M0 / Platform | Qt, package | Partial | CI configure/build/CTest; real-session smoke missing | Omarchy Quattro launch record missing |
| L002 | Multi-document tabbed workspace | M0 / Shell | Qt Widgets | Partial | `DocumentSessionTest.cpp` and `MainWindowTest.cpp` cover a 512-command per-tab limit, a 64 MiB default ceiling on caller-declared retained payload costs, active routing, dirty markers, clean organizer tabs, and explicit owner-thread source-revision classification; true heap bounds, monitoring, save/recovery, and real-session lifecycle remain | N/A |
| L003 | System theme, scaling, and high contrast | M0 / Accessibility | Qt platform theme | Partial | Portal, fractional-scale, contrast tests missing | Wayland compositor matrix missing |
| L004 | Persistent local preferences | M0 / State | QSettings | Partial | Corrupt/migration fixtures missing | N/A |
| L005 | Private bounded local diagnostics | M0 / Operations | Filesystem policy | Partial | `LoggingTest.cpp`; crash/privacy review remains | N/A |
| L006 | Crash/session recovery | M0 / State | Recovery journal decision | Not started | Source-revision revalidation is only a prerequisite; interrupted-write, working-copy, journal, and stale-session recovery remain absent | N/A |
| L007 | Omarchy launcher contract | M0 / Platform | Quickshell | Partial | Static and independent launcher failure tests; installed launch missing | Real Quickshell/Wayland evidence missing |
| L008 | Arch package and portable artifact | M0 / Release | Packaging decision | Not started | Clean install/remove/reproducibility tests | Arch package-manager evidence missing |
| L009 | Open bounded local PDF | M1 / Reader | Qt PDF adapter | Partial | `PdfDocumentTest.cpp`; hostile corpus remains | Independent-reader corpus remains |
| L010 | Password-protected PDF open | M1 / Reader | Qt PDF adapter | Partial | `DocumentWidgetTest.cpp` verifies password clearing | Encrypted producer matrix remains |
| L011 | Continuous page rendering and scrolling | M1 / Reader | Render adapter | Partial | Long/corrupt/render-budget corpus missing | Mixed page-box corpus missing |
| L012 | Full-document text search | M1 / Reader | Text adapter | Partial | Unicode/no-text/highlight tests missing | Tagged and untagged producer corpus missing |
| L013 | Lazy page thumbnails | M1 / Reader | Render cache | Partial | `ThumbnailModelTest.cpp` covers owner-thread FIFO deferral, a 64-request boundary, saturation invalidation and automatic visible-row readmission without a rejected-page backlog, duplicate suppression, cancellation/readmission, 128x512 render bounds, exact 32 MiB four-byte pixel-cost eviction, failure suppression, document reset/destruction, and an 80-page generated fixture; synchronous render timeout/isolation, hostile independent producers, transient/true heap bounds, and installed exact-SHA Wayland/AT evidence for this slice remain | N/A |
| L014 | Outline and bookmark navigation | M1 / Reader | Outline adapter | Partial | Malformed/deep/named-destination tests missing | Producer corpus missing |
| L015 | Metadata inspection | M1 / Reader | Metadata adapter | Partial | Malformed/XMP fixtures missing | Standard/XMP producer corpus missing |
| L016 | Private recent-files history and purge | M1 / State | SQLite | Partial | `apps/desktop/tests/LocalStateTest.cpp::clearingPurgesPathsFromDatabaseFiles`; platform forensic run remains | N/A |
| L017 | Page navigation and labels | M1 / Reader | Qt PDF adapter | Partial | UI automation/page-label fixtures missing | Nondecimal page-label corpus missing |
| L018 | Zoom, actual size, fit width/page | M1 / Reader | Render adapter | Partial | DPI/shortcut/position tests missing | Reference-render comparison missing |
| L019 | Non-destructive view rotation | M1 / Reader | Render adapter | Not started | Rotation and source-unchanged fixtures | Independent-reader source hash |
| L020 | Continuous, single, and two-page layouts | M1 / Reader | View layout | Partial | Two-page/position regression missing | N/A |
| L021 | Full-screen presentation reading | M1 / Reader | Wayland shell | Partial | Escape/screen/compositor tests missing | Real compositor evidence missing |
| L022 | Bounded local printing | M1 / Print | Qt PrintSupport | Partial | `PrintPolicyTest.cpp`; real backend missing | CUPS/printer output comparison missing |
| L023 | Merge documents into a new file | M1 / Organizer | Structure adapter | Partial | `QpdfOperationsTest.cpp`; forms/outlines corpus remains | Independent-reader reopen remains |
| L024 | Extract page ranges into a new file | M1 / Organizer | Structure adapter | Partial | `apps/desktop/tests/QpdfOperationsTest.cpp::extractsAndDeletesWithReopenEquivalence`; broad permission fixtures remain | Generated outline/annotation fixture reopens in Qt PDF; independent producer corpus missing |
| L025 | Persistently rotate page ranges | M1 / Organizer | Structure adapter | Partial | `QpdfOperationsTest.cpp`; UI/reopen matrix remains | Independent-reader reopen remains |
| L026 | Atomic, cancellable, bounded organizer jobs | M1 / Organizer | Helper isolation | Partial | Unix qpdf process-group cleanup, bounded helper channels, and shared-scheduler UI integration cover admission, reentry, cancel, lifecycle join, ordered completion, and clean-output tabs; blocking filesystem calls, non-Unix tree cleanup, and crash injection remain | N/A |
| L027 | Insert, delete, reorder, replace, crop, and split pages | M1 / Organizer | Structure/write adapter decision | Partial | Extraction/deletion and clean-output tests plus per-document undo admission and Linux source-revision revalidation prerequisites; no persistent mutation/save, insert/reorder/replace/crop/split, or mutation-backed undo exists, and retained-cost declarations are not a true heap bound | Generated outline/annotation fixture only; mixed-box and independent-reader corpus missing |
| L028 | View and safely extract embedded attachments | M1 / Reader | Attachment parser/isolation decision | Not started | Hostile-name/type/size/explicit-destination fixtures | Standard embedded-file corpus required |
| L029 | View and fill basic AcroForms | M1 / Forms | Forms engine decision | Not started | Field/appearance/save/reopen fixtures | Multiple-reader form round trip |
| L030 | Preserve existing annotations/forms on save | M1 / Organizer | Write adapter decision | Partial | Generated retained text-annotation/outline fixture passes `apps/desktop/tests/QpdfOperationsTest.cpp::extractsAndDeletesWithReopenEquivalence`; forms/tagged/signed corpus missing | Multiple-reader comparison missing |
| L031 | Text markup annotations | M2 / Review | Annotation adapter decision | Not started | Selection/appearance/undo/reopen fixtures | ISO annotation round trip |
| L032 | Notes and free-text annotations | M2 / Review | Annotation adapter decision | Not started | Fonts/appearance/undo/reopen fixtures | ISO annotation round trip |
| L033 | Ink, shapes, and custom stamps | M2 / Review | Annotation adapter decision | Not started | Pointer/stylus/keyboard/reopen fixtures | ISO annotation round trip |
| L034 | Comment list, filters, replies, and status | M2 / Review | Annotation model decision | Not started | Thread/filter/status fixtures | Producer compatibility matrix |
| L035 | Annotation import/export | M2 / Review | XFDF/FDF scope decision | Not started | Schema/round-trip/error fixtures | Documented XFDF/FDF subset |
| L036 | Distance, perimeter, and area measurement | M2 / Review | Geometry model | Not started | Calibration/unit/rotation tests | Scale-dictionary fixtures |
| L037 | Visual and text comparison | M2 / Review | Comparison policy | Not started | Tolerance/false-positive corpus | Independent reference renders |
| L038 | Permission-aware copy and snapshot | M2 / Security | Permission adapter | Not started | Permission/clipboard/screenshot tests | Encrypted producer matrix |
| L039 | Complete keyboard and screen-reader operation | M2 / Accessibility | AT test environment | Not started | Focus/shortcut/name/announcement audit | Orca/Wayland evidence |
| L040 | High-contrast and reflow reading | M2 / Accessibility | Layout semantics decision | Not started | Contrast/reading-order/fidelity tests | Tagged-PDF corpus |
| L041 | Add, edit, and move text | M3 / Editor | Content-write engine decision | Not started | Font/fidelity/undo/reopen fixtures | Multiple-reader round trip |
| L042 | Add, replace, and move images | M3 / Editor | Content-write engine decision | Not started | Profile/alpha/undo/reopen fixtures | Multiple-reader round trip |
| L043 | Headers, footers, and page numbers | M3 / Editor | Content-write engine decision | Not started | Range/undo/reopen fixtures | Multiple-reader round trip |
| L044 | Backgrounds and watermarks | M3 / Editor | Content-write engine decision | Not started | Layer/opacity/undo/reopen fixtures | Multiple-reader round trip |
| L045 | Links and named destinations | M3 / Editor | Content-write engine decision | Not started | URL policy/keyboard/reopen fixtures | ISO action/destination corpus |
| L046 | Create and edit bookmarks | M3 / Editor | Outline-write decision | Not started | Deep tree/undo/reopen fixtures | Multiple-reader outline round trip |
| L047 | Import scans and images | M3 / Creation | Image pipeline decision | Not started | Dimension/bomb/profile fixtures | Standard PDF image inspection |
| L048 | Deskew, denoise, and page cleanup | M3 / Scan | Image pipeline decision | Not started | Preview/cancel/quality corpus | Reference image comparison |
| L049 | Offline OCR and searchable text layer | M3 / OCR | OCR engine/language-pack decision | Not started | Language/confidence/placement fixtures | Search/copy in independent readers |
| L050 | Create PDF from local files and clipboard | M3 / Creation | Conversion adapter decisions | Not started | Type/resource/atomic-output fixtures | Multiple-reader reopen |
| L051 | Create and edit AcroForm fields | M4 / Forms | Forms engine decision | Not started | Hierarchy/appearance/undo/reopen fixtures | Multiple-reader form round trip |
| L052 | Form calculation and validation subset | M4 / Forms | Script safety policy | Not started | Allowlist/no-script/security fixtures | Documented supported subset |
| L053 | Flatten forms and annotations | M4 / Forms | Write adapter decision | Not started | Visual/irreversibility/reopen fixtures | Independent-reader render comparison |
| L054 | Import/export form data | M4 / Forms | FDF/XFDF scope decision | Not started | Schema/round-trip/error fixtures | Documented FDF/XFDF subset |
| L055 | Offline local-key certificate signing and appearance | M4 / Signatures | Crypto/key-store/signature model decision | Not started | Local-key/algorithm/appearance/reopen fixtures | Multi-validator signed corpus |
| L056 | Embedded signature and timestamp validation | M4 / Signatures | Crypto/trust-store decision | Not started | Valid/invalid/expired and offline revocation-unchecked fixtures | Multi-validator signature/timestamp corpus |
| L057 | Optional trusted timestamps and revocation refresh | M4 / Signatures | Explicit network policy | Not started | Offline/failure/disclosure/privacy and status-distinction tests | TSA/OCSP interoperability evidence |
| L058 | Password encryption and permissions | M4 / Protection | Crypto/write adapter decision | Not started | Algorithm/permission/reopen fixtures | Multiple-reader encrypted corpus |
| L059 | Certificate encryption | M4 / Protection | Crypto/key-store decision | Not started | Key/recovery/reopen fixtures | Multiple-reader encrypted corpus |
| L060 | Sanitize hidden content and metadata | M4 / Protection | Sanitizer/write decision | Not started | Forensic/incremental-history corpus | Independent forensic inspection |
| L061 | True redaction with content removal | M4 / Protection | Redaction/write decision | Not started | Text/image/OCR/resource forensic corpus | Independent extraction inspection |
| L062 | Preflight profiles and fixups | M5 / Standards | Validation engine decision | Not started | Versioned-profile/report fixtures | Independent validator comparison |
| L063 | PDF/A validation and conversion | M5 / Standards | veraPDF/write decision | Not started | Conformance-level corpus | veraPDF plus second validator |
| L064 | PDF/X validation and conversion | M5 / Standards | Validation/color decisions | Not started | Output-intent/print corpus | Independent validator comparison |
| L065 | Color separations, ink, and overprint preview | M5 / Print production | Color engine decision | Not started | ICC/spot/overprint render corpus | Reference RIP comparison |
| L066 | Object inspector and transparency flattening | M5 / Print production | Parser/write decisions | Not started | Recursion/visual/print corpus | Independent object/render inspection |
| L067 | Tags tree and reading-order editing | M5 / Accessibility | Tagged-write decision | Not started | Undo/reopen/AT fixtures | PAC/independent checker comparison |
| L068 | Alt text, language, and table semantics | M5 / Accessibility | Tagged-write decision | Not started | Semantics/reopen/ruleset fixtures | Independent accessibility checker |
| L069 | Accessibility checker | M5 / Accessibility | Ruleset decision | Not started | Versioned-rule false-positive corpus | Independent checker comparison |
| L070 | Portfolios/packages, batch actions, watched folders, and CLI | M5 / Automation | Package/isolation/CLI contract decisions | Not started | Package traversal, dry-run, rollback, and version tests | Standard collection/package and CLI contract |
| L071 | Encrypted, damaged, signed, tagged compatibility corpus | M6 / Quality | Redistributable fixtures | Partial | Small generated fixtures exist; broad legal corpus missing | Multiple independent producers/readers |
| L072 | Large-document budgets and decompression-bomb defenses | M6 / Security | Resource telemetry | Partial | Organizer hostile limits, caller-declared undo-cost admission, integrated scheduler lifecycle tests, and thumbnail pending/dimension/cache-cost boundaries exist; scheduler payload bytes, transient/true heap, decompression, whole-job memory, hard render timeout, and hostile producer corpus remain | N/A |
| L073 | Parser/converter fuzzing and risky-helper isolation | M6 / Security | Sandbox architecture decision | Partial | Bounded qpdf child exists; fuzz/sandbox CI missing | N/A |
| L074 | Reproducible Arch/package artifacts | M6 / Release | Packaging decision | Partial | Published baseline exact-tip Arch package CI run 32589928577 is green; this slice requires its own exact-tip run | Independent artifact comparison missing |
| L075 | Dependency lock, SBOM, notices, and license policy | M6 / Release | Artifact-derived transitive inventory | Partial | CI validates direct package pins/hashes, SPDX expressions, notices, and policy; transitive artifact inventory is absent | Direct package provenance is reviewable; release-grade transitive evidence missing |
| L076 | Real Omarchy Quattro/Wayland release smoke | M6 / Release | Compatible device/session | Partial | Installed exact-SHA `7661933` baseline acceptance was recorded; chooser AT, live cancellation announcements, and this source-revision slice's own published-package evidence remain | Required before release claim |

## Governance

Change a row in the same commit as its implementation and focused regression evidence. The validator enforces 76 unique rows, the status vocabulary, and count totals. A row remains `Partial` or `Blocked` when exact-commit CI, real-platform, standards, accessibility, or interoperability evidence is absent. Dependency adoption, licensing posture, or helper-isolation architecture changes require an accepted ADR and user direction first.
