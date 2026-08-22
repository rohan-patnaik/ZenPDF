import { convexTest } from "convex-test";
import { makeFunctionReference } from "convex/server";
import { afterEach, describe, expect, it, vi } from "vitest";

import schema from "../../convex/schema";
import { cleanupExpiredArtifactsBatch } from "../../convex/cleanup";
import {
  canDeleteStorageObject,
  MAX_STORAGE_OBJECT_BYTES,
} from "../../convex/storage_cleanup";

const modules = import.meta.glob("../../convex/**/*.ts");

const backfill = makeFunctionReference<
  "mutation",
  { maxJobs: number; phaseBudgetMs?: number },
  { status: "pending" | "complete" | "blocked"; processed: number }
>("storage_cleanup:backfillStorageReferences");

const markCandidates = makeFunctionReference<
  "mutation",
  {
    mode: "dryRun" | "delete";
    graceMs: number;
    maxInspected: number;
    maxDeleted: number;
    maxBytesDeleted: number;
    maxWallMs: number;
    phaseBudgetMs?: number;
  },
  {
    runId: string;
    status: "completed" | "failed";
    candidateIds: string[];
  }
>("storage_cleanup:markStorageCandidates");

const finalizeCandidates = makeFunctionReference<
  "mutation",
  {
    runId?: string;
    maxDeleted: number;
    maxBytesDeleted: number;
    maxWallMs: number;
    phaseBudgetMs?: number;
  },
  { deleted: number; bytesDeleted: number; protected: number }
>("storage_cleanup:finalizeStorageCandidates");

const runCleanup = makeFunctionReference<
  "action",
  Record<string, never>,
  {
    mode: "dryRun" | "delete";
    backfill: string;
    deleted?: number;
    bytesDeleted?: number;
  }
>("storage_cleanup:runStorageCleanup");

const cleanupExpiredArtifacts = makeFunctionReference<
  "mutation",
  { batchSize?: number },
  { deleted: number; pendingDeleted: number }
>("cleanup:cleanupExpiredArtifacts");

const beginBrowserUpload = makeFunctionReference<
  "mutation",
  { anonId?: string; filename: string; sizeBytes: number; contentType: string },
  { reservationId: string; uploadUrl: string; expiresAt: number }
>("files:beginBrowserUpload");

const bindBrowserUpload = makeFunctionReference<
  "mutation",
  { reservationId: string; storageId: string; anonId?: string },
  { reservationId: string; storageId: string }
>("files:bindBrowserUpload");

const registerWorkerUpload = makeFunctionReference<
  "mutation",
  {
    pendingUploadId: string;
    workerId: string;
    storageId: string;
    workerToken?: string;
  },
  boolean
>("files:registerWorkerUpload");

const completeJob = makeFunctionReference<
  "mutation",
  {
    jobId: string;
    workerId: string;
    outputs: Array<{
      storageId: string;
      pendingUploadId: string;
      filename: string;
      sizeBytes?: number;
    }>;
    workerToken?: string;
  },
  { status: string } | null
>("jobs:completeJob");

const bounds = {
  graceMs: 48 * 60 * 60 * 1000,
  maxInspected: 50,
  maxDeleted: 5,
  maxBytesDeleted: 1024 * 1024,
  maxWallMs: 1000,
};

const makeOldStorage = async (
  t: ReturnType<typeof convexTest>,
  contents: string,
) => {
  vi.setSystemTime(new Date("2026-01-01T00:00:00Z"));
  const storageId = await t.run(async (ctx) =>
    ctx.storage.store(new Blob([contents])),
  );
  vi.setSystemTime(new Date("2026-01-04T00:00:00Z"));
  return storageId;
};

afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllEnvs();
});

