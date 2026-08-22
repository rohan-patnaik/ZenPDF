import type { Id } from "../_generated/dataModel";
import type { MutationCtx } from "../_generated/server";

export const STORAGE_CLEANUP_STATE_NAME = "storage-orphan-sweep-v1";

type OwnershipExclusions = {
  artifactId?: Id<"artifacts">;
  pendingUploadId?: Id<"pendingUploads">;
  cleanupRecordId?: Id<"storageCleanupRecords">;
};

export const storageIdHasLiveAuthoritativeOwner = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
  now: number,
  exclude: OwnershipExclusions = {},
) => {
  const [
    referenceIndexComplete,
    jobReference,
    artifact,
    pendingUpload,
    reservation,
    cleanupRecord,
  ] = await Promise.all([
    storageReferenceIndexIsComplete(ctx),
    ctx.db
      .query("storageReferences")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .first(),
    ctx.db
      .query("artifacts")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .filter((q) => q.neq(q.field("_id"), exclude.artifactId))
      .first(),
    ctx.db
      .query("pendingUploads")
      .withIndex("by_storage_expiry", (q) =>
        q.eq("storageId", storageId).gt("expiresAt", now),
      )
      .filter((q) => q.neq(q.field("_id"), exclude.pendingUploadId))
      .first(),
    ctx.db
      .query("browserUploadReservations")
      .withIndex("by_storage_expiry", (q) =>
        q.eq("storageId", storageId).gt("expiresAt", now),
      )
      .first(),
    ctx.db
      .query("storageCleanupRecords")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .filter((q) => q.neq(q.field("_id"), exclude.cleanupRecordId))
      .first(),
  ]);
  return Boolean(
    !referenceIndexComplete ||
    jobReference ||
    artifact ||
    pendingUpload ||
    reservation ||
    cleanupRecord,
  );
};

export const storageReferenceIndexIsComplete = async (ctx: MutationCtx) => {
  const state = await ctx.db
    .query("storageCleanupState")
    .withIndex("by_name", (q) => q.eq("name", STORAGE_CLEANUP_STATE_NAME))
    .unique();
  return state?.backfillStatus === "complete";
};

export const storageIdHasAuthoritativeOwner = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
) => {
  const [jobReference, artifact, pendingUpload, reservation, cleanupRecord] =
    await Promise.all([
      ctx.db
        .query("storageReferences")
        .withIndex("by_storage", (q) => q.eq("storageId", storageId))
        .first(),
      ctx.db
        .query("artifacts")
        .withIndex("by_storage", (q) => q.eq("storageId", storageId))
        .first(),
      ctx.db
        .query("pendingUploads")
        .withIndex("by_storage", (q) => q.eq("storageId", storageId))
        .first(),
      ctx.db
        .query("browserUploadReservations")
        .withIndex("by_storage", (q) => q.eq("storageId", storageId))
        .first(),
      ctx.db
        .query("storageCleanupRecords")
        .withIndex("by_storage", (q) => q.eq("storageId", storageId))
        .first(),
    ]);
  return Boolean(
    jobReference || artifact || pendingUpload || reservation || cleanupRecord,
  );
};

export const storageIdHasConflictingOwner = async (
  ctx: MutationCtx,
  storageId: Id<"_storage">,
  reservationId: Id<"browserUploadReservations">,
) => {
  const [
    jobReference,
    artifact,
    pendingUpload,
    otherReservation,
    cleanupRecord,
  ] = await Promise.all([
    ctx.db
      .query("storageReferences")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .first(),
    ctx.db
      .query("artifacts")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .first(),
    ctx.db
      .query("pendingUploads")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .first(),
    ctx.db
      .query("browserUploadReservations")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .filter((q) => q.neq(q.field("_id"), reservationId))
      .first(),
    ctx.db
      .query("storageCleanupRecords")
      .withIndex("by_storage", (q) => q.eq("storageId", storageId))
      .first(),
  ]);
  return Boolean(
    jobReference ||
    artifact ||
    pendingUpload ||
    otherReservation ||
    cleanupRecord,
  );
};
