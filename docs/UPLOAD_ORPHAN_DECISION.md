# Stored-object orphan decision

## Decision required

The current direct-upload path cannot prove cleanup when storage accepted bytes but the worker never received, IPC-delivered, or journaled the returned storage ID. The same limitation applies when an anonymous browser uploads a file and never creates a job. The durable worker journal closes failures after the ID reaches the parent process; it cannot identify an ID that was never observed.

No change below is implemented. This is the remaining release-gate decision because fully closing the gap requires a backend cleanup policy or a constrained alternate upload path.

## Option 1: bounded age-gated `_storage` sweep

Use the current Convex system-storage query APIs in a scheduled internal cleanup operation. Enumerate a small, paginated batch of `_storage` metadata older than a conservative grace period, then retain every object referenced by an authoritative record: job inputs, job outputs, artifacts, or pending worker uploads. Delete only an old object proven absent from every authoritative reference.

Safety requirements:

- hard limits on rows inspected, objects deleted, bytes deleted, and wall time per run;
- a grace age longer than the maximum browser upload-to-job interval, worker upload deadline, retry interval, and clock-skew allowance;
- stable pagination with metrics for backlog age and size;
- a final reference recheck immediately before deletion;
- fail closed on incomplete queries, pagination errors, unknown schemas, or reference ambiguity;
- deletion evidence containing only storage ID, age, size, cleanup run ID, and the completed reference checks—never file content, signed URLs, names, or tokens;
- recovery-safe deletion: a missing object is success, while a referenced or newly referenced object is skipped;
- integration tests for browser upload/create-job races, worker registration/completion races, cleanup retries, pagination boundaries, and false-reference negatives.

This is the only option that can cover both large direct worker uploads and abandoned anonymous browser uploads. It introduces a destructive backend sweep, so it needs explicit owner approval and a staged dry-run/metrics rollout.

## Option 2: server-mediated HTTP action for smaller uploads

Route eligible uploads through an authenticated Convex HTTP action that accepts at most 20 MB, stores the body, and immediately binds it to a browser session or pending worker record. Enforce the 20 MB limit before and while reading, use one-time scoped authorization, reject chunked/oversize bodies, and keep request/action timeouts below the pending-record lifetime.

Safety requirements:

- exact content-length and streamed-byte enforcement with a 20 MB hard ceiling;
- one-time, operation-specific tickets tied to the intended session/job, filename-independent authorization, and replay rejection;
- idempotent binding and cleanup responses;
- stable sanitized errors and no body, URL, token, path, or filename logging;
- tests for disconnects before store, store/bind failure, duplicate delivery, action crash, and retry races.

This reduces the client-side unknown-ID window for small files, but an action can still fail after storage succeeds and before binding becomes authoritative. It therefore does not independently prove zero orphans without an age-gated sweep, and it cannot replace the general worker path that permits outputs up to 2 GiB.

## Option 3: retain direct uploads and document permanent orphans

Keep the present signed direct-upload paths and rely on the worker journal only after a storage ID is observed. Unknown-ID worker uploads and abandoned anonymous browser uploads can remain permanently unreferenced.

This has the smallest implementation cost and no destructive sweep risk, but it leaves unbounded retained data and no deletion proof. It is not acceptable for the current release gate.

## Recommendation

Approve Option 1 with a dry-run-first rollout, conservative age gate, bounded pagination, authoritative-reference recheck, and deletion audit metrics. Option 2 may later reduce load and improve behavior for files up to 20 MB, but it is complementary rather than a complete replacement. Until Option 1 or an equivalent authoritative lifecycle design is approved and implemented, the remaining unknown-ID orphan gap is a release blocker.
