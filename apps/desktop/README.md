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

The installed `zenpdf-launch` command starts the application independently of Quickshell, writes private startup diagnostics under the user state directory, and reports a missing executable or early exit without privilege escalation.

The native app opens multiple local PDFs (including password-protected files) in tabs and provides continuous/single-page reading, page navigation, fit/zoom controls, bounded thumbnails, outlines, full-document text search, metadata, recent files, drag-and-drop, bounded local printing, and full-screen presentation. Printing is limited to 100 pages per job and a 2048-pixel render dimension; Qt PDF renders each page synchronously, so cancellation takes effect between pages rather than during the current page. qpdf-backed organizer commands merge documents, extract page ranges, and rotate page ranges into a new file. Organizer commands never overwrite their source, run with cancellation and a two-minute bound, and only publish a completed PDF-shaped temporary result.

Attachments, form filling, two-page layout, persistent crop/reorder/insert/delete, and undo/redo are not implemented yet and remain marked accordingly in the capability matrix.
