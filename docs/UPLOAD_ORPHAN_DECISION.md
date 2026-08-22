# Stored-object orphan decision

## Decision

The current direct-upload path cannot prove cleanup when storage accepted bytes but the worker never received, IPC-delivered, or journaled the returned storage ID. The same limitation applies when an anonymous browser uploads a file and never creates a job. The durable worker journal closes failures after the ID reaches the parent process; it cannot identify an ID that was never observed.

Option 1 was owner-approved on 2026-08-22 and is implemented in this repository. Browser uploads now require server-issued reservations and atomic job consumption. The scheduled `_storage` sweep is deployed in dry-run mode unless an operator explicitly sets `ZENPDF_STORAGE_SWEEP_DELETE_ENABLED=1`; deletion must not be enabled until reference backfill is complete and dry-run metrics have been reviewed.

The remaining release work is operational validation of at least one complete dry-run pagination cycle and the deliberate production enablement decision. This document does not claim that those deployment steps have occurred.

## Option 1: bounded age-gated `_storage` sweep

Pair the current Convex system-storage query APIs with a server-authoritative browser reservation ledger. Before issuing a browser upload URL, create a one-time ticket bound to the authenticated user or anonymous session, intended input constraints, issuance time, and an enforced expiry. After direct upload, bind the returned storage ID to that ticket. `createJob` must validate that every browser input is backed by the caller's live bound ticket and atomically consume the tickets in the same mutation that inserts the job; accepting a bare browser-provided storage ID is forbidden.

A scheduled internal cleanup operation then enumerates a small, paginated batch of `_storage` metadata older than a conservative grace period. It retains every object referenced by an authoritative job input/output, artifact, pending worker upload, or live browser reservation. It deletes only an old candidate proven absent from all authoritative references and reservations.

Safety requirements:

- hard limits on rows inspected, objects deleted, bytes deleted, and wall time per run;
- a grace age longer than the maximum browser upload-to-job interval, worker upload deadline, retry interval, and clock-skew allowance;
- server-enforced reservation expiry with no client-controlled extension, bounded outstanding tickets per identity, one-time binding, and one-time atomic consumption;
- `createJob` ownership, expiry, storage-existence, size, and ticket-binding validation in the same transaction as job insertion and ticket consumption;
- stable pagination with metrics for backlog age and size;
- candidate marking followed by a final atomic reference/reservation recheck and delete transition, serialized against reservation binding and `createJob` consumption;
- a deletion tombstone or equivalent serialized state so binding or job creation arriving after the delete decision is rejected and cannot resurrect a deleted storage reference;
- fail closed on incomplete queries, pagination errors, unknown schemas, or reference ambiguity;
- deletion evidence containing only storage ID, age, size, cleanup run ID, and the completed reference checks—never file content, signed URLs, names, or tokens;
- recovery-safe deletion: a missing object is success, while a referenced or newly referenced object is skipped;
- integration tests for reservation expiry/ownership/replay, bind-versus-expire, bind-versus-delete, `createJob`-versus-delete, post-delete binding rejection, transaction rollback, worker registration/completion races, cleanup retries, pagination boundaries, and false-reference negatives;
- a deterministic serialization test proving that every race ends in exactly one safe state: live authoritative reference, live reservation, or deleted object with all later binds/consumes rejected.

This is the only option that can cover both large direct worker uploads and abandoned anonymous browser uploads while also preventing unreserved storage IDs from entering jobs. It introduces a new authoritative reservation lifecycle and a destructive backend sweep, so it needs explicit owner approval, schema/protocol review, and a staged dry-run/metrics rollout.

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

## Implementation and rollout

The implementation uses:

- one-time, 15-minute browser reservations bound to the authenticated user or anonymous session;
- binding only after reference backfill is complete, with an exact defined content type, an object creation time inside the ticket window, and no existing job, artifact, pending-upload, reservation, or cleanup ownership;
- atomic reservation validation, repeated storage metadata validation, job-reference insertion, job creation, and reservation consumption;
- a bounded legacy job-reference backfill that gates every destructive sweep;
- a 72-hour default storage age gate with a non-configurable 48-hour minimum;
- stable `_storage` pagination with bounded page size, plus an action-wide monotonic wall-time cap shared by backfill, retries, and new work and delete-count/byte caps shared by retry and new work; an object above the ordinary byte budget may be deleted only by itself and remains hard-capped at the worker's 2 GiB output limit;
- a durable candidate record followed by a separate mutation that rechecks job references, artifacts, live pending worker uploads, and live browser reservations before deletion;
- the same serialized ownership recheck in legacy artifact/pending expiry cleanup, so a live owner always retains shared bytes;
- producer-side rejection of browser plan limits, upload intents, and worker output-limit configuration above the authoritative 2 GiB ceiling;
- a retained deletion tombstone that causes later browser binds, job creation, or worker registration to fail;
- run evidence limited to storage IDs, object age/size, aggregate backlog metrics, cleanup run IDs, and completed check flags.

The focused integration suite covers reservation ownership/expiry/replay and transaction rollback, candidate-versus-bind serialization, post-delete binding rejection, legacy-reference backfill, worker registration/completion protection, pagination boundaries, retry, missing-object success, false-reference negatives, and tombstone evidence. Option 2 remains complementary and is not implemented.
