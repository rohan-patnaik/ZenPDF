# ZenPDF Desktop security, recovery, and performance model

## Trust boundary

Every PDF, embedded object, attachment, font, image, form action, URI, and converted office document is untrusted input. A successful parse does not make embedded content safe. ZenPDF Desktop does not require accounts, network services, telemetry, or document uploads. It does not auto-open attachments or external links.

The Qt UI process owns user interaction and local state. Parsing/rendering and structural changes sit behind narrow adapters. Work that invokes broad parsers or converters should move to a separately launched, resource-bounded helper before its feature can be marked verified. Helpers receive explicit input/output paths, run without network access where the host supports it, and write a new output rather than mutating the source in place.

## Resource policy

The following initial budgets are release gates, not assumptions that libraries enforce them automatically:

| Resource | Initial budget |
| --- | --- |
| Open document size | 2 GiB hard rejection before parsing |
| Page count | 100,000 hard rejection; warning above 10,000 |
| Rendered page dimension | 32,768 pixels on either axis |
| In-process render and thumbnail cache | 256 MiB combined |
| Caller-declared undo payload retention | 64 MiB per document and 512 commands |
| Concurrent background document jobs | 2 per application |
| Extracted attachment or helper output | User-confirmed destination; 2 GiB per output |
| Diagnostic logs | Two local files of at most 1 MiB each |

Features must apply stricter limits when their engine cannot safely meet these ceilings. Cancellation is cooperative first and process termination is the last boundary for isolated helpers. A cancelled transformation must not replace the source or leave a result presented as complete.

## Local data and privacy

The local state database contains recent paths, timestamps, preferences, index metadata, recovery metadata, and future audit entries. Logs contain application events and error categories; document bytes, extracted text, form values, passwords, cryptographic keys, and full user paths must not be logged by default. Users must be able to clear recent history and derived indexes without touching their documents.

## Save and crash recovery design

Document changes use a command journal with deterministic undo/redo records. Autosave metadata references the source identity and a private working copy; it never modifies the source. Each save follows this sequence:

1. Write the complete result to a unique file in the destination directory.
2. Flush the file and validate that it can be reopened with the expected page count and requested changes.
3. Preserve destination permissions where possible.
4. Atomically replace the chosen destination only after validation.
5. Remove recovery metadata only after the replacement and state commit succeed.

On startup, ZenPDF offers recoverable sessions rather than silently reopening hostile input. Stale working files are listed with their source and timestamp and require an explicit recovery or discard choice. Symlinks, ownership changes, destination changes, insufficient space, and cross-filesystem replacement are treated as save failures, not reasons to weaken atomicity.

The current implementation establishes a 512-command in-memory undo limit, a configurable per-document admission ceiling that defaults to 64 MiB of caller-declared retained payload costs, clean-revision dirty tracking, active-tab undo/redo routing, and dirty-close confirmation. Admission fails before command execution or transfer to the Qt undo stack when a command is missing, individually over budget, would overflow the counter, or would exceed the cumulative ceiling. Commands cannot merge; undo/redo retain their costs, and redo-branch replacement or oldest-command eviction releases the corresponding declarations.

This accounting does not inspect allocations or prove a heap bound. It depends on future command producers declaring complete retained payload costs and excludes command object and Qt framework overhead. Producer-specific measurement, under-declaration tests, and an allocator/process-level memory boundary remain required before strict completion. No PDF mutation command, atomic save/save-as path, autosave working copy, serialized recovery journal, or startup recovery workflow exists yet; the sequence above remains the gate for those later slices.

## Failure behavior

Corrupt, encrypted, unsupported, or resource-exhausting documents fail closed with an actionable local message. The UI remains responsive, partial organizer output is deleted, and the original remains untouched. Passwords live only for the active operation and are never written to state or logs. URI launches show the normalized destination and require consent; non-HTTP schemes are denied unless separately reviewed.

## Validation strategy

- Unit tests cover state migrations, command/range validation, bounds, and adapter argument construction.
- Integration fixtures cover malformed object graphs, extreme dimensions, encryption, forms, annotations, page edits, cancellation, and save/reopen.
- Sanitizer and fuzz jobs target adapters and helper protocols before broad format support is marked verified.
- Real Arch/Wayland and Omarchy Quattro validation remains a release gate because offscreen CI cannot reproduce compositor, portal, printing, or assistive-technology behavior.
