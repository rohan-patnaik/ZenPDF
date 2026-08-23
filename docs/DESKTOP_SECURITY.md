# ZenPDF Desktop security, recovery, and performance model

## Trust boundary

Every PDF, embedded object, attachment, font, image, form action, URI, and converted office document is untrusted input. A successful parse does not make embedded content safe. ZenPDF Desktop does not require accounts, network services, telemetry, or document uploads. It does not auto-open attachments or external links.

The Qt UI process owns user interaction and local state. Parsing/rendering and structural changes sit behind narrow adapters. Work that invokes broad parsers or converters should move to a separately launched, resource-bounded helper before its feature can be marked verified. Helpers receive explicit input/output paths, run without network access where the host supports it, and write a new output rather than mutating the source in place.

## Resource policy

The following initial budgets are release gates, not assumptions that libraries enforce them automatically:

| Resource | Initial budget |
| --- | --- |
| Open document size | 2 GiB hard rejection before parsing |
| Page count | 100,000 hard rejection; warning above 10,000 |
| Rendered page dimension | 32,768 pixels on either axis |
| In-process render and thumbnail cache | 256 MiB combined |
| Caller-declared undo payload retention | 64 MiB per document and 512 commands |
| Concurrent background document jobs | 2 per application |
| Extracted attachment or helper output | User-confirmed destination; 2 GiB per output |
| Diagnostic logs | Two local files of at most 1 MiB each |

Features must apply stricter limits when their engine cannot safely meet these ceilings. Cancellation is cooperative first and process termination is the last boundary for isolated helpers. A cancelled transformation must not replace the source or leave a result presented as complete.

The shared scheduler foundation defaults to two running and 32 queued jobs, caps all admitted-but-undelivered work at 34, runs the queue FIFO, and delivers terminal results once in submission order on its owning Qt thread. Capacity, missing-task, shutdown, and invalid-configuration failures are explicit. Queued cancellation prevents execution; running cancellation is cooperative and checked again at owner-thread delivery so cancellation wins the completion race. Shutdown rejects new submissions, cancels outstanding work, and reports whether its timed join completed. Final destruction joins without a deadline to protect scheduler-owned state.

The modal organizer uses this scheduler for admission, cancellation, ordered terminal delivery, and main-window shutdown. It disables organizer actions while active, rejects programmatic reentry, and passes the scheduler cancellation token directly into the unchanged qpdf adapter. The adapter, not the scheduler, enforces its input/output limits, polling deadline, private staging, atomic publication, and Unix process-group cleanup. UI tests cover actionable capacity rejection, cancellation without stale success data, lifecycle join, and a clean result tab with an unchanged clean source.

These remain count and lifecycle bounds, not whole-job resource isolation. Captured task state and result payload bytes are not measured, no generic in-process task can be forcibly terminated, and a non-cooperative task can outlive timed shutdown. Future reader adapters still need task-specific payload, memory, output, and deadline controls; broad parsers and converters still require separately reviewed process boundaries.

The thumbnail model separately caps deferred owner-thread work at 64 distinct pending pages. It bounds accepted render dimensions to 128 by 512 pixels and charges each cached pixmap four bytes per pixel against a 32 MiB `QCache` ceiling; duplicate requests add no entry, and least-recently-used cached pixmaps are evicted at the exact boundary. Capacity rejection records one saturation bit rather than rejected page numbers; the next drained slot emits a decoration invalidation so the view can re-request visible pages. Cancellation, document replacement, page-count reset, and source destruction stop the queued timer and discard pending, cached, failed-page, and saturation state. A render failure is remembered until that reset so repeated view queries cannot create an unbounded retry loop.

These controls do not isolate PDF parsing or rendering. The render call is synchronous on the `QPdfDocument` owner thread, cannot be interrupted once entered, and has no hard timeout. The cache charge is declared pixel storage rather than measured process, Qt object, allocator, or graphics-driver memory, and image decoding may use additional transient memory. Background rendering would require a separately owned document/password lifecycle and is not claimed here.

## Local data and privacy

The local state database contains recent paths, timestamps, preferences, index metadata, recovery metadata, and future audit entries. Logs contain application events and error categories; document bytes, extracted text, form values, passwords, cryptographic keys, and full user paths must not be logged by default. Users must be able to clear recent history and derived indexes without touching their documents.

On Unix, the process sets umask 077 before constructing `QApplication`. Before diagnostics or SQLite can create sensitive data, ZenPDF opens the application-data leaf without following symlinks, validates the opened descriptor as an effective-user-owned ordinary directory, and requires effective mode 0700. Before SQLite opens, the database and any existing WAL/SHM sidecars are opened without following symlinks and validated through the same descriptor as owner-owned, single-link regular files at mode 0600. Owner-owned 0755 directories and 0644 files may be tightened only when they were never group/other-writable and have no special mode bits. The repaired descriptor is revalidated before use.

