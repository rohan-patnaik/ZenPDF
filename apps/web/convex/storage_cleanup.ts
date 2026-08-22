import { makeFunctionReference } from "convex/server";
import { v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import {
  internalAction,
  internalMutation,
  type MutationCtx,
} from "./_generated/server";

const STATE_NAME = "storage-orphan-sweep-v1";
const MIN_GRACE_MS = 48 * 60 * 60 * 1000;
const MAX_GRACE_MS = 30 * 24 * 60 * 60 * 1000;
const DEFAULT_GRACE_MS = 72 * 60 * 60 * 1000;
const MAX_INSPECTED = 100;
const DEFAULT_INSPECTED = 50;
const MAX_DELETED = 10;
const DEFAULT_DELETED = 5;
const MAX_BYTES_DELETED = 256 * 1024 * 1024;
const DEFAULT_BYTES_DELETED = 128 * 1024 * 1024;
const MAX_WALL_MS = 5_000;
const DEFAULT_WALL_MS = 1_000;
const MAX_BACKFILL_JOBS = 50;
const DEFAULT_BACKFILL_JOBS = 25;
const MAX_REFERENCES_PER_JOB = 100;

type CleanupMode = "dryRun" | "delete";

const boundedInteger = (
  value: string | undefined,
  fallback: number,
  minimum: number,
  maximum: number,
) => {
  if (value === undefined || !/^\d+$/.test(value)) {
    return fallback;
  }
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed >= minimum
    ? Math.min(parsed, maximum)
    : fallback;
};

const getConfiguredBounds = () => ({
  graceMs: boundedInteger(
    process.env.ZENPDF_STORAGE_SWEEP_GRACE_HOURS,
    DEFAULT_GRACE_MS / (60 * 60 * 1000),
    MIN_GRACE_MS / (60 * 60 * 1000),
    MAX_GRACE_MS / (60 * 60 * 1000),
  ) * 60 * 60 * 1000,
  maxInspected: boundedInteger(
    process.env.ZENPDF_STORAGE_SWEEP_MAX_INSPECTED,
    DEFAULT_INSPECTED,
    1,
    MAX_INSPECTED,
  ),
  maxDeleted: boundedInteger(
    process.env.ZENPDF_STORAGE_SWEEP_MAX_DELETED,
    DEFAULT_DELETED,
    1,
    MAX_DELETED,
  ),
  maxBytesDeleted: boundedInteger(
    process.env.ZENPDF_STORAGE_SWEEP_MAX_BYTES,
    DEFAULT_BYTES_DELETED,
    1,
    MAX_BYTES_DELETED,
  ),
  maxWallMs: boundedInteger(
    process.env.ZENPDF_STORAGE_SWEEP_MAX_WALL_MS,
    DEFAULT_WALL_MS,
    50,
    MAX_WALL_MS,
  ),
  maxBackfillJobs: boundedInteger(
    process.env.ZENPDF_STORAGE_BACKFILL_MAX_JOBS,
    DEFAULT_BACKFILL_JOBS,
    1,
    MAX_BACKFILL_JOBS,
  ),
});

const getState = async (ctx: MutationCtx) =>
  ctx.db
    .query("storageCleanupState")
    .withIndex("by_name", (q) => q.eq("name", STATE_NAME))
    .unique();

const storageReferenceExists = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
) =>
  (await ctx.db
    .query("storageReferences")
    .withIndex("by_storage", (q) => q.eq("storageId", storageId))
    .first()) !== null;

const artifactExists = async (ctx: MutationCtx, storageId: Id<"_storage">) =>
  (await ctx.db
    .query("artifacts")
    .withIndex("by_storage", (q) => q.eq("storageId", storageId))
    .first()) !== null;

const livePendingUploadExists = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
  now: number,
) => {
  const records = await ctx.db
    .query("pendingUploads")
    .withIndex("by_storage", (q) => q.eq("storageId", storageId))
    .take(2);
  return records.some((record) => record.expiresAt > now);
};

