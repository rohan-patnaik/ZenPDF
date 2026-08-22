# ZenPDF Desktop

ZenPDF Desktop is the native, account-free workspace for Arch Linux and Omarchy Quattro. It uses C++23 and Qt 6 and does not upload documents or emit telemetry.

## Build

Install Qt 6 Base, Qt 6 PDF, qpdf, CMake 3.25+, Ninja, and a C++23 compiler, then run:

```sh
cmake -S apps/desktop -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/desktop
ctest --test-dir build/desktop --output-on-failure
```

Install into a staging root with `DESTDIR=/tmp/zenpdf-package cmake --install build/desktop`. The Arch `PKGBUILD` is a development packaging skeleton; release packages will replace its moving Git source with a signed, checksummed source archive.

## Local data

Qt selects the platform data directory (normally `~/.local/share/ZenPDF/ZenPDF`). ZenPDF stores a small SQLite state database and bounded diagnostic logs there. Document bytes remain in paths selected by the user. Recent-file history can be cleared locally; no account or network connection is required.

## Current reader and organizer scope

The installed `zenpdf-launch` command starts the application independently of Quickshell, writes at most 60 KiB of private startup output under the user state directory before draining and discarding later output, and reports a missing executable or early exit without privilege escalation.

The native app opens multiple local PDFs (including password-protected files) in tabs and provides continuous/single-page reading, page navigation, fit/zoom controls, bounded thumbnails, outlines, full-document text search, metadata, recent files, drag-and-drop, bounded local printing, and full-screen presentation. Printing is limited to 100 pages per job and a 2048-pixel render dimension; Qt PDF renders each page synchronously, so cancellation takes effect between pages rather than during the current page. qpdf-backed organizer commands merge documents, extract page ranges, delete selected pages, and rotate page ranges into a new file. Extract/delete outputs retain qpdf-supported document-level structure, pass qpdf reopen and expected-page-count checks, and open as a new clean tab; the source tab is never made dirty. Encrypted or permission-restricted organizer inputs are rejected until a separately reviewed password and permission policy exists.

Every document tab owns an in-memory undo stack limited to 512 non-merging commands with clean-revision dirty tracking. Command producers must declare each command's retained payload cost before transfer; a per-session admission gate defaults to 64 MiB and rejects a missing or already-obsolete command, a single over-budget command, arithmetic overflow, or a cumulative over-budget command before execution. Undo and redo retain their declared costs, while replacing a redo branch, Qt obsolete-command removal, and the 512-command oldest-entry eviction release them. Accounting reconciles from the stack's lifecycle signals, including Undo/Redo actions routed through `QUndoGroup`. The non-merging wrapper mirrors the producer's action text after each redo and undo. A command that becomes obsolete during redo is conservatively dirty because it executed without leaving an undo record; an obsolete command removed while undoing follows the resulting Qt clean revision. Queries and rejection/discard signals expose the declared usage, limit, remaining bytes, and an actionable reason for later UI integration.

This is a hard limit on caller-declared retained payload costs, not measured heap use or an allocator-level memory bound. Command object/framework overhead and an inaccurate producer declaration are outside the current accounting, so producer-specific measurement and adversarial validation remain required before a strict memory-bound claim. The Edit menu routes standard Undo/Redo shortcuts to the active tab, dirty tabs receive a visible marker, and tab/application close requires explicit discard confirmation. This is lifecycle infrastructure only: no persistent organizer mutation currently creates commands, and save, save-as, autosave, serialized recovery journals, and crash recovery remain unimplemented.

Organizer commands take private, stable source snapshots, never overwrite a source or existing destination, enforce a 2 GiB input/output ceiling, and apply 100,000-page and bounded-range validation. On Unix, every qpdf phase runs in a separate session, its process group is killed on cancellation or timeout, and stdout/stderr flow through continuously drained bounded pipes. The 120-second deadline is checked between snapshot chunks and while polling helpers; individual filesystem calls and process cleanup can still block beyond it, so whole-job hard wall-clock isolation remains an unsupported edge. Completed results are fsynced, validated, published with an atomic no-replace operation, and followed by a parent-directory fsync. A reported directory-fsync failure means the output was published but its crash durability is uncertain; it does not mean publication was rolled back. Cancellation, timeout, malformed input, validation failure, or a source/destination race before publication removes staging and leaves the source unchanged. Non-Unix process-tree isolation and crash-injection evidence remain missing.

Attachments, form filling, two-page layout, persistent crop/reorder/insert/replace/split, in-place editing, mutation-backed undo/redo, and save/recovery are not implemented yet and remain marked accordingly in the capability matrix. The generated extraction/deletion fixture covers retained text annotations, outlines, metadata, page rendering, and Qt PDF reopen; broad forms, tagged, signed, mixed-box, and independent-producer interoperability evidence is still missing, so the matrix remains `Partial`.
