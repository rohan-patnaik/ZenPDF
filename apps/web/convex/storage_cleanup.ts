import { v } from "convex/values";

import { internal } from "./_generated/api";
import type { Id } from "./_generated/dataModel";
import {
  internalAction,
  internalMutation,
  type MutationCtx,
} from "./_generated/server";
import {
  STORAGE_CLEANUP_STATE_NAME,
  storageIdHasLiveAuthoritativeOwner,
} from "./lib/storage_ownership";

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
export const MAX_STORAGE_OBJECT_BYTES = 2 * 1024 * 1024 * 1024;

type CleanupMode = "dryRun" | "delete";
type BackfillResult = {
  status: "pending" | "complete" | "blocked";
  processed: number;
};
type FinalizeResult = {
  deleted: number;
  bytesDeleted: number;
  protected: number;
};
type MarkResult = {
  runId: Id<"storageCleanupRuns">;
  status: "completed" | "failed";
  candidateIds: Id<"storageCleanupRecords">[];
};
type CleanupActionResult = {
  mode: CleanupMode;
  backfill: BackfillResult["status"];
  runId?: Id<"storageCleanupRuns">;
  status?: MarkResult["status"];
  candidateIds?: Id<"storageCleanupRecords">[];
  retried?: FinalizeResult;
  deleted?: number;
  bytesDeleted?: number;
  protected?: number;
};