const liveReservationExists = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
  now: number,
) => {
  const records = await ctx.db
    .query("browserUploadReservations")
    .withIndex("by_storage", (q) => q.eq("storageId", storageId))
    .take(2);
  return records.some(
    (record) =>
      record.status === "bound" && record.expiresAt > now,
  );
};

const hasAuthoritativeReference = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
  now: number,
) => {
  const [jobReference, artifact, pendingUpload, reservation] =
    await Promise.all([
      storageReferenceExists(ctx, storageId),
      artifactExists(ctx, storageId),
      livePendingUploadExists(ctx, storageId, now),
      liveReservationExists(ctx, storageId, now),
    ]);
  return { jobReference, artifact, pendingUpload, reservation };
};

export const backfillStorageReferences = internalMutation({
  args: { maxJobs: v.number() },
  handler: async (ctx, args) => {
    const now = Date.now();
    const maxJobs = Math.max(
      1,
      Math.min(Math.floor(args.maxJobs), MAX_BACKFILL_JOBS),
    );
    let state = await getState(ctx);
    if (!state) {
      const stateId = await ctx.db.insert("storageCleanupState", {
        name: STATE_NAME,
        sweepCycle: 0,
        backfillStatus: "pending",
        updatedAt: now,
      });
      state = await ctx.db.get(stateId);
    }
    if (!state || state.backfillStatus === "blocked") {
      return { status: "blocked" as const, processed: 0 };
    }
    if (state.backfillStatus === "complete") {
      return { status: "complete" as const, processed: 0 };
    }

    const page = await ctx.db
      .query("jobs")
      .order("asc")
      .paginate({ cursor: state.backfillCursor ?? null, numItems: maxJobs });
    if (page.pageStatus === "SplitRequired") {
      await ctx.db.patch(state._id, {
        backfillStatus: "blocked",
        updatedAt: now,
      });
      return { status: "blocked" as const, processed: 0 };
    }

    for (const job of page.page) {
      const references = [
        ...job.inputs.map((input) => ({
          storageId: input.storageId,
          kind: "jobInput" as const,
        })),
        ...(job.outputs ?? []).map((output) => ({
          storageId: output.storageId,
          kind: "jobOutput" as const,
        })),
      ];
      if (references.length > MAX_REFERENCES_PER_JOB * 2) {
        await ctx.db.patch(state._id, {
          backfillStatus: "blocked",
          updatedAt: now,
        });
        return { status: "blocked" as const, processed: 0 };
      }
      const existing = await ctx.db
        .query("storageReferences")
        .withIndex("by_job_kind", (q) => q.eq("jobId", job._id))
        .take(MAX_REFERENCES_PER_JOB * 2 + 1);
      if (existing.length > MAX_REFERENCES_PER_JOB * 2) {
        await ctx.db.patch(state._id, {
          backfillStatus: "blocked",
          updatedAt: now,
        });
        return { status: "blocked" as const, processed: 0 };
      }
      const keys = new Set(
        existing.map((reference) => `${reference.kind}:${reference.storageId}`),
      );
      for (const reference of references) {
        const key = `${reference.kind}:${reference.storageId}`;
        if (!keys.has(key)) {
          await ctx.db.insert("storageReferences", {
            ...reference,
            jobId: job._id,
            createdAt: now,
          });
          keys.add(key);
        }
      }
    }

    await ctx.db.patch(state._id, {
      backfillCursor: page.isDone ? undefined : page.continueCursor,
      backfillStatus: page.isDone ? "complete" : "pending",
      updatedAt: now,
    });
    return {
      status: page.isDone ? ("complete" as const) : ("pending" as const),
      processed: page.page.length,
    };
  },
});

