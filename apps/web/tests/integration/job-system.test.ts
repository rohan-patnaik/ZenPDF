import { convexTest } from "convex-test";
import { makeFunctionReference } from "convex/server";
import { describe, expect, it, vi } from "vitest";

import schema from "../../convex/schema";
import type { Id } from "../../convex/_generated/dataModel";
import { monthKey, startOfDayUtc } from "../../convex/lib/time";

const modules = import.meta.glob("../../convex/**/*.ts");

const storeBrowserFile = async (
  t: Pick<ReturnType<typeof convexTest>, "run">,
  contents: string,
  contentType = "application/octet-stream",
) =>
  t.run(async (ctx) => {
    const storageId = await ctx.storage.store(
      new Blob([contents], { type: contentType }),
    );
    // convex-test omits Blob.type from its fake `_storage` metadata.
    await ctx.db.patch(storageId as never, { contentType });
    return storageId;
  });

const backfillStorageReferences = makeFunctionReference<
  "mutation",
  { maxJobs: number },
  { status: "pending" | "complete" | "blocked"; processed: number }
>("storage_cleanup:backfillStorageReferences");

const createJob = makeFunctionReference<
  "mutation",
  {
    tool: string;
    inputs: Array<{
      reservationId: string;
      storageId: string;
      filename: string;
      sizeBytes: number;
    }>;
    config?: unknown;
    anonId?: string;
  },
  { jobId: string; anonId?: string }
>("jobs:createJob");

type ClaimedJob = {
  _id: string;
  status: string;
  attempts: number;
} | null;

const claimNextJob = makeFunctionReference<
  "mutation",
  { workerId: string; workerToken?: string },
  ClaimedJob
>("jobs:claimNextJob");

const beginWorkerUpload = makeFunctionReference<
  "mutation",
  {
    jobId: string;
    workerId: string;
    filename: string;
    sizeBytes: number;
    workerToken?: string;
  },
  { pendingUploadId: string; uploadUrl: string; uploadDeadlineAt: number } | null
>("files:beginWorkerUpload");

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

const discardWorkerUpload = makeFunctionReference<
  "mutation",
  {
    pendingUploadId: string;
    workerId: string;
    storageId?: string;
    workerToken?: string;
  },
  boolean
>("files:discardWorkerUpload");

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

const getWorkerUploadState = makeFunctionReference<
  "query",
  {
    jobId: string;
    pendingUploadId: string;
    workerId: string;
    storageId: string;
    workerToken?: string;
  },
  "registered" | "unregistered" | "committed" | "orphaned" | "deleted" | "mismatch"
>("files:getWorkerUploadState");

const failJob = makeFunctionReference<
  "mutation",
  {
    jobId: string;
    workerId: string;
    errorCode: string;
    errorMessage?: string;
    workerToken?: string;
  },
  { _id: string; status: string; claimedBy?: string } | null
>("jobs:failJob");

type CapacitySnapshot = {
  budget: { monthlyBudgetUsage: number; status: string };
};

const getCapacitySnapshot = makeFunctionReference<
  "query",
  Record<string, never>,
  CapacitySnapshot
>("capacity:getCapacitySnapshot");

