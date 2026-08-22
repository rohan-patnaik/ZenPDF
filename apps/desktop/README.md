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

Organizer commands take private, stable source snapshots, never overwrite a source or existing destination, enforce a 2 GiB input/output ceiling, 100,000-page and bounded-range validation, run with cancellation and a hard two-minute ceiling, and publish only a completed validated result with an atomic no-replace operation. Cancellation, timeout, malformed input, validation failure, or a source/destination race removes staging and leaves the source unchanged.

Attachments, form filling, two-page layout, persistent crop/reorder/insert/replace/split, in-place editing, and undo/redo are not implemented yet and remain marked accordingly in the capability matrix. The generated extraction/deletion fixture covers retained text annotations, outlines, metadata, page rendering, and Qt PDF reopen; broad forms, tagged, signed, mixed-box, and independent-producer interoperability evidence is still missing, so the matrix remains `Partial`.