export const markStorageCandidates = internalMutation({
  args: {
    mode: v.union(v.literal("dryRun"), v.literal("delete")),
    graceMs: v.number(),
    maxInspected: v.number(),
    maxDeleted: v.number(),
    maxBytesDeleted: v.number(),
    maxWallMs: v.number(),
  },
  handler: async (ctx, args) => {
    const startedAt = Date.now();
    const state = await getState(ctx);
    const graceMs = Math.max(
      MIN_GRACE_MS,
      Math.min(Math.floor(args.graceMs), MAX_GRACE_MS),
    );
    const maxInspected = Math.max(
      1,
      Math.min(Math.floor(args.maxInspected), MAX_INSPECTED),
    );
    const maxDeleted = Math.max(
      1,
      Math.min(Math.floor(args.maxDeleted), MAX_DELETED),
    );
    const maxBytesDeleted = Math.max(
      1,
      Math.min(Math.floor(args.maxBytesDeleted), MAX_BYTES_DELETED),
    );
    const maxWallMs = Math.max(
      50,
      Math.min(Math.floor(args.maxWallMs), MAX_WALL_MS),
    );
    const runId = await ctx.db.insert("storageCleanupRuns", {
      mode: args.mode,
      status: "running",
      cursorIn: state?.sweepCursor,
      inspected: 0,
      eligible: 0,
      eligibleBytes: 0,
      protected: 0,
      candidates: 0,
      candidateBytes: 0,
      deleted: 0,
      bytesDeleted: 0,
      maxInspected,
      maxDeleted,
      maxBytesDeleted,
      maxWallMs,
      startedAt,
    });
    if (!state || state.backfillStatus !== "complete") {
      await ctx.db.patch(runId, {
        status: "failed",
        errorCode: "REFERENCE_BACKFILL_INCOMPLETE",
        completedAt: Date.now(),
      });
      return { runId, status: "failed" as const, candidateIds: [] };
    }

    const page = await ctx.db.system
      .query("_storage")
      .order("asc")
      .paginate({ cursor: state.sweepCursor ?? null, numItems: maxInspected });
    if (
      page.pageStatus === "SplitRequired" ||
      page.page.length > maxInspected
    ) {
      await ctx.db.patch(runId, {
        status: "failed",
        errorCode: "INCOMPLETE_STORAGE_PAGE",
        completedAt: Date.now(),
      });
      return { runId, status: "failed" as const, candidateIds: [] };
    }

    let eligible = 0;
    let eligibleBytes = 0;
    let protectedCount = 0;
    let oldestEligibleAt: number | undefined;
    const candidates: Array<{
      _id: Id<"_storage">;
      _creationTime: number;
      size: number;
    }> = [];
    for (const storage of page.page) {
      if (Date.now() - startedAt >= maxWallMs) {
        await ctx.db.patch(runId, {
          status: "failed",
          inspected: page.page.length,
          eligible,
          eligibleBytes,
          protected: protectedCount,
          candidates: 0,
          candidateBytes: 0,
          errorCode: "WALL_TIME_BOUND",
          completedAt: Date.now(),
        });
        return { runId, status: "failed" as const, candidateIds: [] };
      }
      if (storage._creationTime > startedAt - graceMs) {
        continue;
      }
      eligible += 1;
      eligibleBytes += storage.size;
      oldestEligibleAt = Math.min(
        oldestEligibleAt ?? storage._creationTime,
        storage._creationTime,
      );
      const cleanupRecords = await ctx.db
        .query("storageCleanupRecords")
        .withIndex("by_storage", (q) => q.eq("storageId", storage._id))
        .take(2);
      if (cleanupRecords.length > 1) {
        await ctx.db.patch(runId, {
          status: "failed",
          inspected: page.page.length,
          eligible,
          eligibleBytes,
          protected: protectedCount,
          candidates: 0,
          candidateBytes: 0,
          errorCode: "AMBIGUOUS_CLEANUP_STATE",
          completedAt: Date.now(),
        });
        return { runId, status: "failed" as const, candidateIds: [] };
      }
      if (cleanupRecords.length === 1) {
        protectedCount += 1;
        continue;
      }
      const references = await hasAuthoritativeReference(
        ctx,
        storage._id,
        startedAt,
      );
      if (Object.values(references).some(Boolean)) {
        protectedCount += 1;
        continue;
      }
      if (storage.size > maxBytesDeleted) {
        protectedCount += 1;
        continue;
      }
      candidates.push(storage);
    }

    const candidateIds: Id<"storageCleanupRecords">[] = [];
    if (args.mode === "delete") {
      for (const storage of candidates.slice(0, maxDeleted)) {
        candidateIds.push(
          await ctx.db.insert("storageCleanupRecords", {
            storageId: storage._id,
            state: "candidate",
            runId,
            observedCreatedAt: storage._creationTime,
            observedSizeBytes: storage.size,
            candidateAt: startedAt,
            checkedJobReferences: true,
            checkedArtifacts: true,
            checkedPendingUploads: true,
            checkedReservations: true,
          }),
        );
      }
    }

    await ctx.db.patch(state._id, {
      sweepCursor: page.isDone ? undefined : page.continueCursor,
      sweepCycle: page.isDone ? state.sweepCycle + 1 : state.sweepCycle,
      updatedAt: Date.now(),
    });
    await ctx.db.patch(runId, {
      status: "completed",
      cursorOut: page.isDone ? undefined : page.continueCursor,
      inspected: page.page.length,
      eligible,
      eligibleBytes,
      protected: protectedCount,
      candidates: candidates.length,
      candidateBytes: candidates.reduce(
        (total, storage) => total + storage.size,
        0,
      ),
      oldestEligibleAt,
      completedAt: Date.now(),
    });
    return { runId, status: "completed" as const, candidateIds };
  },
});

