import { convexTest } from "convex-test";
import { makeFunctionReference } from "convex/server";
import { describe, expect, it } from "vitest";

import schema from "../../convex/schema";
import { monthKey } from "../../convex/lib/time";

const modules = import.meta.glob("../../convex/**/*.ts");

const createJob = makeFunctionReference<
  "mutation",
  {
    tool: string;
    inputs: Array<{ storageId: string; filename: string; sizeBytes?: number }>;
    config?: unknown;
  },
  { jobId: string }
>("jobs:createJob");

type ClaimedJob = {
  _id: string;
  status: string;
  attempts: number;
} | null;

const claimNextJob = makeFunctionReference<
  "mutation",
  { workerId: string },
  ClaimedJob
>("jobs:claimNextJob");

type CapacitySnapshot = {
  budget: { monthlyBudgetUsage: number; status: string };
};

const getCapacitySnapshot = makeFunctionReference<
  "query",
  Record<string, never>,
  CapacitySnapshot
>("capacity:getCapacitySnapshot");

describe("job system", () => {
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

    const storageId = await t.run(async (ctx) =>
      ctx.storage.store(new Blob(["test"])),
    );

    const { jobId } = await t.mutation(createJob, {
      tool: "merge",
      inputs: [{ storageId, filename: "sample.pdf", sizeBytes: 5000 }],
    });

    const claimed = await t.mutation(claimNextJob, { workerId: "worker-1" });
    expect(claimed?._id).toBe(jobId);
    expect(claimed?.status).toBe("running");
    expect(claimed?.attempts).toBe(1);
  });
});