describe("job system", () => {
  it("issues owned one-shot browser reservations instead of generic URLs", async () => {
    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const anonymous = convexTest(schema, modules);
      await anonymous.mutation(backfillStorageReferences, { maxJobs: 25 });
      await expect(
        anonymous.mutation(
          beginBrowserUpload,
          {
            workerToken: "test-worker-token",
            filename: "input.pdf",
            sizeBytes: 4,
            contentType: "application/pdf",
          } as never,
        ),
      ).rejects.toThrow();
      await expect(
        anonymous.mutation(beginBrowserUpload, {
          filename: "input.pdf",
          sizeBytes: 4,
          contentType: "application/pdf",
        }),
      ).rejects.toThrow();

      const signedIn = anonymous.withIdentity({
        subject: "user_upload",
        email: "upload@example.com",
      });
      const now = Date.now();
      await signedIn.run(async (ctx) => {
        await ctx.db.insert("users", {
          clerkUserId: "user_upload",
          email: "upload@example.com",
          tier: "FREE_ACCOUNT",
          createdAt: now,
          updatedAt: now,
        });
      });
      const issued = await signedIn.mutation(beginBrowserUpload, {
        filename: "input.pdf",
        sizeBytes: 4,
        contentType: "application/pdf",
      });
      expect(issued.uploadUrl).toMatch(/^https?:\/\//);
      expect(issued.expiresAt).toBeGreaterThan(now);
      const storageId = await storeBrowserFile(
        signedIn,
        "test",
        "application/pdf",
      );
      const otherUser = anonymous.withIdentity({
        subject: "other_upload",
      });
      await expect(
        otherUser.mutation(bindBrowserUpload, {
          reservationId: issued.reservationId,
          storageId,
        }),
      ).rejects.toThrow();
      await expect(
        signedIn.mutation(bindBrowserUpload, {
          reservationId: issued.reservationId,
          storageId,
        }),
      ).resolves.toEqual({ reservationId: issued.reservationId, storageId });
      await expect(
        signedIn.mutation(bindBrowserUpload, {
          reservationId: issued.reservationId,
          storageId,
        }),
      ).rejects.toThrow();

      const anonIssued = await anonymous.mutation(beginBrowserUpload, {
        anonId: "anon-browser",
        filename: "anon.pdf",
        sizeBytes: 4,
        contentType: "application/pdf",
      });
      expect(anonIssued.uploadUrl).toMatch(/^https?:\/\//);
    } finally {
      if (previousWorkerToken === undefined) {
        delete process.env.ZENPDF_WORKER_TOKEN;
      } else {
        process.env.ZENPDF_WORKER_TOKEN = previousWorkerToken;
      }
    }
  });

  it("enforces browser intent, server expiry, and outstanding-ticket bounds", async () => {
    const t = convexTest(schema, modules);
    await t.mutation(backfillStorageReferences, { maxJobs: 25 });
    const anonId = "anon-reservation-owner";
    await expect(
      t.mutation(beginBrowserUpload, {
        anonId,
        filename: "../private.pdf",
        sizeBytes: 4,
        contentType: "application/pdf",
      }),
    ).rejects.toThrow();

    const expired = await t.mutation(beginBrowserUpload, {
      anonId,
      filename: "expired.pdf",
      sizeBytes: 4,
      contentType: "application/pdf",
    });
    const exactStorageId = await storeBrowserFile(
      t,
      "test",
      "application/pdf",
    );
    await t.run(async (ctx) => {
      await ctx.db.patch(
        expired.reservationId as Id<"browserUploadReservations">,
        { expiresAt: Date.now() - 1 },
      );
    });
    await expect(
      t.mutation(bindBrowserUpload, {
        reservationId: expired.reservationId,
        storageId: exactStorageId,
        anonId,
      }),
    ).rejects.toThrow();
    const expiredRecord = await t.run(async (ctx) =>
      ctx.db.get(expired.reservationId as Id<"browserUploadReservations">),
    );
    expect(expiredRecord?.status).toBe("issued");
    expect(expiredRecord).not.toHaveProperty("storageId");

    const mismatched = await t.mutation(beginBrowserUpload, {
      anonId,
      filename: "mismatch.pdf",
      sizeBytes: 5,
      contentType: "application/pdf",
    });
    await expect(
      t.mutation(bindBrowserUpload, {
        reservationId: mismatched.reservationId,
        storageId: exactStorageId,
        anonId,
      }),
    ).rejects.toThrow();

    for (let index = 0; index < 11; index += 1) {
      await t.mutation(beginBrowserUpload, {
        anonId,
        filename: `outstanding-${index}.pdf`,
        sizeBytes: 4,
        contentType: "application/pdf",
      });
    }
    await expect(
      t.mutation(beginBrowserUpload, {
        anonId,
        filename: "over-limit.pdf",
        sizeBytes: 4,
        contentType: "application/pdf",
      }),
    ).rejects.toThrow();
  });

  it("atomically consumes owned live reservations and rejects bare IDs or replay", async () => {
    const root = convexTest(schema, modules);
    await root.mutation(backfillStorageReferences, { maxJobs: 25 });
    const owner = root.withIdentity({ subject: "reservation_job_owner" });
    const other = root.withIdentity({ subject: "reservation_job_other" });
    const reservation = await owner.mutation(beginBrowserUpload, {
      filename: "job-input.pdf",
      sizeBytes: 5,
      contentType: "application/octet-stream",
    });
    const storageId = await storeBrowserFile(root, "input");
    await owner.mutation(bindBrowserUpload, {
      reservationId: reservation.reservationId,
      storageId,
    });
    const input = {
      reservationId: reservation.reservationId,
      storageId,
      filename: "job-input.pdf",
      sizeBytes: 5,
    };

    await expect(
      owner.mutation(createJob, {
        tool: "merge",
        inputs: [{ storageId, filename: "job-input.pdf", sizeBytes: 5 }] as never,
      }),
    ).rejects.toThrow();
    await expect(
      other.mutation(createJob, { tool: "merge", inputs: [input] }),
    ).rejects.toThrow();

    const created = await owner.mutation(createJob, {
      tool: "merge",
      inputs: [input],
    });
    const consumed = await root.run(async (ctx) =>
      ctx.db.get(reservation.reservationId as Id<"browserUploadReservations">),
    );
    expect(consumed).toMatchObject({
      status: "consumed",
      jobId: created.jobId,
      storageId,
    });
    await expect(
      owner.mutation(createJob, { tool: "merge", inputs: [input] }),
    ).rejects.toThrow();

    const expiring = await owner.mutation(beginBrowserUpload, {
      filename: "expired-job.pdf",
      sizeBytes: 4,
      contentType: "application/octet-stream",
    });
    const expiringStorageId = await storeBrowserFile(root, "late");
    await owner.mutation(bindBrowserUpload, {
      reservationId: expiring.reservationId,
      storageId: expiringStorageId,
    });
    await root.run(async (ctx) => {
      await ctx.db.patch(
        expiring.reservationId as Id<"browserUploadReservations">,
        { expiresAt: Date.now() - 1 },
      );
    });
    await expect(
      owner.mutation(createJob, {
        tool: "merge",
        inputs: [
          {
            reservationId: expiring.reservationId,
            storageId: expiringStorageId,
            filename: "expired-job.pdf",
            sizeBytes: 4,
          },
        ],
      }),
    ).rejects.toThrow();
  });

  it("rejects binding storage already owned by jobs, artifacts, or pending uploads", async () => {
    const root = convexTest(schema, modules);
    await root.mutation(backfillStorageReferences, { maxJobs: 25 });
    const anonId = "attacker-session";
    const preexistingStorageId = await storeBrowserFile(
      root,
      "victim",
      "application/pdf",
    );
    const preexistingIntent = await root.mutation(beginBrowserUpload, {
      anonId,
      filename: "preexisting.pdf",
      sizeBytes: 6,
      contentType: "application/pdf",
    });
    const intents = await Promise.all(
      ["job-ref.pdf", "artifact.pdf", "pending.pdf"].map((filename) =>
        root.mutation(beginBrowserUpload, {
          anonId,
          filename,
          sizeBytes: 6,
          contentType: "application/pdf",
        }),
      ),
    );
    const storageIds = await Promise.all(
      intents.map(() => storeBrowserFile(root, "victim", "application/pdf")),
    );
    await root.run(async (ctx) => {
      const now = Date.now();
      const victimJobId = await ctx.db.insert("jobs", {
        anonId: "victim-session",
        tier: "ANON",
        tool: "merge",
        status: "running",
        claimedBy: "victim-worker",
        claimExpiresAt: now + 60_000,
        attempts: 1,
        maxAttempts: 3,
        inputs: [{ storageId: storageIds[0], filename: "victim.pdf" }],
        createdAt: now,
        updatedAt: now,
      });
      await ctx.db.insert("storageReferences", {
        storageId: storageIds[0],
        jobId: victimJobId,
        kind: "jobInput",
        createdAt: now,
      });
      await ctx.db.insert("artifacts", {
        jobId: victimJobId,
        storageId: storageIds[1],
        kind: "output",
        filename: "victim-output.pdf",
        createdAt: now,
        expiresAt: now + 60_000,
      });
      await ctx.db.insert("pendingUploads", {
        jobId: victimJobId,
        workerId: "victim-worker",
        filename: "victim-pending.pdf",
        sizeBytes: 6,
        storageId: storageIds[2],
        createdAt: now,
        expiresAt: now + 60_000,
      });
    });

    for (let index = 0; index < storageIds.length; index += 1) {
      await expect(
        root.mutation(bindBrowserUpload, {
          reservationId: intents[index].reservationId,
          storageId: storageIds[index],
          anonId,
        }),
      ).rejects.toThrow();
      await root.run(async (ctx) => {
        await ctx.db.patch(
          intents[index].reservationId as Id<"browserUploadReservations">,
          {
            status: "bound",
            storageId: storageIds[index],
            boundAt: Date.now(),
          },
        );
      });
      await expect(
        root.mutation(createJob, {
          tool: "merge",
          anonId,
          inputs: [
            {
              reservationId: intents[index].reservationId,
              storageId: storageIds[index],
              filename: ["job-ref.pdf", "artifact.pdf", "pending.pdf"][index],
              sizeBytes: 6,
            },
          ],
        }),
      ).rejects.toThrow();
      await root.run(async (ctx) => {
        await ctx.db.patch(
          intents[index].reservationId as Id<"browserUploadReservations">,
          { status: "issued", storageId: undefined, boundAt: undefined },
        );
      });
    }
    await expect(
      root.mutation(bindBrowserUpload, {
        reservationId: preexistingIntent.reservationId,
        storageId: preexistingStorageId,
        anonId,
      }),
    ).rejects.toThrow();
  });

  it("requires exact defined content type at bind and consume with atomic rejection", async () => {
    const root = convexTest(schema, modules);
    await root.mutation(backfillStorageReferences, { maxJobs: 25 });
    const anonId = "content-type-owner";
    const undefinedType = await root.mutation(beginBrowserUpload, {
      anonId,
      filename: "undefined.pdf",
      sizeBytes: 4,
      contentType: "application/pdf",
    });
    const mismatchedType = await root.mutation(beginBrowserUpload, {
      anonId,
      filename: "mismatch.pdf",
      sizeBytes: 4,
      contentType: "application/pdf",
    });
    const undefinedStorageId = await root.run(async (ctx) =>
      ctx.storage.store(new Blob(["none"])),
    );
    const mismatchedStorageId = await storeBrowserFile(
      root,
      "nope",
      "image/png",
    );

    await expect(
      root.mutation(bindBrowserUpload, {
        reservationId: undefinedType.reservationId,
        storageId: undefinedStorageId,
        anonId,
      }),
    ).rejects.toThrow();
    await expect(
      root.mutation(bindBrowserUpload, {
        reservationId: mismatchedType.reservationId,
        storageId: mismatchedStorageId,
        anonId,
      }),
    ).rejects.toThrow();

    await root.run(async (ctx) => {
      await ctx.db.patch(
        undefinedType.reservationId as Id<"browserUploadReservations">,
        { status: "bound", storageId: undefinedStorageId, boundAt: Date.now() },
      );
      await ctx.db.patch(
        mismatchedType.reservationId as Id<"browserUploadReservations">,
        { status: "bound", storageId: mismatchedStorageId, boundAt: Date.now() },
      );
    });
    await expect(
      root.mutation(createJob, {
        tool: "merge",
        anonId,
        inputs: [
          {
            reservationId: undefinedType.reservationId,
            storageId: undefinedStorageId,
            filename: "undefined.pdf",
            sizeBytes: 4,
          },
          {
            reservationId: mismatchedType.reservationId,
            storageId: mismatchedStorageId,
            filename: "mismatch.pdf",
            sizeBytes: 4,
          },
        ],
      }),
    ).rejects.toThrow();
    const state = await root.run(async (ctx) => ({
      jobs: await ctx.db.query("jobs").collect(),
      first: await ctx.db.get(
        undefinedType.reservationId as Id<"browserUploadReservations">,
      ),
      second: await ctx.db.get(
        mismatchedType.reservationId as Id<"browserUploadReservations">,
      ),
    }));
    expect(state.jobs).toEqual([]);
    expect(state.first?.status).toBe("bound");
    expect(state.second?.status).toBe("bound");
  });

  it("rolls back reservation consumption, job insertion, and usage together", async () => {
    vi.stubEnv("ZENPDF_DEV_MODE", "1");
    vi.stubEnv("NODE_ENV", "development");
    try {
      const root = convexTest(schema, modules);
      await root.mutation(backfillStorageReferences, { maxJobs: 25 });
      const owner = root.withIdentity({ subject: "reservation_rollback_owner" });
      const reservation = await owner.mutation(beginBrowserUpload, {
        filename: "rollback.pdf",
        sizeBytes: 4,
        contentType: "application/octet-stream",
      });
      const storageId = await storeBrowserFile(root, "test");
      await owner.mutation(bindBrowserUpload, {
        reservationId: reservation.reservationId,
        storageId,
      });
      await root.run(async (ctx) => {
        const periodStart = startOfDayUtc(Date.now());
        for (let index = 0; index < 2; index += 1) {
          await ctx.db.insert("globalUsageCounters", {
            periodStart,
            jobsUsed: 0,
            minutesUsed: 0,
            bytesProcessed: 0,
          });
        }
      });

      await expect(
        owner.mutation(createJob, {
          tool: "merge",
          inputs: [
            {
              reservationId: reservation.reservationId,
              storageId,
              filename: "rollback.pdf",
              sizeBytes: 4,
            },
          ],
        }),
      ).rejects.toThrow();

      const state = await root.run(async (ctx) => ({
        reservation: await ctx.db.get(
          reservation.reservationId as Id<"browserUploadReservations">,
        ),
        jobs: await ctx.db.query("jobs").collect(),
        usage: await ctx.db.query("usageCounters").collect(),
      }));
      expect(state.reservation?.status).toBe("bound");
      expect(state.reservation).not.toHaveProperty("jobId");
      expect(state.jobs).toEqual([]);
      expect(state.usage).toEqual([]);
    } finally {
      vi.unstubAllEnvs();
    }
  });

  it("rejects jobs when monthly budget is exceeded", async () => {
    const t = convexTest(schema, modules);
    await t.mutation(backfillStorageReferences, { maxJobs: 25 });
    const now = Date.now();
    const month = monthKey(now);

    await t.run(async (ctx) => {
      await ctx.db.insert("budgetState", {
        month,
        monthlyBudgetUsage: 1.05,
        heavyToolsEnabled: false,
        status: "at_capacity",
        updatedAt: now,
      });
    });

    const reservation = await t.mutation(beginBrowserUpload, {
      anonId: "anon-test",
      filename: "sample.pdf",
      sizeBytes: 4,
      contentType: "application/octet-stream",
    });
    const storageId = await storeBrowserFile(t, "test");
    await t.mutation(bindBrowserUpload, {
      reservationId: reservation.reservationId,
      storageId,
      anonId: "anon-test",
    });

    const snapshot = await t.query(getCapacitySnapshot, {});
    expect(snapshot.budget.monthlyBudgetUsage).toBeGreaterThanOrEqual(1);

    let errorCode = "";
    try {
      await t.mutation(createJob, {
        tool: "merge",
        inputs: [
          {
            reservationId: reservation.reservationId,
            storageId,
            filename: "sample.pdf",
            sizeBytes: 4,
          },
        ],
        anonId: "anon-test",
      });
    } catch (error: unknown) {
      const formatted = error as { data?: { code?: string } };
      if (formatted?.data?.code) {
        errorCode = formatted.data.code;
      } else if (error instanceof Error) {
        errorCode = error.message.includes("SERVICE_CAPACITY_MONTHLY_BUDGET")
          ? "SERVICE_CAPACITY_MONTHLY_BUDGET"
          : "";
      }
    }

    expect(errorCode).toBe("SERVICE_CAPACITY_MONTHLY_BUDGET");
  });

  it("claims queued jobs and moves them to running", async () => {
    const t = convexTest(schema, modules).withIdentity({
      subject: "user_123",
      email: "user@example.com",
    });
    await t.mutation(backfillStorageReferences, { maxJobs: 25 });

    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const reservation = await t.mutation(beginBrowserUpload, {
        filename: "sample.pdf",
        sizeBytes: 4,
        contentType: "application/octet-stream",
      });
      const storageId = await storeBrowserFile(t, "test");
      await t.mutation(bindBrowserUpload, {
        reservationId: reservation.reservationId,
        storageId,
      });

      const { jobId } = await t.mutation(createJob, {
        tool: "merge",
        inputs: [
          {
            reservationId: reservation.reservationId,
            storageId,
            filename: "sample.pdf",
            sizeBytes: 4,
          },
        ],
      });

      const claimed = await t.mutation(claimNextJob, {
        workerId: "worker-1",
        workerToken: "test-worker-token",
      });
      expect(claimed?._id).toBe(jobId);
      expect(claimed?.status).toBe("running");
      expect(claimed?.attempts).toBe(1);
    } finally {
      if (previousWorkerToken === undefined) {
        delete process.env.ZENPDF_WORKER_TOKEN;
      } else {
        process.env.ZENPDF_WORKER_TOKEN = previousWorkerToken;
      }
    }
  });

  it("rejects failure after lease expiry and lets another worker reclaim", async () => {
    const t = convexTest(schema, modules).withIdentity({
      subject: "lease_failure_user",
      email: "lease-failure@example.com",
    });
    await t.mutation(backfillStorageReferences, { maxJobs: 25 });
    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const reservation = await t.mutation(beginBrowserUpload, {
        filename: "sample.pdf",
        sizeBytes: 5,
        contentType: "application/octet-stream",
      });
      const storageId = await storeBrowserFile(t, "input");
      await t.mutation(bindBrowserUpload, {
        reservationId: reservation.reservationId,
        storageId,
      });
      const { jobId } = await t.mutation(createJob, {
        tool: "merge",
        inputs: [
          {
            reservationId: reservation.reservationId,
            storageId,
            filename: "sample.pdf",
            sizeBytes: 5,
          },
        ],
      });
      await t.mutation(claimNextJob, {
        workerId: "worker-stale",
        workerToken: "test-worker-token",
      });
      await t.run(async (ctx) => {
        await ctx.db.patch(jobId as never, { claimExpiresAt: Date.now() - 1 });
      });

      const rejected = await t.mutation(failJob, {
        jobId,
        workerId: "worker-stale",
        errorCode: "SERVICE_CAPACITY_TEMPORARY",
        workerToken: "test-worker-token",
      });
      expect(rejected?.status).toBe("running");
      expect(rejected?.claimedBy).toBe("worker-stale");

      const reclaimed = await t.mutation(claimNextJob, {
        workerId: "worker-recovery",
        workerToken: "test-worker-token",
      });
      expect(reclaimed?._id).toBe(jobId);
      expect(reclaimed?.status).toBe("running");
      expect((reclaimed as ClaimedJob & { claimedBy?: string })?.claimedBy).toBe(
        "worker-recovery",
      );
    } finally {
      if (previousWorkerToken === undefined) {
        delete process.env.ZENPDF_WORKER_TOKEN;
      } else {
        process.env.ZENPDF_WORKER_TOKEN = previousWorkerToken;
      }
    }
  });

  it("issues worker uploads only to the lease owner and deletes abandoned storage", async () => {
    const t = convexTest(schema, modules).withIdentity({
      subject: "user_123",
      email: "user@example.com",
    });
    await t.mutation(backfillStorageReferences, { maxJobs: 25 });
    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const reservation = await t.mutation(beginBrowserUpload, {
        filename: "sample.pdf",
        sizeBytes: 5,
        contentType: "application/octet-stream",
      });
      const inputStorageId = await storeBrowserFile(t, "input");
      await t.mutation(bindBrowserUpload, {
        reservationId: reservation.reservationId,
        storageId: inputStorageId,
      });
      const { jobId } = await t.mutation(createJob, {
        tool: "merge",
        inputs: [
          {
            reservationId: reservation.reservationId,
            storageId: inputStorageId,
            filename: "sample.pdf",
            sizeBytes: 5,
          },
        ],
      });
      await t.mutation(claimNextJob, {
        workerId: "worker-1",
        workerToken: "test-worker-token",
      });

      expect(
        await t.mutation(beginWorkerUpload, {
          jobId,
          workerId: "worker-2",
          filename: "output.pdf",
          sizeBytes: 6,
          workerToken: "test-worker-token",
        }),
      ).toBeNull();
      const pending = await t.mutation(beginWorkerUpload, {
        jobId,
        workerId: "worker-1",
        filename: "output.pdf",
        sizeBytes: 6,
        workerToken: "test-worker-token",
      });
      expect(pending?.uploadUrl).toMatch(/^https?:\/\//);
      expect(pending!.uploadDeadlineAt).toBeGreaterThan(Date.now());
      const pendingRecord = await t.run(async (ctx) =>
        ctx.db.get(pending!.pendingUploadId as Id<"pendingUploads">),
      );
      expect(pendingRecord!.expiresAt - pending!.uploadDeadlineAt).toBeGreaterThan(
        23 * 60 * 60 * 1000,
      );

      const outputStorageId = await t.run(async (ctx) =>
        ctx.storage.store(new Blob(["output"])),
      );
      expect(
        await t.mutation(registerWorkerUpload, {
          pendingUploadId: pending!.pendingUploadId,
          workerId: "worker-1",
          storageId: outputStorageId,
          workerToken: "test-worker-token",
        }),
      ).toBe(true);
      expect(
        await t.query(getWorkerUploadState, {
          jobId,
          pendingUploadId: pending!.pendingUploadId,
          workerId: "worker-1",
          storageId: outputStorageId,
          workerToken: "test-worker-token",
        }),
      ).toBe("registered");
      expect(
        await t.mutation(discardWorkerUpload, {
          pendingUploadId: pending!.pendingUploadId,
          workerId: "worker-1",
          workerToken: "test-worker-token",
        }),
      ).toBe(true);
      expect(
        await t.run(async (ctx) => ctx.db.system.get(outputStorageId)),
      ).toBeNull();
      expect(
        await t.query(getWorkerUploadState, {
          jobId,
          pendingUploadId: pending!.pendingUploadId,
          workerId: "worker-1",
          storageId: outputStorageId,
          workerToken: "test-worker-token",
        }),
      ).toBe("deleted");
    } finally {
      if (previousWorkerToken === undefined) {
        delete process.env.ZENPDF_WORKER_TOKEN;
      } else {
        process.env.ZENPDF_WORKER_TOKEN = previousWorkerToken;
      }
    }
  });
});