export const finalizeStorageCandidates = internalMutation({
  args: {
    runId: v.optional(v.id("storageCleanupRuns")),
    maxDeleted: v.number(),
    maxBytesDeleted: v.number(),
    maxWallMs: v.number(),
  },
  handler: async (ctx, args) => {
    const startedAt = Date.now();
    const state = await getState(ctx);
    const firstCandidate = args.runId
      ? null
      : await ctx.db
          .query("storageCleanupRecords")
          .withIndex("by_state", (q) => q.eq("state", "candidate"))
          .order("asc")
          .first();
    const runId = args.runId ?? firstCandidate?.runId;
    const run = runId ? await ctx.db.get(runId) : null;
    if (
      !state ||
      state.backfillStatus !== "complete" ||
      !run ||
      run.mode !== "delete" ||
      run.status !== "completed"
    ) {
      return { deleted: 0, bytesDeleted: 0, protected: 0 };
    }
    const maxDeleted = Math.max(
      1,
      Math.min(Math.floor(args.maxDeleted), MAX_DELETED),
    );
    const maxBytesDeleted = Math.max(
      1,
      Math.min(Math.floor(args.maxBytesDeleted), MAX_BYTES_DELETED),
    );
    const maxWallMs = Math.max(
      50,
      Math.min(Math.floor(args.maxWallMs), MAX_WALL_MS),
    );
    const records = await ctx.db
      .query("storageCleanupRecords")
      .withIndex("by_run_state", (q) =>
        q.eq("runId", runId!).eq("state", "candidate"),
      )
      .take(maxDeleted);
    let deleted = 0;
    let bytesDeleted = 0;
    let protectedCount = 0;

    for (const record of records) {
      if (Date.now() - startedAt >= maxWallMs) {
        break;
      }
      const metadata = await ctx.db.system.get(record.storageId);
      const references = await hasAuthoritativeReference(
        ctx,
        record.storageId,
        startedAt,
      );
      if (Object.values(references).some(Boolean)) {
        await ctx.db.delete(record._id);
        protectedCount += 1;
        continue;
      }
      const deletionBytes = metadata?.size ?? 0;
      if (
        deletionBytes > maxBytesDeleted ||
        bytesDeleted + deletionBytes > maxBytesDeleted
      ) {
        continue;
      }
      if (metadata) {
        await ctx.storage.delete(record.storageId);
      }
      const expiredReservations = await ctx.db
        .query("browserUploadReservations")
        .withIndex("by_storage", (q) => q.eq("storageId", record.storageId))
        .take(2);
      for (const reservation of expiredReservations) {
        if (reservation.expiresAt <= startedAt) {
          await ctx.db.patch(reservation._id, {
            status: "deleted",
            deletedAt: startedAt,
          });
        }
      }
      await ctx.db.patch(record._id, {
        state: "deleted",
        deletedAt: startedAt,
        checkedJobReferences: true,
        checkedArtifacts: true,
        checkedPendingUploads: true,
        checkedReservations: true,
      });
      deleted += 1;
      bytesDeleted += deletionBytes;
    }
    await ctx.db.patch(run._id, {
      deleted: run.deleted + deleted,
      bytesDeleted: run.bytesDeleted + bytesDeleted,
      protected: run.protected + protectedCount,
    });
    return { deleted, bytesDeleted, protected: protectedCount };
  },
});

