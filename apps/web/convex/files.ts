import { mutationGeneric as mutation, queryGeneric as query } from "convex/server";
import { ConvexError, v } from "convex/values";

import { resolveUser } from "./lib/auth";

export const generateUploadUrl = mutation({
  args: {},
  handler: async (ctx) => ctx.storage.generateUploadUrl(),
});

export const getDownloadUrl = query({
  args: { storageId: v.id("_storage"), workerToken: v.optional(v.string()) },
  handler: async (ctx, args) => {
    const workerToken = process.env.ZENPDF_WORKER_TOKEN;
    if (!workerToken || args.workerToken !== workerToken) {
      throw new ConvexError({
        code: "UNAUTHORIZED",
        message: "Worker token required.",
      });
    }
    return ctx.storage.getUrl(args.storageId);
  },
});

export const getOutputDownloadUrl = query({
  args: {
    jobId: v.id("jobs"),
    storageId: v.id("_storage"),
    anonId: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    const { userId } = await resolveUser(ctx);
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
      (output) => output.storageId === args.storageId,
    );
    if (!allowed) {
      return null;
    }
    return ctx.storage.getUrl(args.storageId);
  },
});
