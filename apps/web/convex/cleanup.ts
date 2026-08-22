import { v } from "convex/values";

import { mutation } from "./_generated/server";
import { storageIdHasLiveAuthoritativeOwner } from "./lib/storage_ownership";

export const cleanupExpiredArtifacts = mutation({
  args: {
    batchSize: v.optional(v.number()),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const requestedBatchSize = args.batchSize ?? 50;
    const batchSize = Number.isFinite(requestedBatchSize)
      ? Math.max(1, Math.min(Math.floor(requestedBatchSize), 500))
      : 50;
    const expired = await ctx.db
      .query("artifacts")
      .withIndex("by_expires", (q) => q.lt("expiresAt", now))
      .take(batchSize);

    let deleted = 0;

    for (const artifact of expired) {
      try {
        const meta = await ctx.db.system.get(artifact.storageId);
        const protectedStorage = await storageIdHasLiveAuthoritativeOwner(
          ctx,
          artifact.storageId,
          now,
          { artifactId: artifact._id },
        );
        if (meta && !protectedStorage) {
          await ctx.storage.delete(artifact.storageId);
        }
        await ctx.db.delete(artifact._id);
        deleted += 1;
      } catch {
        continue;
      }
    }

    const pending = await ctx.db
      .query("pendingUploads")
      .withIndex("by_expires", (q) => q.lt("expiresAt", now))
      .take(Math.max(batchSize - deleted, 0));
    let pendingDeleted = 0;
    for (const upload of pending) {
      try {
        if (upload.storageId) {
          const meta = await ctx.db.system.get(upload.storageId);
          const protectedStorage = await storageIdHasLiveAuthoritativeOwner(
            ctx,
            upload.storageId,
            now,
            { pendingUploadId: upload._id },
          );
          if (meta && !protectedStorage) {
            await ctx.storage.delete(upload.storageId);
          }
        }
        await ctx.db.delete(upload._id);
        pendingDeleted += 1;
      } catch {
        continue;
      }
    }

    return { deleted, pendingDeleted };
  },
});