const backfillReference = makeFunctionReference<
  "mutation",
  { maxJobs: number },
  { status: "pending" | "complete" | "blocked"; processed: number }
>("storage_cleanup:backfillStorageReferences");

const markReference = makeFunctionReference<
  "mutation",
  {
    mode: CleanupMode;
    graceMs: number;
    maxInspected: number;
    maxDeleted: number;
    maxBytesDeleted: number;
    maxWallMs: number;
  },
  {
    runId: Id<"storageCleanupRuns">;
    status: "completed" | "failed";
    candidateIds: Id<"storageCleanupRecords">[];
  }
>("storage_cleanup:markStorageCandidates");

const finalizeReference = makeFunctionReference<
  "mutation",
  {
    runId?: Id<"storageCleanupRuns">;
    maxDeleted: number;
    maxBytesDeleted: number;
    maxWallMs: number;
  },
  { deleted: number; bytesDeleted: number; protected: number }
>("storage_cleanup:finalizeStorageCandidates");

export const runStorageCleanup = internalAction({
  args: {},
  handler: async (ctx) => {
    const bounds = getConfiguredBounds();
    const backfill = await ctx.runMutation(backfillReference, {
      maxJobs: bounds.maxBackfillJobs,
    });
    if (backfill.status !== "complete") {
      return { mode: "dryRun" as const, backfill: backfill.status };
    }
    const mode: CleanupMode =
      process.env.ZENPDF_STORAGE_SWEEP_DELETE_ENABLED === "1"
        ? "delete"
        : "dryRun";
    const retried =
      mode === "delete"
        ? await ctx.runMutation(finalizeReference, {
            maxDeleted: bounds.maxDeleted,
            maxBytesDeleted: bounds.maxBytesDeleted,
            maxWallMs: bounds.maxWallMs,
          })
        : undefined;
    const marked = await ctx.runMutation(markReference, {
      mode,
      graceMs: bounds.graceMs,
      maxInspected: bounds.maxInspected,
      maxDeleted: bounds.maxDeleted,
      maxBytesDeleted: bounds.maxBytesDeleted,
      maxWallMs: bounds.maxWallMs,
    });
    if (mode === "delete" && marked.status === "completed") {
      const finalized = await ctx.runMutation(finalizeReference, {
        runId: marked.runId,
        maxDeleted: bounds.maxDeleted,
        maxBytesDeleted: bounds.maxBytesDeleted,
        maxWallMs: bounds.maxWallMs,
      });
      return {
        mode,
        backfill: backfill.status,
        retried,
        ...marked,
        ...finalized,
      };
    }
    return { mode, backfill: backfill.status, ...marked };
  },
});