export const canDeleteStorageObject = (
  objectBytes: number,
  maxBytesDeleted: number,
  deletedAlready: number,
  bytesDeletedAlready: number,
) =>
  Number.isSafeInteger(objectBytes) &&
  objectBytes >= 0 &&
  objectBytes <= MAX_STORAGE_OBJECT_BYTES &&
  (bytesDeletedAlready + objectBytes <= maxBytesDeleted ||
    (deletedAlready === 0 && bytesDeletedAlready === 0));

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
  graceMs:
    boundedInteger(
      process.env.ZENPDF_STORAGE_SWEEP_GRACE_HOURS,
      DEFAULT_GRACE_MS / (60 * 60 * 1000),
      MIN_GRACE_MS / (60 * 60 * 1000),
      MAX_GRACE_MS / (60 * 60 * 1000),
    ) *
    60 *
    60 *
    1000,
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
    .withIndex("by_name", (q) => q.eq("name", STORAGE_CLEANUP_STATE_NAME))
    .unique();

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
        name: STORAGE_CLEANUP_STATE_NAME,
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
    deadlineAt: v.optional(v.number()),
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
      0,
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
    const deadlineAt = Math.min(
      args.deadlineAt ?? startedAt + maxWallMs,
      startedAt + maxWallMs,
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
    if (Date.now() >= deadlineAt) {
      await ctx.db.patch(runId, {
        status: "failed",
        errorCode: "WALL_TIME_BOUND",
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
      if (Date.now() >= deadlineAt) {
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
      const cleanupRecord = await ctx.db
        .query("storageCleanupRecords")
        .withIndex("by_storage", (q) => q.eq("storageId", storage._id))
        .first();
      if (cleanupRecord) {
        protectedCount += 1;
        continue;
      }
      if (
        await storageIdHasLiveAuthoritativeOwner(ctx, storage._id, startedAt)
      ) {
        protectedCount += 1;
        continue;
      }
      if (storage.size > MAX_STORAGE_OBJECT_BYTES) {
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
    deletedAlready: v.optional(v.number()),
    bytesDeletedAlready: v.optional(v.number()),
    deadlineAt: v.optional(v.number()),
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
      0,
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
    const deletedAlready = Math.max(0, Math.floor(args.deletedAlready ?? 0));
    const bytesDeletedAlready = Math.max(
      0,
      Math.floor(args.bytesDeletedAlready ?? 0),
    );
    const deadlineAt = Math.min(
      args.deadlineAt ?? startedAt + maxWallMs,
      startedAt + maxWallMs,
    );
    const remainingDeletes = Math.max(0, maxDeleted - deletedAlready);
    if (remainingDeletes === 0 || Date.now() >= deadlineAt) {
      return { deleted: 0, bytesDeleted: 0, protected: 0 };
    }
    const records = await ctx.db
      .query("storageCleanupRecords")
      .withIndex("by_run_state", (q) =>
        q.eq("runId", runId!).eq("state", "candidate"),
      )
      .take(remainingDeletes);
    let deleted = 0;
    let bytesDeleted = 0;
    let protectedCount = 0;

    for (const record of records) {
      if (Date.now() >= deadlineAt) {
        break;
      }
      const metadata = await ctx.db.system.get(record.storageId);
      if (
        await storageIdHasLiveAuthoritativeOwner(
          ctx,
          record.storageId,
          startedAt,
          { cleanupRecordId: record._id },
        )
      ) {
        await ctx.db.delete(record._id);
        protectedCount += 1;
        continue;
      }
      const deletionBytes = metadata?.size ?? 0;
      if (
        !canDeleteStorageObject(
          deletionBytes,
          maxBytesDeleted,
          deletedAlready + deleted,
          bytesDeletedAlready + bytesDeleted,
        )
      ) {
        continue;
      }
      if (metadata) {
        await ctx.storage.delete(record.storageId);
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
    if (deleted > 0) {
      // Deleting a page member can invalidate its opaque `_storage` cursor.
      // Restarting cannot skip objects; protected-only pages still paginate.
      await ctx.db.patch(state._id, {
        sweepCursor: undefined,
        updatedAt: Date.now(),
      });
    }
    return { deleted, bytesDeleted, protected: protectedCount };
  },
});

export const runStorageCleanup = internalAction({
  args: {},
  handler: async (ctx): Promise<CleanupActionResult> => {
    const actionStartedAt = Date.now();
    const bounds = getConfiguredBounds();
    const deadlineAt = actionStartedAt + bounds.maxWallMs;
    const backfill: BackfillResult = await ctx.runMutation(
      internal.storage_cleanup.backfillStorageReferences,
      {
        maxJobs: bounds.maxBackfillJobs,
      },
    );
    if (backfill.status !== "complete") {
      return { mode: "dryRun" as const, backfill: backfill.status };
    }
    const mode: CleanupMode =
      process.env.ZENPDF_STORAGE_SWEEP_DELETE_ENABLED === "1"
        ? "delete"
        : "dryRun";
    const retried: FinalizeResult | undefined =
      mode === "delete"
        ? await ctx.runMutation(
            internal.storage_cleanup.finalizeStorageCandidates,
            {
              maxDeleted: bounds.maxDeleted,
              maxBytesDeleted: bounds.maxBytesDeleted,
              maxWallMs: bounds.maxWallMs,
              deletedAlready: 0,
              bytesDeletedAlready: 0,
              deadlineAt,
            },
          )
        : undefined;
    const marked: MarkResult = await ctx.runMutation(
      internal.storage_cleanup.markStorageCandidates,
      {
        mode,
        graceMs: bounds.graceMs,
        maxInspected: bounds.maxInspected,
        maxDeleted: Math.max(0, bounds.maxDeleted - (retried?.deleted ?? 0)),
        maxBytesDeleted: bounds.maxBytesDeleted,
        maxWallMs: bounds.maxWallMs,
        deadlineAt,
      },
    );
    if (mode === "delete" && marked.status === "completed") {
      const finalized: FinalizeResult = await ctx.runMutation(
        internal.storage_cleanup.finalizeStorageCandidates,
        {
          runId: marked.runId,
          maxDeleted: bounds.maxDeleted,
          maxBytesDeleted: bounds.maxBytesDeleted,
          maxWallMs: bounds.maxWallMs,
          deletedAlready: retried?.deleted ?? 0,
          bytesDeletedAlready: retried?.bytesDeleted ?? 0,
          deadlineAt,
        },
      );
      return {
        mode,
        backfill: backfill.status,
        retried,
        ...marked,
        deleted: (retried?.deleted ?? 0) + finalized.deleted,
        bytesDeleted: (retried?.bytesDeleted ?? 0) + finalized.bytesDeleted,
        protected: (retried?.protected ?? 0) + finalized.protected,
      };
    }
    if (mode === "delete") {
      return {
        mode,
        backfill: backfill.status,
        retried,
        ...marked,
        deleted: retried?.deleted ?? 0,
        bytesDeleted: retried?.bytesDeleted ?? 0,
        protected: retried?.protected ?? 0,
      };
    }
    return { mode, backfill: backfill.status, ...marked };
  },
});
