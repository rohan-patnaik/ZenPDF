import { mutationGeneric as mutation } from "convex/server";
import { v } from "convex/values";

export const cleanupExpiredArtifacts = mutation({
  args: {
    batchSize: v.optional(v.number()),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const batchSize = Math.min(args.batchSize ?? 50, 500);
    const expired = await ctx.db
      .query("artifacts")
      .withIndex("by_expires", (q) => q.lt("expiresAt", now))
      .take(batchSize);

    let deleted = 0;

    for (const artifact of expired) {
      try {
        const meta = await ctx.db.system.get(artifact.storageId);
        if (meta) {
          await ctx.storage.delete(artifact.storageId);
        }
        await ctx.db.delete(artifact._id);
        deleted += 1;
      } catch {
        continue;
      }
    }

    return { deleted };
  },
});
