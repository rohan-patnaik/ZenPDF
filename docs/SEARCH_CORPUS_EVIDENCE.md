# Search corpus and keyboard evidence

This source-level gate supplements the generated unit fixtures. It does not establish
parser isolation, a hard time or memory bound, complete interoperability, installed
assistive-technology behavior, or spoken output.

## Corpus provenance and integrity

`apps/desktop/tests/search-corpus/generate-fixtures.py` deterministically produces the
compact corpus from source-owned text and shapes using ReportLab/Pillow, with qpdf
used only to create a deliberately weak, deterministic AES-128 compatibility fixture.
Those authoring tools are optional test tools, not runtime or build dependencies. The
PDFs are not committed because the long, result-flood, decompression, and
extreme-page-box inputs should remain opt-in hostile-test material. The reviewed files
total less than 420 KiB.

`apps/desktop/tests/search-corpus/SHA256SUMS` identifies the exact approved external
files. The generator requires a new explicit external output directory, refuses every
existing target, and verifies its output against the same manifest. The runner verifies
every hash before and after probing, so search must leave all sources byte-identical.

Generate the exact approved corpus with the pinned test-authoring environment, never
inside the source checkout:

```sh
/path/to/python-with-reportlab-and-pillow \
  apps/desktop/tests/search-corpus/generate-fixtures.py \
  /new/external/zenpdf-search-corpus-v1
```

The generator uses qpdf's insecure static-ID/static-IV switches solely to make the
AES-128 test fixture reproducible. It must never be used to create user encryption.
The reviewed authoring set is Python 3.12.13, ReportLab 4.4.9, Pillow 12.3.0,
qpdf 12.4.0, and `NotoSans-Regular.ttf` SHA-256
`478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823`.
Different authoring inputs are permitted only when the generated files still verify
against the immutable manifest.

Build and run the opt-in gate with an approved corpus directory:

```sh
cmake -S apps/desktop -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/desktop --target zenpdf_search_corpus_probe
apps/desktop/tests/search-corpus/run-corpus.sh \
  build/desktop/zenpdf_search_corpus_probe /path/to/approved/search-corpus
```

Each probe has both an internal observation duration and an external kill deadline.
The fatal malformed case must return the documented load failure; every other case
must open read-only. The encrypted case supplies the test-only `reader` password and
searches after unlock rather than merely checking password acceptance.

## Recorded source-level observations

The reviewed Qt 6.11.2 corpus run produced the following observations. RSS is sampled
process evidence, not a product limit, and result counts at the deadline do not imply
search completion.

| Case | Observation |
| --- | --- |
| Independent ReportLab Unicode | `quokka`: 3, composed `café`: 1, Cyrillic `Привет`: 1 in 3 s windows |
| Image-only/no text layer | visible `wombat`: 0 text results in a 3 s window; no OCR claim |
| AES-128 test encryption | wrong password: load error 5; `quokka` after test password: 3 results in a 3 s window |
| Fatal malformed | load error, zero pages, no search started |
| Recoverable malformed xref | opened read-only; `quokka`: 3 results in a 3 s window |
| 32 MiB Flate expansion | 93,788 KiB maximum RSS; no hard bound claim |
| Extreme page box | 272,188 KiB maximum RSS; no hard bound claim |
| Result flood | all 4,000 rows exposed by 10 s; 31,572 KiB maximum RSS; no result cap exists |
| 400-page late hit | page-399 target absent at 25 s; 29,724 KiB maximum RSS; zero must not be announced as completion |

These outcomes keep L012, L039, L071-L073, and L076 below `Verified`.

## Keyboard and accessibility contract

`DocumentWidgetTest::traversesSearchControlsByKeyboard` exercises the source-level
contract in the offscreen Qt platform:

1. Select the Search tab and focus `Search document` (`EditableText`).
2. Type a query; `Search status` is a `StaticText` description whose accessible name
   changes from `Enter text to search.` to the visible found-so-far count.
3. Press Tab to reach `Document search results` (`List`), Down to select the result,
   and Enter to activate it and navigate to the matching page.
4. Press Shift+Tab to return to the query, clear it, and confirm focus stays in the
   field while selection and highlight reset. Type a replacement query and confirm
   the visible/accessibility status changes without moving focus.

The installed Wayland gate must still capture AT-SPI names, roles, focus, selection,
status events, and actual Orca speech. This source test does not claim that status
changes are announced aloud.
