# ZenPDF Desktop direct-package notices

This Phase 0 notice index covers only the three direct Arch runtime packages. It is not a complete release notice bundle. Distribution remains blocked by policy until an artifact-derived transitive inventory and its notices are collected.

## qpdf 12.3.2-2

- Artifact: `qpdf-12.3.2-2-x86_64.pkg.tar.zst`
- SHA-256: `c35c98609645a4209a3a644cf9a36de6cd6d8f8672662cdd369713a33d898d31`
- License expression: `Apache-2.0 OR Artistic-2.0`
- Installed license material: `/usr/share/licenses/qpdf/`
- Upstream license: <https://qpdf.readthedocs.io/en/stable/license.html>

## qt6-base 6.11.1-1

- Artifact: `qt6-base-6.11.1-1-x86_64.pkg.tar.zst`
- SHA-256: `bba875581f1a750180603ff412155313023a98b13ccf318cc699e65025faf2d5`
- License expression: `GPL-3.0-only OR LGPL-3.0-only OR LicenseRef-Qt-Commercial`
- Installed license material: `/usr/share/licenses/qt6-base/`
- Upstream licensing: <https://doc.qt.io/qt-6/licensing.html>

## qt6-webengine 6.11.1-5

- Artifact: `qt6-webengine-6.11.1-5-x86_64.pkg.tar.zst`; this is Arch's current provider of Qt PDF.
- SHA-256: `a5c970eaaae07484a16b62e45a9ec32b234c2d52b87b45c47633a57b0bbae5ce`
- License expression: `GPL-3.0-only OR LGPL-3.0-only OR LicenseRef-Qt-Commercial`
- Installed license material: `/usr/share/licenses/qt6-webengine/`
- Qt PDF and bundled PDFium notices: <https://doc.qt.io/qt-6/qtpdf-licensing.html>

## Known incomplete evidence

The CycloneDX file is explicitly direct-package scoped. It does not enumerate Qt/PDFium or other transitive components, corresponding-source obligations, or every installed notice. It also excludes the web and worker product-container inventories: their base image digests, the worker's dated Debian snapshot and direct APT versions, the hashed Python resolution, and the npm lock are governed by `dependencies.lock.json`, but a release-grade artifact-derived transitive SBOM and notice bundle has not been produced for them. L075 therefore remains `Partial`.