Symlinks, wrong-owner objects, wrong types, multi-link files, special modes, group/other-writable state, and unverifiable repairs fail startup closed. Errors are product-owned, path-free, driver-free, and limited to 256 characters. Initialization failure also closes and removes the Qt SQL connection so a clean retry cannot inherit a stale handle. These controls preserve the existing 50-entry history bound, path normalization and deduplication, SQLite secure deletion, WAL truncation, `VACUUM`, and purge-sentinel contract.

The secured 0700 leaf excludes replacement by a different unprivileged local user. It does not defend against the same effective UID, privileged processes, a hostile kernel or filesystem, or paths disclosed before repair. Existing diagnostic-log child handling is inside the secured leaf but retains a same-UID child-path indirection residual; this slice does not claim a no-alias guarantee for every log or rotation path. Non-Unix behavior is unchanged.

## Save and crash recovery design

Document changes use a command journal with deterministic undo/redo records. Autosave metadata references the source identity and a private working copy; it never modifies the source. Each save follows this sequence:

1. Write the complete result to a unique file in the destination directory.
2. Flush the file and validate that it can be reopened with the expected page count and requested changes.
3. Preserve destination permissions where possible.
4. Atomically replace the chosen destination only after validation.
5. Remove recovery metadata only after the replacement and state commit succeed.

On startup, ZenPDF offers recoverable sessions rather than silently reopening hostile input. Stale working files are listed with their source and timestamp and require an explicit recovery or discard choice. Symlinks, ownership changes, destination changes, insufficient space, and cross-filesystem replacement are treated as save failures, not reasons to weaken atomicity.

The current implementation establishes a 512-command in-memory undo limit, a configurable per-document admission ceiling that defaults to 64 MiB of caller-declared retained payload costs, clean-revision dirty tracking, active-tab undo/redo routing, and dirty-close confirmation. Admission fails before command execution or transfer to the Qt undo stack when a command is missing, already obsolete, individually over budget, would overflow the counter, or would exceed the cumulative ceiling. Commands cannot merge; the wrapper mirrors producer action text after each transition. Undo/redo retain declared costs, while redo-branch replacement, Qt obsolete-command removal, or oldest-command eviction releases the corresponding declarations. Reconciliation observes the stack itself, so active `QUndoGroup` menu and shortcut actions cannot bypass it. A command that becomes obsolete while executing redo is conservatively dirty because no undo record remains; removal during undo follows Qt's resulting clean revision. Both cases emit an actionable discard signal.

Document sessions capture an immutable source-revision prerequisite only when their initial path resolves to a stable regular file. Explicit owner-thread revalidation classifies unchanged, modified, replaced, missing, unavailable, and initially untracked sources. Linux checks the lexical path entry and resolved source device/inode identities, symlink state, canonical path, size, and nanosecond mtime/ctime in two stable metadata probes. It therefore detects same-size edits with restored mtime and symlink or inode replacement; an initially missing or unstable path is never silently adopted later. Non-Linux checks use canonical path, size, and available Qt timestamps and are weaker evidence.

The source guard is not a watcher, byte hash, open-descriptor lease, or save lock. Revalidation has a subsequent race and cannot establish content identity against a privileged metadata-restoring adversary. A future save must revalidate at publication and retain the existing race, alias, fsync, validation, and source-preservation gates. The undo accounting likewise does not inspect allocations or prove a heap bound. It depends on future command producers declaring complete retained payload costs and excludes command object and Qt framework overhead. Producer-specific measurement, under-declaration tests, and an allocator/process-level memory boundary remain required before strict completion. No PDF mutation command, atomic save/save-as path, autosave working copy, serialized recovery journal, or startup recovery workflow exists yet; the sequence above remains the gate for those later slices.

## Failure behavior

Corrupt, encrypted, unsupported, or resource-exhausting documents fail closed with an actionable local message. The UI remains responsive, partial organizer output is deleted, and the original remains untouched. Passwords live only for the active operation and are never written to state or logs. URI launches show the normalized destination and require consent; non-HTTP schemes are denied unless separately reviewed.

## Validation strategy

- Unit tests cover state migrations, command/range validation, bounds, and adapter argument construction.
- Integration fixtures cover malformed object graphs, extreme dimensions, encryption, forms, annotations, page edits, cancellation, and save/reopen.
- Sanitizer and fuzz jobs target adapters and helper protocols before broad format support is marked verified.
- Real Arch/Wayland and Omarchy Quattro validation remains a release gate because offscreen CI cannot reproduce compositor, portal, printing, or assistive-technology behavior.
- The post-publication thumbnail gate follows `docs/THUMBNAIL_WAYLAND_ACCEPTANCE.md`; it must use the exact reviewed and CI-green installed package rather than a developer build.
