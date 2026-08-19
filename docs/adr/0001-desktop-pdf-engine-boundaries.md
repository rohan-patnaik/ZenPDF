# ADR 0001: Desktop PDF engine and support-library boundaries

- Status: Proposed; no dependency or architecture change authorized
- Date: 2026-08-20
- Scope: native desktop only

## Context

ZenPDF Desktop is AGPL-3.0, local-first, and treats PDFs as hostile. Rendering, structural edits, OCR, color, cryptography, validation, and content editing have different risk and interoperability profiles. Choosing an engine affects packaging, redistribution notices, process isolation, fixture requirements, and long-term API coupling.

Today the application links dynamically to Qt 6/Qt PDF and invokes the qpdf executable through a bounded argument-array adapter. This ADR records alternatives without adopting them. License identifiers below are an engineering inventory, not legal advice; the exact selected version and all transitive notices must be reviewed before distribution.

## Decision

1. Retain the current Qt PDF reader adapter and qpdf process adapter during Phase 0.
2. Keep engine-facing APIs narrow: document/render, structure/write, OCR, color, crypto/trust, standards validation, and conversion helpers. Do not expose third-party object models across application code.
3. Prefer an isolated executable with explicit input/output, timeout, cancellation, size, environment, and publication policy for parsers/converters that can support it. In-process use needs a separately accepted threat-model decision.
4. Do not add, replace, embed, or link any candidate below until the user directs the material license/architecture choice and the ADR is accepted.

## Candidate inventory

| Candidate | Potential adapter role | Arch/package and API implications | Upstream license inventory | Phase 0 disposition |
| --- | --- | --- | --- | --- |
| qpdf | Structural page operations, encryption, inspection | Official Arch `qpdf`; current CLI adapter avoids ABI coupling but requires executable/version probing and bounded child handling | Apache-2.0 ([upstream](https://qpdf.readthedocs.io/en/stable/license.html)) | **Current** 12.4.0 CLI remains; library API adoption needs direction |
| Qt PDF / PDFium | Rendering, search, outline, metadata | Official Arch `qt6-pdf`; C++ Qt API fits the UI, but Qt PDF bundles a PDFium snapshot and third-party notices and currently parses in-process | Qt PDF LGPL-3.0 or GPL-2.0/commercial; PDFium BSD plus bundled notices ([upstream](https://doc.qt.io/qt-6/qtpdf-licensing.html)) | **Current** dynamic Qt adapter remains; direct PDFium or helper isolation needs direction |
| Tesseract / Leptonica | Offline OCR and image preprocessing | Official Arch `tesseract` and `leptonica`; native APIs require language-data lifecycle and memory bounds; a helper protocol can isolate failures | Tesseract Apache-2.0; Leptonica BSD-like/2-clause ([upstream](https://github.com/tesseract-ocr/tesseract)) | Candidate only; choose language-pack and helper model first |
| LittleCMS | ICC transforms, output intents, soft proofing | Official Arch `lcms2`; small C API, but color policy, proof profiles, and reference-RIP fixtures must precede integration | MIT ([upstream](https://www.littlecms.com/)) | Candidate only; color architecture decision required |
| OpenSSL / Botan | Certificates, CMS signatures, trust, encryption primitives | Official Arch `openssl` / `botan`; never invent PDF crypto. OpenSSL aligns with system trust but has provider/version policy; Botan offers a C++ API but adds another crypto stack | OpenSSL 3.x Apache-2.0 ([upstream](https://openssl-library.org/source/license/index.html)); Botan Simplified BSD ([upstream](https://botan.randombit.net/handbook/botan.pdf)) | Mutually evaluated candidates; crypto/trust-store decision required |
| veraPDF | PDF/A and PDF/UA machine validation | `verapdf` is AUR rather than an official Arch package; Java CLI/process boundary is natural but adds a runtime and substantial rule assets | GPL-3.0-or-later OR MPL-2.0-or-later ([upstream](https://verapdf.org/home/)) | Candidate only; packaging/runtime/validator authority decision required |
| Ghostscript | PostScript/PDF conversion, flattening, reference rendering | Official Arch `ghostscript`; executable boundary is feasible, but distribution, SAFER policy, resource controls, and format fidelity need explicit review | AGPL-3.0 or commercial ([upstream](https://ghostscript.com/releases/gpcldnld.html)) | Candidate only; license and sandbox decision required |
| Poppler | Independent rendering/text/signature comparison or alternate engine | Official Arch `poppler` and `poppler-qt6`; GPL C++ APIs and a broad parser surface. CLI tools can serve test-oracle roles without becoming a product dependency | GPL-2.0-or-later for the C++ API ([upstream](https://poppler.freedesktop.org/api/cpp/poppler-document_8h_source.html)) | Test-oracle candidate; product-engine adoption needs direction |
| PoDoFo | Native C++ content/forms/signing writer | Official Arch `podofo`; rich C++ object model creates significant coupling and parses hostile data in-process unless wrapped | Library LGPL-2.0-or-later or MPL-2.0; tools GPL-2.0-or-later ([upstream](https://github.com/podofo/podofo)) | Candidate only; writer/API/isolation decision required |
| MuPDF | Alternate render/edit/conversion engine | Official Arch `mupdf`; C API is capable but a replacement would be material, and direct integration expands the in-process parser surface | AGPL or commercial ([upstream](https://mupdf.com/releases)) | Candidate only; license, engine replacement, and isolation direction required |

## Consequences and gates

- Phase 0 gains reviewable adapter seams and an explicit license/package inventory without changing runtime behavior.
- The current Qt PDF in-process parser remains a known isolation gap, tracked as `Partial` in L073. Fixing it is an architecture choice, not a documentation claim.
- OCR, signatures, standards conversion, print-production color, content editing, and alternate renderers remain `Not started` until their decision is accepted.
- Before adoption: pin an exact version/source, update the lock and CycloneDX inventory, collect complete notices/transitives, add hostile-input/resource tests, define the helper or in-process threat boundary, and provide independent interoperability fixtures.

## User direction required

Separate decisions are required for: (a) renderer isolation or replacement, (b) content/forms writer, (c) OCR helper and language packs, (d) OpenSSL versus Botan and the trust-store/network policy, (e) veraPDF packaging, and (f) Ghostscript/Poppler/PoDoFo/MuPDF product use versus test-only use. This ADR intentionally selects none of them.
