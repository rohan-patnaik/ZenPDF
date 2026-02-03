import { v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import { mutation, query } from "./_generated/server";

import { resolveUser } from "./lib/auth";
import { throwFriendlyError } from "./lib/errors";
import { assertWorkerToken } from "./lib/worker_auth";

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
    const job = await ctx.db.get(args.jobId);
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
