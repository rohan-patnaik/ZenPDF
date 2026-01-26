import type { GenericDataModel, GenericMutationCtx } from "convex/server";
import { mutationGeneric as mutation, queryGeneric as query } from "convex/server";
import { v } from "convex/values";

import { resolveOrCreateUser } from "./lib/auth";
import { resolveBudgetState } from "./lib/budget";
import { throwFriendlyError } from "./lib/errors";
import { assertTransition } from "./lib/job-state";
import { checkGlobalLimits, checkPlanLimits } from "./lib/limit-checks";
import { resolveGlobalLimits, resolvePlanLimits } from "./lib/limits";
import {
  incrementGlobalUsage,
  incrementUsage,
  resolveGlobalUsageCounter,
  resolveUsageCounter,
} from "./lib/usage";

const jobInput = v.object({
  storageId: v.id("_storage"),
  filename: v.string(),
  sizeBytes: v.optional(v.number()),
});

const jobOutput = v.object({
  storageId: v.id("_storage"),
  filename: v.string(),
  sizeBytes: v.optional(v.number()),
});

const HEAVY_TOOLS = new Set([
  "ocr-searchable-pdf",
  "pdf-to-word-ocr",
  "pdf-to-excel-ocr",
  "pdfa",
]);

type MutationCtx = GenericMutationCtx<GenericDataModel>;

const countJobs = async (
  ctx: MutationCtx,
  filter: { userId?: string; status: "queued" | "running" },
  limit: number,
) => {
  if (filter.userId) {
    const results = await ctx.db
      .query("jobs")
      .withIndex("by_user_status", (q) =>
        q.eq("userId", filter.userId).eq("status", filter.status),
      )
      .take(limit + 1);
    return results.length;
  }

  const results = await ctx.db
    .query("jobs")
    .withIndex("by_status", (q) => q.eq("status", filter.status))
    .take(limit + 1);
  return results.length;
};

export const createJob = mutation({
  args: {
    tool: v.string(),
    inputs: v.array(jobInput),
    config: v.optional(v.any()),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const { userId, tier } = await resolveOrCreateUser(ctx);
    const planLimits = await resolvePlanLimits(ctx, tier);
    const globalLimits = await resolveGlobalLimits(ctx);
    const budget = await resolveBudgetState(ctx, now);

    if (budget.monthlyBudgetUsage >= 1) {
      throwFriendlyError("SERVICE_CAPACITY_MONTHLY_BUDGET");
    }

    if (!budget.heavyToolsEnabled && HEAVY_TOOLS.has(args.tool)) {
      throwFriendlyError("SERVICE_CAPACITY_TEMPORARY");
    }

    const { counter: usageCounter } = await resolveUsageCounter(ctx, userId, now);
    const { counter: globalUsage } = await resolveGlobalUsageCounter(ctx, now);

    const activeUserJobs =
      (await countJobs(ctx, { userId, status: "queued" }, planLimits.maxConcurrentJobs)) +
      (await countJobs(ctx, { userId, status: "running" }, planLimits.maxConcurrentJobs));
    const activeGlobalJobs =
      (await countJobs(ctx, { status: "queued" }, globalLimits.maxConcurrentJobs)) +
      (await countJobs(ctx, { status: "running" }, globalLimits.maxConcurrentJobs));

    const planCheck = checkPlanLimits(
      args.inputs,
      planLimits,
      {
        jobsUsed: usageCounter?.jobsUsed ?? 0,
        minutesUsed: usageCounter?.minutesUsed ?? 0,
      },
      activeUserJobs,
    );

    if (!planCheck.ok) {
      throwFriendlyError(planCheck.code, planCheck.details);
    }

    const globalCheck = checkGlobalLimits(
      globalLimits,
      {
        jobsUsed: globalUsage?.jobsUsed ?? 0,
        minutesUsed: globalUsage?.minutesUsed ?? 0,
      },
      activeGlobalJobs,
    );

    if (!globalCheck.ok) {
      throwFriendlyError(globalCheck.code, globalCheck.details);
    }

    const jobId = await ctx.db.insert("jobs", {
      userId,
      tier,
      tool: args.tool,
      status: "queued",
      progress: 0,
      errorCode: undefined,
      errorMessage: undefined,
      claimedBy: undefined,
      claimExpiresAt: undefined,
      attempts: 0,
      maxAttempts: globalLimits.jobMaxAttempts,
      startedAt: undefined,
      finishedAt: undefined,
      lastHeartbeatAt: undefined,
      inputs: args.inputs,
      outputs: undefined,
      createdAt: now,
      updatedAt: now,
    });

    await incrementUsage(ctx, userId, tier, now, { jobs: 1 });
    await incrementGlobalUsage(ctx, now, { jobs: 1 });

    return { jobId };
  },
});

