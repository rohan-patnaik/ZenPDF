# ZenPDF Desktop

ZenPDF Desktop is the native, account-free workspace for Arch Linux and Omarchy Quattro. It uses C++23 and Qt 6 and does not upload documents or emit telemetry.

## Build

Install Qt 6 Base, CMake 3.25+, Ninja, and a C++23 compiler, then run:

```sh
cmake -S apps/desktop -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/desktop
ctest --test-dir build/desktop --output-on-failure
```

Install into a staging root with `DESTDIR=/tmp/zenpdf-package cmake --install build/desktop`. The Arch `PKGBUILD` is a development packaging skeleton; release packages will replace its moving Git source with a signed, checksummed source archive.

## Local data

Qt selects the platform data directory (normally `~/.local/share/ZenPDF/ZenPDF`). ZenPDF stores a small SQLite state database and bounded diagnostic logs there. Document bytes remain in paths selected by the user. Recent-file history can be cleared locally; no account or network connection is required.
