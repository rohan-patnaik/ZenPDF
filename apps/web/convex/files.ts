import { v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import { mutation, query } from "./_generated/server";

import { resolveOrCreateUser, resolveUser } from "./lib/auth";
import {
  BROWSER_UPLOAD_RESERVATION_TTL_MS,
  MAX_OUTSTANDING_BROWSER_UPLOADS,
  normalizeAnonId,
  normalizeUploadIntent,
  reservationBelongsTo,
} from "./lib/browser_uploads";
import { throwFriendlyError } from "./lib/errors";
import { assertWorkerToken } from "./lib/worker_auth";
import { resolveGlobalLimits, resolvePlanLimits } from "./lib/limits";
import {
  storageIdHasAuthoritativeOwner,
  storageReferenceIndexIsComplete,
} from "./lib/storage_ownership";
import { MAX_AUTHORITATIVE_STORAGE_BYTES } from "./lib/storage_limits";

const WORKER_UPLOAD_DEADLINE_MS = 2 * 60 * 1000;
const WORKER_UPLOAD_RECOVERY_MS = 24 * 60 * 60 * 1000;

export const beginBrowserUpload = mutation({
  args: {
    anonId: v.optional(v.string()),
    filename: v.string(),
    sizeBytes: v.number(),
    contentType: v.string(),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const { userId, tier } = await resolveOrCreateUser(ctx);
    const anonId = userId ? undefined : normalizeAnonId(args.anonId);
    if (!userId && !anonId) {
      throwFriendlyError("USER_SESSION_REQUIRED");
    }
    const limits = await resolvePlanLimits(ctx, tier);
    const intent = normalizeUploadIntent(
      args.filename,
      args.sizeBytes,
      args.contentType,
      Math.floor(limits.maxMbPerFile * 1024 * 1024),
    );
    const activeStatuses = ["issued", "bound"] as const;
    let outstanding = 0;
    for (const status of activeStatuses) {
      const reservations = userId
        ? await ctx.db
            .query("browserUploadReservations")
            .withIndex("by_user_status_expiry", (q) =>
              q.eq("userId", userId).eq("status", status).gt("expiresAt", now),
            )
            .take(MAX_OUTSTANDING_BROWSER_UPLOADS + 1)
        : await ctx.db
            .query("browserUploadReservations")
            .withIndex("by_anon_status_expiry", (q) =>
              q.eq("anonId", anonId).eq("status", status).gt("expiresAt", now),
            )
            .take(MAX_OUTSTANDING_BROWSER_UPLOADS + 1);
      outstanding += reservations.length;
      if (outstanding >= MAX_OUTSTANDING_BROWSER_UPLOADS) {
        throwFriendlyError("USER_LIMIT_MAX_FILES");
      }
    }
    const expiresAt = now + BROWSER_UPLOAD_RESERVATION_TTL_MS;
    const reservationId = await ctx.db.insert("browserUploadReservations", {
      userId,
      anonId,
      status: "issued",
      ...intent,
      createdAt: now,
      expiresAt,
    });
    return {
      reservationId,
      uploadUrl: await ctx.storage.generateUploadUrl(),
      expiresAt,
    };
  },
});

export const bindBrowserUpload = mutation({
  args: {
    reservationId: v.id("browserUploadReservations"),
    storageId: v.id("_storage"),
    anonId: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const { userId } = await resolveUser(ctx);
    const anonId = userId ? undefined : normalizeAnonId(args.anonId);
    const reservation = await ctx.db.get(args.reservationId);
    if (!reservation) {
      return throwFriendlyError("USER_INPUT_INVALID");
    }
    if (
      !reservationBelongsTo(reservation, userId, anonId) ||
      reservation.status !== "issued"
    ) {
      throwFriendlyError("USER_INPUT_INVALID");
    }
    if (reservation.expiresAt <= now) {
      return throwFriendlyError("USER_INPUT_INVALID");
    }
    if (!(await storageReferenceIndexIsComplete(ctx))) {
      return throwFriendlyError("USER_INPUT_INVALID");
    }
    const metadata = await ctx.db.system.get(args.storageId);
    if (
      !metadata ||
      metadata.size !== reservation.sizeBytes ||
      metadata.contentType?.toLowerCase() !== reservation.contentType ||
      metadata._creationTime <= reservation._creationTime ||
      metadata._creationTime > reservation.expiresAt
    ) {
      throwFriendlyError("USER_INPUT_INVALID");
    }
    if (await storageIdHasAuthoritativeOwner(ctx, args.storageId)) {
      return throwFriendlyError("USER_INPUT_INVALID");
    }
    await ctx.db.patch(reservation._id, {
      status: "bound",
      storageId: args.storageId,
      boundAt: now,
    });
    return {
      reservationId: reservation._id,
      storageId: args.storageId,
    };
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
    if (
      !args.filename ||
      args.sizeBytes < 0 ||
      args.sizeBytes > MAX_AUTHORITATIVE_STORAGE_BYTES ||
      !Number.isSafeInteger(args.sizeBytes)
    ) {
      return null;
    }
    const limits = await resolveGlobalLimits(ctx);
    const ttlMs = Math.min(limits.artifactTtlHours, 1) * 60 * 60 * 1000;
    const uploadDeadlineAt = now + WORKER_UPLOAD_DEADLINE_MS;
    const pendingUploadId = await ctx.db.insert("pendingUploads", {
      jobId: args.jobId,
      workerId: args.workerId,
      filename: args.filename,
      sizeBytes: args.sizeBytes,
      uploadDeadlineAt,
      createdAt: now,
      expiresAt: now + Math.max(ttlMs, WORKER_UPLOAD_RECOVERY_MS),
    });
    return {
      pendingUploadId,
      uploadUrl: await ctx.storage.generateUploadUrl(),
      uploadDeadlineAt,
    };
  },
});

export const getWorkerUploadState = query({
  args: {
    jobId: v.id("jobs"),
    pendingUploadId: v.id("pendingUploads"),
    workerId: v.string(),
    storageId: v.id("_storage"),
    workerToken: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    assertWorkerToken(args.workerToken);
    const pending = await ctx.db.get(args.pendingUploadId);
    if (pending) {
      if (
        pending.jobId !== args.jobId ||
        pending.workerId !== args.workerId ||
        (pending.storageId && pending.storageId !== args.storageId)
      ) {
        return "mismatch";
      }
      return pending.storageId === args.storageId
        ? "registered"
        : "unregistered";
    }
    const artifact = await ctx.db
      .query("artifacts")
      .withIndex("by_job", (q) => q.eq("jobId", args.jobId))
      .filter((q) => q.eq(q.field("storageId"), args.storageId))
      .first();
    if (artifact) {
      return "committed";
    }
    const stored = await ctx.db.system.get(args.storageId);
    return stored ? "orphaned" : "deleted";
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
    const now = Date.now();
    const pending = await ctx.db.get(args.pendingUploadId);
    if (
      !pending ||
      pending.workerId !== args.workerId ||
      pending.expiresAt <= now
    ) {
      return false;
    }
    if (pending.storageId && pending.storageId !== args.storageId) {
      return false;
    }
    const [cleanupRecord, metadata] = await Promise.all([
      ctx.db
        .query("storageCleanupRecords")
        .withIndex("by_storage", (q) => q.eq("storageId", args.storageId))
        .first(),
      ctx.db.system.get(args.storageId),
    ]);
    if (!metadata || metadata.size !== pending.sizeBytes || cleanupRecord) {
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
    if (
      pending.storageId &&
      args.storageId &&
      pending.storageId !== args.storageId
    ) {
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
      (output: { storageId: Id<"_storage"> }) =>
        output.storageId === args.storageId,
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