export const claimNextJob = mutation({
  args: {
    workerId: v.string(),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const globalLimits = await resolveGlobalLimits(ctx);

    const queued = await ctx.db
      .query("jobs")
      .withIndex("by_status_created", (q) => q.eq("status", "queued"))
      .order("asc")
      .first();

    const stale =
      queued ??
      (await ctx.db
        .query("jobs")
        .withIndex("by_status_lock", (q) => q.eq("status", "running"))
        .filter((q) => q.lt(q.field("claimExpiresAt"), now))
        .order("asc")
        .first());

    if (!stale) {
      return null;
    }

    if (stale.attempts >= stale.maxAttempts) {
      await ctx.db.patch(stale._id, {
        status: "failed",
        errorCode: "SERVICE_CAPACITY_TEMPORARY",
        errorMessage: "Job exceeded retry attempts.",
        finishedAt: now,
        updatedAt: now,
      });
      return null;
    }

    assertTransition(stale.status, "running");

    await ctx.db.patch(stale._id, {
      status: "running",
      claimedBy: args.workerId,
      claimExpiresAt: now + globalLimits.leaseDurationMs,
      attempts: stale.attempts + 1,
      startedAt: stale.startedAt ?? now,
      lastHeartbeatAt: now,
      updatedAt: now,
    });

    return await ctx.db.get(stale._id);
  },
});

export const reportJobProgress = mutation({
  args: {
    jobId: v.id("jobs"),
    workerId: v.string(),
    progress: v.number(),
  },
  handler: async (ctx, args) => {
    const job = await ctx.db.get(args.jobId);
    if (!job) {
      return null;
    }

    if (job.status !== "running" || job.claimedBy !== args.workerId) {
      return job;
    }

    const now = Date.now();
    const globalLimits = await resolveGlobalLimits(ctx);

    await ctx.db.patch(args.jobId, {
      progress: Math.min(Math.max(args.progress, 0), 100),
      lastHeartbeatAt: now,
      claimExpiresAt: now + globalLimits.leaseDurationMs,
      updatedAt: now,
    });

    return await ctx.db.get(args.jobId);
  },
});

export const completeJob = mutation({
  args: {
    jobId: v.id("jobs"),
    workerId: v.string(),
    outputs: v.array(jobOutput),
    minutesUsed: v.optional(v.number()),
    bytesProcessed: v.optional(v.number()),
  },
  handler: async (ctx, args) => {
    const job = await ctx.db.get(args.jobId);
    if (!job) {
      return null;
    }

    if (job.status !== "running" || job.claimedBy !== args.workerId) {
      return job;
    }

    const now = Date.now();
    assertTransition(job.status, "succeeded");

    await ctx.db.patch(args.jobId, {
      status: "succeeded",
      progress: 100,
      outputs: args.outputs,
      finishedAt: now,
      updatedAt: now,
      errorCode: undefined,
      errorMessage: undefined,
    });

    const globalLimits = await resolveGlobalLimits(ctx);
    const expiresAt = now + globalLimits.artifactTtlHours * 60 * 60 * 1000;

    for (const output of args.outputs) {
      await ctx.db.insert("artifacts", {
        ownerId: job.userId,
        jobId: job._id,
        storageId: output.storageId,
        kind: "output",
        filename: output.filename,
        sizeBytes: output.sizeBytes,
        createdAt: now,
        expiresAt,
      });
    }

    const minutesUsed = args.minutesUsed ?? 0;
    const bytesProcessed = args.bytesProcessed ?? 0;

    await incrementUsage(ctx, job.userId, job.tier, now, {
      minutes: minutesUsed,
      bytes: bytesProcessed,
    });
    await incrementGlobalUsage(ctx, now, {
      minutes: minutesUsed,
      bytes: bytesProcessed,
    });

    return await ctx.db.get(args.jobId);
  },
});

export const failJob = mutation({
  args: {
    jobId: v.id("jobs"),
    workerId: v.string(),
    errorCode: v.string(),
    errorMessage: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    const job = await ctx.db.get(args.jobId);
    if (!job) {
      return null;
    }

    if (job.status !== "running" || job.claimedBy !== args.workerId) {
      return job;
    }

    const now = Date.now();
    assertTransition(job.status, "failed");

    await ctx.db.patch(args.jobId, {
      status: "failed",
      progress: job.progress ?? 0,
      errorCode: args.errorCode,
      errorMessage: args.errorMessage,
      finishedAt: now,
      updatedAt: now,
    });

    return await ctx.db.get(args.jobId);
  },
});

export const getJob = query({
  args: { jobId: v.id("jobs") },
  handler: async (ctx, args) => ctx.db.get(args.jobId),
});
