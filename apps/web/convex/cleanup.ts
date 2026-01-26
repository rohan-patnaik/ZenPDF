import { mutationGeneric as mutation } from "convex/server";
import { v } from "convex/values";

export const cleanupExpiredArtifacts = mutation({
  args: {
    batchSize: v.optional(v.number()),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const batchSize = args.batchSize ?? 50;
    const expired = await ctx.db
      .query("artifacts")
      .withIndex("by_expires", (q) => q.lt("expiresAt", now))
      .take(batchSize);

    for (const artifact of expired) {
      await ctx.storage.delete(artifact.storageId);
      await ctx.db.delete(artifact._id);
    }

    return { deleted: expired.length };
  },
});