describe("bounded storage orphan cleanup", () => {
  it("debits failed artifact attempts from the normalized shared batch", async () => {
    const artifacts = [
      { _id: "artifact-1", storageId: "storage-1" },
      { _id: "artifact-2", storageId: "storage-2" },
    ];
    const takeArtifacts = vi.fn().mockResolvedValue(artifacts);
    const query = vi.fn(() => ({
      withIndex: vi.fn(() => ({ take: takeArtifacts })),
    }));
    const getMetadata = vi.fn().mockResolvedValue({ size: 1 });
    const hasOwner = vi.fn().mockRejectedValue(new Error("owner check failed"));
    const ctx = {
      db: {
        query,
        system: { get: getMetadata },
        delete: vi.fn(),
      },
      storage: { delete: vi.fn() },
    };

    await expect(
      cleanupExpiredArtifactsBatch(
        ctx as never,
        { batchSize: 2.9 },
        hasOwner as never,
      ),
    ).resolves.toEqual({ deleted: 0, pendingDeleted: 0 });
    expect(takeArtifacts).toHaveBeenCalledWith(2);
    expect(getMetadata).toHaveBeenCalledTimes(2);
    expect(hasOwner).toHaveBeenCalledTimes(2);
    expect(query).toHaveBeenCalledTimes(1);
  });

  it("does not start a phase when the monotonic action budget is exhausted", async () => {
    vi.useFakeTimers();
    vi.stubEnv("ZENPDF_STORAGE_SWEEP_MAX_WALL_MS", "50");
    vi.spyOn(performance, "now").mockReturnValueOnce(0).mockReturnValue(100);
    const t = convexTest(schema, modules);

    expect(await t.action(runCleanup, {})).toEqual({
      mode: "dryRun",
      backfill: "pending",
    });
    expect(
      await t.run(async (ctx) => ctx.db.query("storageCleanupState").collect()),
    ).toEqual([]);
  });

  it("stops backfill, mark, and finalize work on monotonic phase expiry", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "delayed");

    vi.spyOn(performance, "now").mockReturnValueOnce(0).mockReturnValue(2);
    expect(
      await t.mutation(backfill, { maxJobs: 25, phaseBudgetMs: 1 }),
    ).toEqual({ status: "pending", processed: 0 });
    expect(
      await t.run(async (ctx) => ctx.db.query("storageCleanupState").collect()),
    ).toEqual([]);

    vi.mocked(performance.now).mockRestore();
    await t.mutation(backfill, { maxJobs: 25 });
    vi.spyOn(performance, "now").mockReturnValueOnce(0).mockReturnValue(2);
    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
      phaseBudgetMs: 1,
    });
    expect(marked).toMatchObject({ status: "failed", candidateIds: [] });
    expect(
      await t.run(async (ctx) =>
        ctx.db.query("storageCleanupRecords").collect(),
      ),
    ).toEqual([]);

    vi.mocked(performance.now).mockRestore();
    const ready = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    vi.spyOn(performance, "now").mockReturnValueOnce(0).mockReturnValue(2);
    expect(
      await t.mutation(finalizeCandidates, {
        runId: ready.runId,
        maxDeleted: 1,
        maxBytesDeleted: 1024,
        maxWallMs: 1000,
        phaseBudgetMs: 1,
      }),
    ).toEqual({ deleted: 0, bytesDeleted: 0, protected: 0 });
    expect(
      await t.run(async (ctx) => ctx.db.system.get(storageId)),
    ).not.toBeNull();
  });

  it("serializes legacy expiry cleanup with a third live pending owner", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const sharedId = await t.run(async (ctx) =>
      ctx.storage.store(new Blob(["shared"])),
    );
    const expiredOnlyId = await t.run(async (ctx) =>
      ctx.storage.store(new Blob(["expired-only"])),
    );
    await t.mutation(backfill, { maxJobs: 25 });
    const livePendingId = await t.run(async (ctx) => {
      const now = Date.now();
      const jobId = await ctx.db.insert("jobs", {
        anonId: "legacy-cleanup-owner",
        tier: "ANON",
        tool: "merge",
        status: "running",
        attempts: 1,
        maxAttempts: 3,
        inputs: [],
        createdAt: now,
        updatedAt: now,
      });
      await ctx.db.insert("artifacts", {
        jobId,
        storageId: sharedId,
        kind: "output",
        filename: "shared.pdf",
        createdAt: now - 120_000,
        expiresAt: now - 60_000,
      });
      await ctx.db.insert("artifacts", {
        jobId,
        storageId: expiredOnlyId,
        kind: "output",
        filename: "expired-only.pdf",
        createdAt: now - 120_000,
        expiresAt: now - 60_000,
      });
      for (let index = 0; index < 2; index += 1) {
        await ctx.db.insert("pendingUploads", {
          jobId,
          workerId: `expired-${index}`,
          filename: "shared.pdf",
          sizeBytes: 6,
          storageId: sharedId,
          createdAt: now - 120_000,
          expiresAt: now - 60_000 + index,
        });
      }
      return ctx.db.insert("pendingUploads", {
        jobId,
        workerId: "live-third",
        filename: "shared.pdf",
        sizeBytes: 6,
        storageId: sharedId,
        createdAt: now,
        expiresAt: now + 60_000,
      });
    });

    expect(await t.mutation(cleanupExpiredArtifacts, { batchSize: 2 })).toEqual(
      {
        deleted: 2,
        pendingDeleted: 0,
      },
    );
    expect(
      await t.run(async (ctx) => ctx.db.system.get(sharedId)),
    ).not.toBeNull();
    expect(
      await t.run(async (ctx) => ctx.db.system.get(expiredOnlyId)),
    ).toBeNull();

    await t.run(async (ctx) => {
      await ctx.db.patch(livePendingId, { expiresAt: Date.now() - 1 });
    });
    expect(await t.mutation(cleanupExpiredArtifacts, { batchSize: 2 })).toEqual(
      {
        deleted: 0,
        pendingDeleted: 2,
      },
    );
    expect(await t.run(async (ctx) => ctx.db.system.get(sharedId))).toBeNull();
    expect(await t.mutation(cleanupExpiredArtifacts, { batchSize: 2 })).toEqual(
      {
        deleted: 0,
        pendingDeleted: 1,
      },
    );
    expect(await t.run(async (ctx) => ctx.db.system.get(sharedId))).toBeNull();
    expect(await t.mutation(cleanupExpiredArtifacts, { batchSize: 2 })).toEqual(
      {
        deleted: 0,
        pendingDeleted: 0,
      },
    );
  });

  it("allows one constant-time oversized orphan deletion only as the first deletion", () => {
    expect(
      canDeleteStorageObject(MAX_STORAGE_OBJECT_BYTES, 128 * 1024 * 1024, 0, 0),
    ).toBe(true);
    expect(
      canDeleteStorageObject(
        MAX_STORAGE_OBJECT_BYTES + 1,
        128 * 1024 * 1024,
        0,
        0,
      ),
    ).toBe(true);
    expect(
      canDeleteStorageObject(MAX_STORAGE_OBJECT_BYTES, 128 * 1024 * 1024, 1, 1),
    ).toBe(false);
    expect(canDeleteStorageObject(Number.MAX_SAFE_INTEGER + 1, 128, 0, 0)).toBe(
      false,
    );
  });

  it("recovers an aged oversized direct-upload orphan without deleting owners or a second object", async () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2026-01-01T00:00:00Z"));
    const t = convexTest(schema, modules);
    await t.mutation(backfill, { maxJobs: 25 });
    const ticket = await t.mutation(beginBrowserUpload, {
      anonId: "oversized-orphan-anon",
      filename: "declared-small.pdf",
      sizeBytes: 1,
      contentType: "application/pdf",
    });
    vi.setSystemTime(new Date("2026-01-01T00:00:00.001Z"));
    const { oversizedId, ownedOversizedId, ordinaryId } = await t.run(
      async (ctx) => {
        const oversizedId = await ctx.storage.store(new Blob(["x"]));
        const ownedOversizedId = await ctx.storage.store(new Blob(["y"]));
        const ordinaryId = await ctx.storage.store(new Blob(["ordinary"]));
        await ctx.db.patch(
          oversizedId as never,
          {
            size: MAX_STORAGE_OBJECT_BYTES + 1,
            contentType: "application/pdf",
          } as never,
        );
        await ctx.db.patch(
          ownedOversizedId as never,
          {
            size: MAX_STORAGE_OBJECT_BYTES + 1,
          } as never,
        );
        await ctx.db.insert("artifacts", {
          storageId: ownedOversizedId,
          kind: "output",
          filename: "owned.bin",
          createdAt: Date.now(),
          expiresAt: Date.now() + 7 * 24 * 60 * 60 * 1000,
        });
        return { oversizedId, ownedOversizedId, ordinaryId };
      },
    );
    await expect(
      t.mutation(bindBrowserUpload, {
        reservationId: ticket.reservationId,
        storageId: oversizedId,
        anonId: "oversized-orphan-anon",
      }),
    ).rejects.toThrow();

    vi.setSystemTime(new Date("2026-01-05T00:00:00Z"));
    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    expect(marked.candidateIds).toHaveLength(2);
    expect(
      await t.mutation(finalizeCandidates, {
        runId: marked.runId,
        maxDeleted: 5,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toMatchObject({
      deleted: 1,
      bytesDeleted: MAX_STORAGE_OBJECT_BYTES + 1,
      protected: 0,
    });
    expect(
      await t.run(async (ctx) => ctx.db.system.get(oversizedId)),
    ).toBeNull();
    expect(
      await t.run(async (ctx) => ctx.db.system.get(ownedOversizedId)),
    ).not.toBeNull();
    expect(
      await t.run(async (ctx) => ctx.db.system.get(ordinaryId)),
    ).not.toBeNull();
  });

  it("accepts a 2 GiB browser ticket and rejects one byte more", async () => {
    vi.stubEnv("ZENPDF_ANON_MAX_MB_PER_FILE", "2048");
    const t = convexTest(schema, modules);

    await expect(
      t.mutation(beginBrowserUpload, {
        anonId: "absolute-limit-anon",
        filename: "maximum.pdf",
        sizeBytes: MAX_STORAGE_OBJECT_BYTES,
        contentType: "application/pdf",
      }),
    ).resolves.toMatchObject({ reservationId: expect.any(String) });
    await expect(
      t.mutation(beginBrowserUpload, {
        anonId: "absolute-limit-anon",
        filename: "too-large.pdf",
        sizeBytes: MAX_STORAGE_OBJECT_BYTES + 1,
        contentType: "application/pdf",
      }),
    ).rejects.toThrow();

    vi.stubEnv("ZENPDF_ANON_MAX_MB_PER_FILE", "2049");
    await expect(
      t.mutation(beginBrowserUpload, {
        anonId: "absolute-limit-anon",
        filename: "misconfigured.pdf",
        sizeBytes: 1,
        contentType: "application/pdf",
      }),
    ).rejects.toThrow("Plan file-size limit exceeds the 2 GiB storage ceiling");
  });

  it("runs from cron configuration in dry-run mode by default", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "orphan");

    const result = await t.action(runCleanup, {});

    expect(result).toMatchObject({ mode: "dryRun", backfill: "complete" });
    expect(
      await t.run(async (ctx) => ctx.db.system.get(storageId)),
    ).not.toBeNull();
    const records = await t.run(async (ctx) =>
      ctx.db.query("storageCleanupRecords").collect(),
    );
    expect(records).toEqual([]);
  });

  it("backfills legacy job references before permitting deletion", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "referenced");
    await t.run(async (ctx) => {
      await ctx.db.insert("jobs", {
        anonId: "legacy-reference",
        tier: "ANON",
        tool: "merge",
        status: "succeeded",
        attempts: 1,
        maxAttempts: 3,
        inputs: [{ storageId, filename: "input.pdf", sizeBytes: 10 }],
        createdAt: Date.now() - bounds.graceMs,
        updatedAt: Date.now(),
      });
    });

    expect(await t.mutation(backfill, { maxJobs: 25 })).toMatchObject({
      status: "complete",
      processed: 1,
    });
    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });

    expect(marked.candidateIds).toEqual([]);
    expect(
      await t.run(async (ctx) => ctx.db.system.get(storageId)),
    ).not.toBeNull();
    expect(
      await t.run(async (ctx) =>
        ctx.db
          .query("storageReferences")
          .withIndex("by_storage", (q) => q.eq("storageId", storageId))
          .first(),
      ),
    ).toMatchObject({ storageId, kind: "jobInput" });
  });

  it("retains storage when a live pending row follows multiple expired rows", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "pending");
    await t.mutation(backfill, { maxJobs: 25 });
    const livePendingId = await t.run(async (ctx) => {
      const now = Date.now();
      const jobId = await ctx.db.insert("jobs", {
        anonId: "pending-reference-owner",
        tier: "ANON",
        tool: "merge",
        status: "running",
        attempts: 1,
        maxAttempts: 3,
        inputs: [],
        createdAt: now,
        updatedAt: now,
      });
      for (let index = 0; index < 2; index += 1) {
        await ctx.db.insert("pendingUploads", {
          jobId,
          workerId: `expired-${index}`,
          filename: "expired.pdf",
          sizeBytes: 7,
          storageId,
          createdAt: now - 120_000,
          expiresAt: now - 60_000 + index,
        });
      }
      return ctx.db.insert("pendingUploads", {
        jobId,
        workerId: "live-third",
        filename: "live.pdf",
        sizeBytes: 7,
        storageId,
        createdAt: now,
        expiresAt: now + 60_000,
      });
    });

    expect(
      (
        await t.mutation(markCandidates, {
          mode: "delete",
          ...bounds,
        })
      ).candidateIds,
    ).toEqual([]);
    await t.run(async (ctx) => {
      await ctx.db.patch(livePendingId, { expiresAt: Date.now() - 1 });
    });
    expect(
      (
        await t.mutation(markCandidates, {
          mode: "delete",
          ...bounds,
        })
      ).candidateIds,
    ).toHaveLength(1);
  });

  it("proves both serialized bind/delete orderings and rejects post-delete binding", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const boundFirstId = await makeOldStorage(t, "bound");
    const deleteFirstId = await makeOldStorage(t, "delete");
    await t.mutation(backfill, { maxJobs: 25 });
    await t.run(async (ctx) => {
      const now = Date.now();
      await ctx.db.insert("browserUploadReservations", {
        anonId: "cleanup-race-anon",
        status: "bound",
        filename: "bound.pdf",
        sizeBytes: 5,
        contentType: "application/octet-stream",
        storageId: boundFirstId,
        createdAt: now,
        expiresAt: now + 60_000,
        boundAt: now,
      });
    });
    const deleteFirstReservation = await t.mutation(beginBrowserUpload, {
      anonId: "cleanup-race-anon",
      filename: "delete.pdf",
      sizeBytes: 6,
      contentType: "application/octet-stream",
    });
    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    expect(marked.candidateIds).toHaveLength(1);
    expect(
      await t.run(async (ctx) => ctx.db.system.get(boundFirstId)),
    ).not.toBeNull();
    await expect(
      t.mutation(bindBrowserUpload, {
        reservationId: deleteFirstReservation.reservationId,
        storageId: deleteFirstId,
        anonId: "cleanup-race-anon",
      }),
    ).rejects.toThrow();
    expect(
      await t.mutation(finalizeCandidates, {
        runId: marked.runId,
        maxDeleted: 5,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toMatchObject({ deleted: 1 });
    expect(
      await t.run(async (ctx) => ctx.db.system.get(deleteFirstId)),
    ).toBeNull();
    await expect(
      t.mutation(bindBrowserUpload, {
        reservationId: deleteFirstReservation.reservationId,
        storageId: deleteFirstId,
        anonId: "cleanup-race-anon",
      }),
    ).rejects.toThrow();
  });

  it("serializes worker registration/completion against deletion", async () => {
    vi.useFakeTimers();
    vi.stubEnv("ZENPDF_WORKER_TOKEN", "cleanup-worker-token");
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "output");
    const deleteFirstStorageId = await makeOldStorage(t, "doomed");
    await t.mutation(backfill, { maxJobs: 25 });
    const temporaryProtection = await t.run(async (ctx) => {
      const now = Date.now();
      return ctx.db.insert("browserUploadReservations", {
        anonId: "worker-delete-protection",
        status: "bound",
        filename: "doomed.pdf",
        sizeBytes: 6,
        contentType: "application/octet-stream",
        storageId: deleteFirstStorageId,
        createdAt: now,
        expiresAt: now + 60_000,
        boundAt: now,
      });
    });
    const { jobId, pendingUploadId } = await t.run(async (ctx) => {
      const now = Date.now();
      const jobId = await ctx.db.insert("jobs", {
        anonId: "worker-cleanup-race",
        tier: "ANON",
        tool: "merge",
        status: "running",
        claimedBy: "worker-cleanup",
        claimExpiresAt: now + 60_000,
        attempts: 1,
        maxAttempts: 3,
        inputs: [],
        createdAt: now,
        updatedAt: now,
      });
      const pendingUploadId = await ctx.db.insert("pendingUploads", {
        jobId,
        workerId: "worker-cleanup",
        filename: "output.pdf",
        sizeBytes: 6,
        createdAt: now,
        expiresAt: now + 60_000,
      });
      return { jobId, pendingUploadId };
    });
    expect(
      await t.mutation(registerWorkerUpload, {
        pendingUploadId,
        workerId: "worker-cleanup",
        storageId,
        workerToken: "cleanup-worker-token",
      }),
    ).toBe(true);

    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    expect(marked.candidateIds).toEqual([]);
    const completed = await t.mutation(completeJob, {
      jobId,
      workerId: "worker-cleanup",
      outputs: [
        {
          storageId,
          pendingUploadId,
          filename: "output.pdf",
          sizeBytes: 6,
        },
      ],
      workerToken: "cleanup-worker-token",
    });
    expect(completed?.status).toBe("succeeded");
    expect(
      await t.run(async (ctx) => ctx.db.system.get(storageId)),
    ).not.toBeNull();
    expect(
      await t.run(async (ctx) =>
        ctx.db
          .query("storageReferences")
          .withIndex("by_storage", (q) => q.eq("storageId", storageId))
          .first(),
      ),
    ).toMatchObject({ kind: "jobOutput" });

    await t.run(async (ctx) => {
      await ctx.db.patch(temporaryProtection, {
        status: "expired",
        expiresAt: Date.now() - 1,
      });
    });

    const deleteFirst = await t.run(async (ctx) => {
      const now = Date.now();
      const deleteFirstJobId = await ctx.db.insert("jobs", {
        anonId: "worker-delete-first",
        tier: "ANON",
        tool: "merge",
        status: "running",
        claimedBy: "worker-delete-first",
        claimExpiresAt: now + 60_000,
        attempts: 1,
        maxAttempts: 3,
        inputs: [],
        createdAt: now,
        updatedAt: now,
      });
      const deleteFirstPendingId = await ctx.db.insert("pendingUploads", {
        jobId: deleteFirstJobId,
        workerId: "worker-delete-first",
        filename: "doomed.pdf",
        sizeBytes: 6,
        createdAt: now,
        expiresAt: now + 60_000,
      });
      return { deleteFirstPendingId };
    });
    const deleteFirstRun = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    expect(deleteFirstRun.candidateIds).toHaveLength(1);
    expect(
      await t.mutation(registerWorkerUpload, {
        pendingUploadId: deleteFirst.deleteFirstPendingId,
        workerId: "worker-delete-first",
        storageId: deleteFirstStorageId,
        workerToken: "cleanup-worker-token",
      }),
    ).toBe(false);
    expect(
      await t.mutation(finalizeCandidates, {
        runId: deleteFirstRun.runId,
        maxDeleted: 5,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toMatchObject({ deleted: 1 });
    expect(
      await t.run(async (ctx) => ctx.db.system.get(deleteFirstStorageId)),
    ).toBeNull();
  });

  it("deletes only the exact unreferenced candidate and retains its tombstone", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const orphanId = await makeOldStorage(t, "orphan");
    const referencedId = await makeOldStorage(t, "safe");
    await t.run(async (ctx) => {
      const jobId = await ctx.db.insert("jobs", {
        anonId: "false-reference-negative",
        tier: "ANON",
        tool: "merge",
        status: "succeeded",
        attempts: 1,
        maxAttempts: 3,
        inputs: [{ storageId: referencedId, filename: "safe.pdf" }],
        createdAt: Date.now(),
        updatedAt: Date.now(),
      });
      await ctx.db.insert("storageReferences", {
        storageId: referencedId,
        jobId,
        kind: "jobInput",
        createdAt: Date.now(),
      });
    });
    await t.mutation(backfill, { maxJobs: 25 });
    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    expect(marked.candidateIds).toHaveLength(1);
    expect(
      await t.mutation(finalizeCandidates, {
        runId: marked.runId,
        maxDeleted: 5,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toMatchObject({ deleted: 1, bytesDeleted: 6 });
    expect(await t.run(async (ctx) => ctx.db.system.get(orphanId))).toBeNull();
    expect(
      await t.run(async (ctx) => ctx.db.system.get(referencedId)),
    ).not.toBeNull();
    expect(
      await t.run(async (ctx) =>
        ctx.db
          .query("storageCleanupRecords")
          .withIndex("by_storage", (q) => q.eq("storageId", orphanId))
          .first(),
      ),
    ).toMatchObject({
      state: "deleted",
      checkedJobReferences: true,
      checkedArtifacts: true,
      checkedPendingUploads: true,
      checkedReservations: true,
    });
  });

  it("advances one bounded page at a time and retries an unfinished candidate", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const firstId = await makeOldStorage(t, "first");
    const secondId = await makeOldStorage(t, "second");
    await t.mutation(backfill, { maxJobs: 25 });

    const firstRun = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
      maxInspected: 1,
    });
    expect(firstRun.candidateIds).toHaveLength(1);
    expect(
      await t.mutation(finalizeCandidates, {
        runId: firstRun.runId,
        maxDeleted: 1,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toMatchObject({ deleted: 1 });

    const secondRun = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
      maxInspected: 1,
    });
    expect(secondRun.candidateIds).toHaveLength(1);
    expect(
      await t.mutation(finalizeCandidates, {
        maxDeleted: 1,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toMatchObject({ deleted: 1 });
    expect(await t.run(async (ctx) => ctx.db.system.get(firstId))).toBeNull();
    expect(await t.run(async (ctx) => ctx.db.system.get(secondId))).toBeNull();
  });

  it("shares delete-count and byte budgets across retried and new work", async () => {
    vi.useFakeTimers();
    vi.stubEnv("ZENPDF_STORAGE_SWEEP_DELETE_ENABLED", "1");
    vi.stubEnv("ZENPDF_STORAGE_SWEEP_MAX_DELETED", "1");
    vi.stubEnv("ZENPDF_STORAGE_SWEEP_MAX_BYTES", "5");
    const t = convexTest(schema, modules);
    const firstId = await makeOldStorage(t, "first");
    const secondId = await makeOldStorage(t, "later");
    await t.mutation(backfill, { maxJobs: 25 });
    const oldRun = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
      maxInspected: 1,
      maxDeleted: 1,
      maxBytesDeleted: 5,
    });
    expect(oldRun.candidateIds).toHaveLength(1);

    const result = await t.action(runCleanup, {});

    expect(result).toMatchObject({
      mode: "delete",
      deleted: 1,
      bytesDeleted: 5,
    });
    expect(await t.run(async (ctx) => ctx.db.system.get(firstId))).toBeNull();
    expect(
      await t.run(async (ctx) => ctx.db.system.get(secondId)),
    ).not.toBeNull();
  });

  it("treats a candidate already missing from storage as a successful deletion", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "already-gone");
    await t.mutation(backfill, { maxJobs: 25 });
    const marked = await t.mutation(markCandidates, {
      mode: "delete",
      ...bounds,
    });
    await t.run(async (ctx) => ctx.storage.delete(storageId));

    expect(
      await t.mutation(finalizeCandidates, {
        runId: marked.runId,
        maxDeleted: 1,
        maxBytesDeleted: 1024 * 1024,
        maxWallMs: 1000,
      }),
    ).toEqual({ deleted: 1, bytesDeleted: 0, protected: 0 });
    expect(
      await t.run(async (ctx) =>
        ctx.db
          .query("storageCleanupRecords")
          .withIndex("by_storage", (q) => q.eq("storageId", storageId))
          .first(),
      ),
    ).toMatchObject({ state: "deleted" });
  });
});
