# Exact-SHA local-state privacy acceptance

Run this gate only after the L016 implementation commit is independently reviewed, published by normal fast-forward, green in exact-tip CI, and built into the exact Arch package under test. Source tests are necessary evidence, not installed acceptance.

## Identity and setup

1. Record the commit and tree, exact-tip CI run, source archive and package filenames and SHA-256 hashes, installed package version, executable hash, Omarchy version, compositor, and display scale.
2. Preserve the approved rollback package and its hashes before installing the candidate. Confirm the source checkout remains unchanged.
3. In a task-owned test account or disposable state root, record the existing application-data state. Do not replace or weaken a user's real state to create hostile fixtures.
4. Use one approved PDF whose absolute path contains a unique non-secret sentinel. Record its SHA-256 before and after every run.

## Equivalent native entry paths

Run the exact installed payload through each path, serially, with no other ZenPDF process present:

1. Start `/usr/bin/zenpdf` from a shell whose umask is 022.
2. Start the desktop/MIME entry for the approved PDF.
3. Start the installed `zenpdf-launch` command.

For every path, while the application is live, verify the application-data leaf is 0700 and every existing `state.sqlite3`, `state.sqlite3-wal`, and `state.sqlite3-shm` is a regular, single-link, effective-user-owned 0600 file. Confirm the approved recent path appears only inside those private SQLite files, not in product diagnostics. After clean shutdown, verify the directory and database remain private; absent WAL/SHM files are valid after close.

## Purge and hostile-state checks

Use the UI to clear recent history, then allow the application's checkpoint, `VACUUM`, final checkpoint, connection close, and clean shutdown to finish. Binary-scan the database and any remaining WAL/SHM files and require the approved sentinel path to be absent.

In a disposable state root, separately test a leaf symlink, database symlink, wrong-owner object, FIFO or other non-regular database, multi-link database, 0777 leaf, and 0666 database/WAL/SHM. Each case must fail closed without target/content/mode mutation, private path or basename disclosure, raw SQLite/driver output, hang, crash, or residual ZenPDF process. Record before/after metadata and hashes for every applicable object. Run ownership cases with normal filesystem ownership controls; do not add test-only production hooks.

## Reconciliation

Record shutdown behavior, AT-SPI modal name/role/focus smoke, package/source hashes, state metadata, sentinel scans, source preservation, and all command outputs. Reconcile the installed executable and package to the exact reviewed commit and tree.

This gate passed for implementation commit `99b75284775f73ec81d249d003691796a13e92ca` and its exact package. The independently accepted, redacted criterion reconciliation is recorded in [`L016_ACCEPTANCE_EVIDENCE.md`](L016_ACCEPTANCE_EVIDENCE.md). L016 is `Verified`; unrelated product-wide release and accessibility gaps do not reopen this row.
