# ZenPDF Desktop alpha

ZenPDF Desktop alpha is an offline, account-free native Arch/Omarchy workspace. It is not an Acrobat compatibility claim. The capability matrix remains authoritative; every desktop row is still `Partial` or `Not started`.

## Included scope

- Local PDF tabs with navigation, zoom, bounded thumbnails, outlines, metadata, text search, recent files, printing, and presentation.
- qpdf-backed merge, page extraction, page deletion, and rotation into a new file. Organizer operations preserve the source and never overwrite an existing destination.
- Thin Omarchy launcher files at the repository root. The native `zenpdf` and `zenpdf-launch` executables are supplied by the Arch package.

## Exact feature-branch package path

The current alpha lives on `feat/omarchy-desktop-m0-m1`. Omarchy's normal plugin-install path cannot install this unpublished feature-branch revision as the default-branch plugin. Until an independently reviewed revision is published to the supported plugin source, install its exact Arch package and start `zenpdf-launch` directly; do not treat a default-branch checkout as equivalent.

For a reviewed published commit, create an archive from a clean HTTPS clone, add the package revision marker, and build without following a moving branch:

```sh
published_sha=<reviewed-40-character-sha>
work_root=$(mktemp -d)
git clone --branch feat/omarchy-desktop-m0-m1 --single-branch \
  https://github.com/rohan-patnaik/ZenPDF "$work_root/repo"
cd "$work_root/repo"
test "$(git rev-parse HEAD)" = "$published_sha"
git archive --format=tar --prefix=ZenPDF/ HEAD >"$work_root/source.tar"
mkdir "$work_root/source"
tar -xf "$work_root/source.tar" -C "$work_root/source"
printf '%s\n' "$published_sha" >"$work_root/source/ZenPDF/.zenpdf-source-revision"
tar -C "$work_root/source" -czf \
  "$work_root/repo/apps/desktop/packaging/arch/zenpdf-source-$published_sha.tar.gz" ZenPDF
cd apps/desktop/packaging/arch
ZENPDF_SOURCE_ARCHIVE=zenpdf-source-$published_sha.tar.gz \
ZENPDF_EXPECTED_REVISION=$published_sha \
makepkg --cleanbuild --clean --noconfirm
sudo pacman -U ./zenpdf-git-0.1.0.r0.g${published_sha:0:7}-1-x86_64.pkg.tar.zst
zenpdf-launch
```

Record the commit, tree, package version and SHA-256 before acceptance. A developer build, a package from another revision, or a successful launch alone is not release evidence.

## Known alpha limits

- Search uses Qt PDF/PDFium in-process. Generated regressions cover text success, Unicode, blank and malformed PDFs, deterministic query supersession, active-search destruction, and an 80-page fixture. Scanned-image PDFs require an OCR engine that has not been selected. Hostile and very-large-document resource behavior, true cancellation and timeout, highlight fidelity, installed assistive technology, and independent-producer coverage remain incomplete.
- PDF parsing and rendering are not process-isolated. Thumbnail rendering is owner-thread deferred but has no mid-render cancellation or hard timeout.
- Persistent PDF mutation, save/save-as, autosave, serialized recovery, crash recovery, forms, signatures, redaction, OCR, color management, and standards conversion are not implemented.
- Native chooser assistive-technology behavior and live cancellation announcements retain known acceptance gaps. See the capability matrix and security model for the complete limits.

Use `docs/SEARCH_WAYLAND_ACCEPTANCE.md` for the post-publication visible search-success gate.
