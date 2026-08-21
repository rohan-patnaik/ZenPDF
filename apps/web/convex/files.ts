import { v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import { mutation, query } from "./_generated/server";

import { resolveUser } from "./lib/auth";
import { throwFriendlyError } from "./lib/errors";
import { assertWorkerToken } from "./lib/worker_auth";
import { resolveGlobalLimits } from "./lib/limits";

export const generateUploadUrl = mutation({
  args: { anonId: v.optional(v.string()), workerToken: v.optional(v.string()) },
  handler: async (ctx, args) => {
    if (args.workerToken) {
      assertWorkerToken(args.workerToken);
      return ctx.storage.generateUploadUrl();
    }

    const { userId } = await resolveUser(ctx);
    if (!userId && !args.anonId) {
      throwFriendlyError("USER_SESSION_REQUIRED");
    }
    return ctx.storage.generateUploadUrl();
  },
});

export const beginWorkerUpload = mutation({
  args: {
    jobId: v.id("jobs"),
    workerId: v.string(),
    filename: v.string(),
    sizeBytes: v.number(),
    workerToken: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    assertWorkerToken(args.workerToken);
    const job = await ctx.db.get(args.jobId);
    const now = Date.now();
    if (
      !job ||
      job.status !== "running" ||
      job.claimedBy !== args.workerId ||
      (job.claimExpiresAt ?? 0) <= now
    ) {
      return null;
    }
    if (!args.filename || args.sizeBytes < 0 || !Number.isSafeInteger(args.sizeBytes)) {
      return null;
    }
    const limits = await resolveGlobalLimits(ctx);
    const ttlMs = Math.min(limits.artifactTtlHours, 1) * 60 * 60 * 1000;
    const pendingUploadId = await ctx.db.insert("pendingUploads", {
      jobId: args.jobId,
      workerId: args.workerId,
      filename: args.filename,
      sizeBytes: args.sizeBytes,
      createdAt: now,
      expiresAt: now + Math.max(ttlMs, 5 * 60 * 1000),
    });
    return {
      pendingUploadId,
      uploadUrl: await ctx.storage.generateUploadUrl(),
    };
  },
});

export const registerWorkerUpload = mutation({
  args: {
    pendingUploadId: v.id("pendingUploads"),
    workerId: v.string(),
    storageId: v.id("_storage"),
    workerToken: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    assertWorkerToken(args.workerToken);
    const pending = await ctx.db.get(args.pendingUploadId);
    if (!pending || pending.workerId !== args.workerId) {
      return false;
    }
    if (pending.storageId && pending.storageId !== args.storageId) {
      return false;
    }
    await ctx.db.patch(args.pendingUploadId, { storageId: args.storageId });
    return true;
  },
});

export const discardWorkerUpload = mutation({
  args: {
    pendingUploadId: v.id("pendingUploads"),
    workerId: v.string(),
    storageId: v.optional(v.id("_storage")),
    workerToken: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    assertWorkerToken(args.workerToken);
    const pending = await ctx.db.get(args.pendingUploadId);
    if (!pending || pending.workerId !== args.workerId) {
      return false;
    }
    if (pending.storageId && args.storageId && pending.storageId !== args.storageId) {
      return false;
    }
    const storageId = pending.storageId ?? args.storageId;
    if (storageId) {
      const stored = await ctx.db.system.get(storageId);
      if (stored) {
        await ctx.storage.delete(storageId);
      }
    }
    await ctx.db.delete(args.pendingUploadId);
    return true;
  },
});

export const getDownloadUrl = query({
  args: { storageId: v.id("_storage"), workerToken: v.optional(v.string()) },
  handler: async (ctx, args) => {
    assertWorkerToken(args.workerToken);
    return ctx.storage.getUrl(args.storageId);
  },
});

export const getOutputDownloadUrl = query({
  args: {
    jobId: v.id("jobs"),
    storageId: v.id("_storage"),
    anonId: v.optional(v.string()),
    allowAnonAccess: v.optional(v.boolean()),
  },
  handler: async (ctx, args) => {
    const { userId } = await resolveUser(ctx);
    const allowAnonFallback = args.allowAnonAccess === true;
    if (allowAnonFallback) {
      const artifact = await ctx.db
        .query("artifacts")
        .withIndex("by_job", (q) => q.eq("jobId", args.jobId))
        .filter((q) => q.eq(q.field("storageId"), args.storageId))
        .first();
      if (!artifact) {
        return null;
      }
      return ctx.storage.getUrl(args.storageId);
    }
    const job = await ctx.db.get(args.jobId);
    if (!job) {
      return null;
    }
    if (job.userId) {
      if (job.userId !== userId) {
        return null;
      }
    } else {
      if (!job.anonId || job.anonId !== args.anonId) {
        return null;
      }
    }
    const allowed = (job.outputs ?? []).some(
      (output: { storageId: Id<"_storage"> }) => output.storageId === args.storageId,
    );
    if (!allowed) {
      return null;
    }
    const artifact = await ctx.db
      .query("artifacts")
      .withIndex("by_job", (q) => q.eq("jobId", args.jobId))
      .filter((q) => q.eq(q.field("storageId"), args.storageId))
      .first();
    if (!artifact) {
      return null;
    }
    return ctx.storage.getUrl(args.storageId);
  },
});
