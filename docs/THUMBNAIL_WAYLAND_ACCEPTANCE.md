# Exact-SHA thumbnail Wayland acceptance

Run this gate only after the thumbnail commits are independently reviewed, published by normal fast-forward, and green in exact-tip CI. A developer build or the earlier `6c21ed3` installed-package pass is not evidence for the thumbnail slice.

## Fixture and package identity

1. Record the published commit and tree, exact-tip CI run, Arch package filename/version and checksum, Omarchy version, compositor, display scale, and assistive-technology versions.
2. Build the focused test from the same published source and generate its bounded 80-page fixture at a new private path:

   ```sh
   cmake -S apps/desktop -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build/desktop --target zenpdf_thumbnail_model_tests
   ZENPDF_THUMBNAIL_ACCEPTANCE_FIXTURE=/tmp/zenpdf-thumbnail-80-pages.pdf \
     QT_QPA_PLATFORM=offscreen \
     build/desktop/zenpdf_thumbnail_model_tests \
     capsCancelsAndReadmitsLongDocumentRequests
   ```

   The destination must not already exist. Record its SHA-256. Do not use this generated fixture as independent-producer interoperability evidence.
3. Install the exact CI-equivalent package and launch its installed `zenpdf-launch` payload in the real Wayland session. Confirm the running executable and package revision match the published SHA before collecting results.

## Interaction and lifecycle checks

- Open the 80-page fixture through the native chooser. Record keyboard focus and Orca/AT-SPI behavior of both the chooser and ZenPDF; chooser gaps remain product gaps even when owned by the platform toolkit.
- Reach the Pages list without a pointer. Confirm its accessible name is `Page thumbnails`, items announce localized `Page N` labels and selection, Enter/Space activation navigates to the matching page, and visible focus remains clear.
- Traverse the beginning, middle, and end rapidly. Confirm thumbnails appear incrementally in FIFO request order without duplicate churn, stale images, or an unresponsive data lookup. Capture peak and settled RSS as observational evidence only; it does not prove a hard heap bound.
- Close the document and then the application while many thumbnails are queued. Confirm there is no late UI update, crash, stale thumbnail on reopen, or shutdown hang. A render already executing may block because mid-render cancellation and a hard render deadline are not implemented.
- Open malformed, encrypted, and unusually tall/wide local fixtures already approved for redistribution. Record failure and retry behavior without paths or document contents in logs. Do not infer decompression-bomb resistance or hostile-producer coverage from the generated fixture.

Keep L013, L039, L072, and L076 below `Verified` until the exact results, accessibility gaps, resource observations, fixtures, and command logs are recorded at the published SHA. This gate does not add mutation, dirty-state, save, or recovery evidence.
