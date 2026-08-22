import { convexTest } from "convex-test";
import { makeFunctionReference } from "convex/server";
import { describe, expect, it } from "vitest";

import schema from "../../convex/schema";
import type { Id } from "../../convex/_generated/dataModel";
import { monthKey } from "../../convex/lib/time";

const modules = import.meta.glob("../../convex/**/*.ts");

const createJob = makeFunctionReference<
  "mutation",
  {
    tool: string;
    inputs: Array<{ storageId: string; filename: string; sizeBytes?: number }>;
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

const generateUploadUrl = makeFunctionReference<
  "mutation",
  { anonId?: string },
  string
>("files:generateUploadUrl");

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
  it("keeps generic uploads browser-scoped even with a valid worker token", async () => {
    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const anonymous = convexTest(schema, modules);
      await expect(
        anonymous.mutation(
          generateUploadUrl,
          { workerToken: "test-worker-token" } as never,
        ),
      ).rejects.toThrow();
      await expect(anonymous.mutation(generateUploadUrl, {})).rejects.toThrow();

      const signedIn = convexTest(schema, modules).withIdentity({
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
      await expect(signedIn.mutation(generateUploadUrl, {})).resolves.toMatch(
        /^https?:\/\//,
      );
      await expect(
        anonymous.mutation(generateUploadUrl, { anonId: "anon-browser" }),
      ).resolves.toMatch(/^https?:\/\//);
    } finally {
      if (previousWorkerToken === undefined) {
        delete process.env.ZENPDF_WORKER_TOKEN;
      } else {
        process.env.ZENPDF_WORKER_TOKEN = previousWorkerToken;
      }
    }
  });

  it("rejects jobs when monthly budget is exceeded", async () => {
    const t = convexTest(schema, modules);
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

    const storageId = await t.run(async (ctx) =>
      ctx.storage.store(new Blob(["test"])),
    );

    const snapshot = await t.query(getCapacitySnapshot, {});
    expect(snapshot.budget.monthlyBudgetUsage).toBeGreaterThanOrEqual(1);

    let errorCode = "";
    try {
      await t.mutation(createJob, {
        tool: "merge",
        inputs: [{ storageId, filename: "sample.pdf", sizeBytes: 5000 }],
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

    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const storageId = await t.run(async (ctx) =>
        ctx.storage.store(new Blob(["test"])),
      );

      const { jobId } = await t.mutation(createJob, {
        tool: "merge",
        inputs: [{ storageId, filename: "sample.pdf", sizeBytes: 5000 }],
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
    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const storageId = await t.run(async (ctx) =>
        ctx.storage.store(new Blob(["input"])),
      );
      const { jobId } = await t.mutation(createJob, {
        tool: "merge",
        inputs: [{ storageId, filename: "sample.pdf", sizeBytes: 5 }],
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
    const previousWorkerToken = process.env.ZENPDF_WORKER_TOKEN;
    process.env.ZENPDF_WORKER_TOKEN = "test-worker-token";
    try {
      const inputStorageId = await t.run(async (ctx) =>
        ctx.storage.store(new Blob(["input"])),
      );
      const { jobId } = await t.mutation(createJob, {
        tool: "merge",
        inputs: [
          { storageId: inputStorageId, filename: "sample.pdf", sizeBytes: 5 },
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
