# Exact-SHA search Wayland acceptance

Run this gate only after the search-evidence commit is independently reviewed, published by normal fast-forward, and green in exact-tip CI. A developer build is not acceptance evidence.

## Fixture and package identity

1. Record the published commit and tree, exact-tip CI run, Arch package filename/version and SHA-256, Omarchy version, compositor, display scale, and assistive-technology versions.
2. Generate the bounded three-page searchable fixture from the same published source:

   ```sh
   cmake -S apps/desktop -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build/desktop --target zenpdf_document_widget_tests
   ZENPDF_SEARCH_ACCEPTANCE_FIXTURE=/tmp/zenpdf-search-alpha.pdf \
     QT_QPA_PLATFORM=offscreen \
     build/desktop/zenpdf_document_widget_tests \
     findsGeneratedTextAndActivatesResult
   sha256sum /tmp/zenpdf-search-alpha.pdf
   ```

   The destination must not already exist. The fixture has three pages and one `quokka` result on page 2. It proves a reproducible generated-text path only; it is not independent-producer interoperability evidence.
3. Install the exact CI-equivalent Arch package and launch its installed `zenpdf-launch` payload in the real Wayland session. Confirm the executable and package revision match the published SHA.

## Visible success checks

- Open the generated fixture through the native chooser and select the Search tab without using the pointer.
- Focus the search field, enter `quokka`, and confirm exactly one visible result identifies page 2 without exposing a private path.
- Move focus to the result and press Enter. Confirm the document view and `Current page` control navigate to page 2 and the matching text is highlighted where supported by the packaged Qt version.
- Clear or replace the query and enter a non-matching term. Confirm the prior selection and visible match highlight clear immediately and stale results disappear.
- Close the tab and application while a query is active. Confirm clean shutdown and no document content or full path in diagnostics.
- Record search-field/result-list AT-SPI names, roles, focus, selection, and announcements. Toolkit or chooser gaps remain product gaps.

Keep L012 and related security/release rows below `Verified` until Unicode, no-text, malformed/hostile, long-document resource and cancellation behavior, highlighting, assistive technology, independent producers, exact-tip CI, and installed exact-package evidence are complete. Qt PDF remains an in-process parser; this gate does not prove isolation or a hard time/memory bound.
