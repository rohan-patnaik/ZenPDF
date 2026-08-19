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

The native app opens multiple local PDFs in tabs and provides continuous/single-page reading, page navigation, fit/zoom controls, bounded thumbnails, outlines, full-document text search, metadata, recent files, drag-and-drop, and full-screen presentation. qpdf-backed organizer commands merge documents, extract page ranges, and rotate page ranges into a new file. Organizer commands never overwrite their source, run with cancellation and a two-minute bound, and only publish a completed PDF-shaped temporary result.

Password entry, printing, attachments, form filling, two-page layout, persistent crop/reorder/insert/delete, and undo/redo are not implemented yet and remain marked accordingly in the capability matrix.
