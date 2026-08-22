import { convexTest } from "convex-test";
import { makeFunctionReference } from "convex/server";
import { afterEach, describe, expect, it, vi } from "vitest";

import schema from "../../convex/schema";

const modules = import.meta.glob("../../convex/**/*.ts");

const backfill = makeFunctionReference<
  "mutation",
  { maxJobs: number },
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
  },
  { deleted: number; bytesDeleted: number; protected: number }
>("storage_cleanup:finalizeStorageCandidates");

const runCleanup = makeFunctionReference<
  "action",
  Record<string, never>,
  { mode: "dryRun" | "delete"; backfill: string }
>("storage_cleanup:runStorageCleanup");

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
  it("runs from cron configuration in dry-run mode by default", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "orphan");

    const result = await t.action(runCleanup, {});

    expect(result).toMatchObject({ mode: "dryRun", backfill: "complete" });
    expect(await t.run(async (ctx) => ctx.db.system.get(storageId))).not.toBeNull();
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
    expect(await t.run(async (ctx) => ctx.db.system.get(storageId))).not.toBeNull();
    expect(
      await t.run(async (ctx) =>
        ctx.db
          .query("storageReferences")
          .withIndex("by_storage", (q) => q.eq("storageId", storageId))
          .first(),
      ),
    ).toMatchObject({ storageId, kind: "jobInput" });
  });

  it("serializes binding against candidate marking and rejects post-delete binding", async () => {
    vi.useFakeTimers();
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "input");
    await t.mutation(backfill, { maxJobs: 25 });
    const reservation = await t.mutation(beginBrowserUpload, {
      anonId: "cleanup-race-anon",
      filename: "input.pdf",
      sizeBytes: 5,
      contentType: "application/octet-stream",
    });

    const [markResult, bindResult] = await Promise.allSettled([
      t.mutation(markCandidates, { mode: "delete", ...bounds }),
      t.mutation(bindBrowserUpload, {
        reservationId: reservation.reservationId,
        storageId,
        anonId: "cleanup-race-anon",
      }),
    ]);
    expect(markResult.status).toBe("fulfilled");
    const marked = (markResult as PromiseFulfilledResult<{
      runId: string;
      candidateIds: string[];
    }>).value;
    if (marked.candidateIds.length > 0) {
      expect(bindResult.status).toBe("rejected");
      expect(
        await t.mutation(finalizeCandidates, {
          runId: marked.runId,
          maxDeleted: 5,
          maxBytesDeleted: 1024 * 1024,
          maxWallMs: 1000,
        }),
      ).toMatchObject({ deleted: 1 });
      expect(await t.run(async (ctx) => ctx.db.system.get(storageId))).toBeNull();
      await expect(
        t.mutation(bindBrowserUpload, {
          reservationId: reservation.reservationId,
          storageId,
          anonId: "cleanup-race-anon",
        }),
      ).rejects.toThrow();
    } else {
      expect(bindResult.status).toBe("fulfilled");
      expect(await t.run(async (ctx) => ctx.db.system.get(storageId))).not.toBeNull();
    }
  });

  it("keeps a registered worker output through completion/finalization races", async () => {
    vi.useFakeTimers();
    vi.stubEnv("ZENPDF_WORKER_TOKEN", "cleanup-worker-token");
    const t = convexTest(schema, modules);
    const storageId = await makeOldStorage(t, "output");
    await t.mutation(backfill, { maxJobs: 25 });
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
    expect(await t.run(async (ctx) => ctx.db.system.get(storageId))).not.toBeNull();
    expect(
      await t.run(async (ctx) =>
        ctx.db
          .query("storageReferences")
          .withIndex("by_storage", (q) => q.eq("storageId", storageId))
          .first(),
      ),
    ).toMatchObject({ kind: "jobOutput" });
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
    expect(await t.run(async (ctx) => ctx.db.system.get(referencedId))).not.toBeNull();
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
