import { defineSchema, defineTable } from "convex/server";
import { v } from "convex/values";

const tier = v.union(
  v.literal("ANON"),
  v.literal("FREE_ACCOUNT"),
);

const jobStatus = v.union(
  v.literal("queued"),
  v.literal("running"),
  v.literal("succeeded"),
  v.literal("failed"),
  v.literal("cancelled"),
);

const feedbackStatus = v.union(
  v.literal("open"),
  v.literal("resolved"),
);

const browserUploadReservationStatus = v.union(
  v.literal("issued"),
  v.literal("bound"),
  v.literal("consumed"),
  v.literal("expired"),
  v.literal("deleted"),
);

const storageReferenceKind = v.union(
  v.literal("jobInput"),
  v.literal("jobOutput"),
);

const storageCleanupRecordState = v.union(
  v.literal("candidate"),
  v.literal("deleted"),
);

const storageCleanupRunMode = v.union(
  v.literal("dryRun"),
  v.literal("delete"),
);

const storageCleanupRunStatus = v.union(
  v.literal("running"),
  v.literal("completed"),
  v.literal("failed"),
);

const storageReferenceBackfillStatus = v.union(
  v.literal("pending"),
  v.literal("complete"),
  v.literal("blocked"),
);

export default defineSchema({
  users: defineTable({
    clerkUserId: v.string(),
    email: v.optional(v.string()),
    name: v.optional(v.string()),
    // Legacy field kept optional so existing records remain schema-valid.
    adsFree: v.optional(v.boolean()),
    tier,
    createdAt: v.number(),
    updatedAt: v.number(),
  })
    .index("by_clerk_id", ["clerkUserId"])
    .index("by_email", ["email"]),

  jobs: defineTable({
    userId: v.optional(v.id("users")),
    anonId: v.optional(v.string()),
    tier,
    tool: v.string(),
    status: jobStatus,
    progress: v.optional(v.number()),
    errorCode: v.optional(v.string()),
    errorMessage: v.optional(v.string()),
    toolResult: v.optional(v.any()),
    config: v.optional(v.any()),
    claimedBy: v.optional(v.string()),
    claimExpiresAt: v.optional(v.number()),
    attempts: v.number(),
    maxAttempts: v.number(),
    startedAt: v.optional(v.number()),
    finishedAt: v.optional(v.number()),
    lastHeartbeatAt: v.optional(v.number()),
    inputs: v.array(
      v.object({
        storageId: v.id("_storage"),
        filename: v.string(),
        sizeBytes: v.optional(v.number()),
      }),
    ),
    outputs: v.optional(
      v.array(
        v.object({
          storageId: v.id("_storage"),
          filename: v.string(),
          sizeBytes: v.optional(v.number()),
        }),
      ),
    ),
    createdAt: v.number(),
    updatedAt: v.number(),
  })
    .index("by_status", ["status"])
    .index("by_status_created", ["status", "createdAt"])
    .index("by_status_lock", ["status", "claimExpiresAt"])
    .index("by_user", ["userId", "createdAt"])
    .index("by_user_status", ["userId", "status"])
    .index("by_anon", ["anonId", "createdAt"])
    .index("by_anon_status", ["anonId", "status"])
    .index("by_updated", ["updatedAt"]),

  artifacts: defineTable({
    ownerId: v.optional(v.id("users")),
    jobId: v.optional(v.id("jobs")),
    storageId: v.id("_storage"),
    kind: v.string(),
    filename: v.string(),
    sizeBytes: v.optional(v.number()),
    contentType: v.optional(v.string()),
    createdAt: v.number(),
    expiresAt: v.number(),
  })
    .index("by_owner", ["ownerId", "createdAt"])
    .index("by_job", ["jobId"])
    .index("by_storage", ["storageId"])
    .index("by_expires", ["expiresAt"]),

  pendingUploads: defineTable({
    jobId: v.id("jobs"),
    workerId: v.string(),
    filename: v.string(),
    sizeBytes: v.number(),
    storageId: v.optional(v.id("_storage")),
    uploadDeadlineAt: v.optional(v.number()),
    createdAt: v.number(),
    expiresAt: v.number(),
  })
    .index("by_job", ["jobId"])
    .index("by_storage", ["storageId"])
    .index("by_storage_expiry", ["storageId", "expiresAt"])
    .index("by_expires", ["expiresAt"]),

  browserUploadReservations: defineTable({
    userId: v.optional(v.id("users")),
    anonId: v.optional(v.string()),
    status: browserUploadReservationStatus,
    filename: v.string(),
    sizeBytes: v.number(),
    contentType: v.string(),
    storageId: v.optional(v.id("_storage")),
    jobId: v.optional(v.id("jobs")),
    createdAt: v.number(),
    expiresAt: v.number(),
    boundAt: v.optional(v.number()),
    consumedAt: v.optional(v.number()),
    deletedAt: v.optional(v.number()),
  })
    .index("by_user_status_expiry", ["userId", "status", "expiresAt"])
    .index("by_anon_status_expiry", ["anonId", "status", "expiresAt"])
    .index("by_storage", ["storageId"])
    .index("by_storage_expiry", ["storageId", "expiresAt"])
    .index("by_status_expiry", ["status", "expiresAt"])
    .index("by_job", ["jobId"]),

  storageReferences: defineTable({
    storageId: v.id("_storage"),
    jobId: v.id("jobs"),
    kind: storageReferenceKind,
    createdAt: v.number(),
  })
    .index("by_storage", ["storageId"])
    .index("by_job_kind", ["jobId", "kind"]),

  storageCleanupRecords: defineTable({
    storageId: v.id("_storage"),
    state: storageCleanupRecordState,
    runId: v.id("storageCleanupRuns"),
    observedCreatedAt: v.number(),
    observedSizeBytes: v.number(),
    candidateAt: v.number(),
    deletedAt: v.optional(v.number()),
    checkedJobReferences: v.boolean(),
    checkedArtifacts: v.boolean(),
    checkedPendingUploads: v.boolean(),
    checkedReservations: v.boolean(),
  })
    .index("by_storage", ["storageId"])
    .index("by_run_state", ["runId", "state", "candidateAt"])
    .index("by_state", ["state", "candidateAt"]),

  storageCleanupRuns: defineTable({
    mode: storageCleanupRunMode,
    status: storageCleanupRunStatus,
    cursorIn: v.optional(v.string()),
    cursorOut: v.optional(v.string()),
    inspected: v.number(),
    eligible: v.number(),
    eligibleBytes: v.number(),
    protected: v.number(),
    candidates: v.number(),
    candidateBytes: v.number(),
    deleted: v.number(),
    bytesDeleted: v.number(),
    oldestEligibleAt: v.optional(v.number()),
    maxInspected: v.number(),
    maxDeleted: v.number(),
    maxBytesDeleted: v.number(),
    maxWallMs: v.number(),
    errorCode: v.optional(v.string()),
    startedAt: v.number(),
    completedAt: v.optional(v.number()),
  }).index("by_started", ["startedAt"]),

  storageCleanupState: defineTable({
    name: v.string(),
    sweepCursor: v.optional(v.string()),
    sweepCycle: v.number(),
    backfillCursor: v.optional(v.string()),
    backfillStatus: storageReferenceBackfillStatus,
    updatedAt: v.number(),
  }).index("by_name", ["name"]),

  usageCounters: defineTable({
    userId: v.optional(v.id("users")),
    anonId: v.optional(v.string()),
    tier,
    periodStart: v.number(),
    jobsUsed: v.number(),
    minutesUsed: v.number(),
    bytesProcessed: v.number(),
  })
    .index("by_user", ["userId", "periodStart"])
    .index("by_anon", ["anonId", "periodStart"]),

  globalUsageCounters: defineTable({
    periodStart: v.number(),
    jobsUsed: v.number(),
    minutesUsed: v.number(),
    bytesProcessed: v.number(),
  }).index("by_period", ["periodStart"]),

  planLimits: defineTable({
    tier,
    maxFilesPerJob: v.number(),
    maxMbPerFile: v.number(),
    maxConcurrentJobs: v.number(),
    maxJobsPerDay: v.number(),
    maxDailyMinutes: v.number(),
    updatedAt: v.number(),
  }).index("by_tier", ["tier"]),

  globalLimits: defineTable({
    maxConcurrentJobs: v.number(),
    maxJobsPerDay: v.number(),
    maxDailyMinutes: v.number(),
    jobMaxAttempts: v.number(),
    leaseDurationMs: v.number(),
    artifactTtlHours: v.number(),
    updatedAt: v.number(),
  }),

  budgetState: defineTable({
    month: v.string(),
    monthlyBudgetUsage: v.number(),
    heavyToolsEnabled: v.boolean(),
    status: v.string(),
    updatedAt: v.number(),
  }).index("by_month", ["month"]),

  feedback: defineTable({
    heading: v.string(),
    message: v.string(),
    status: feedbackStatus,
    createdByUserId: v.optional(v.id("users")),
    createdByAnonId: v.optional(v.string()),
    createdByClerkId: v.optional(v.string()),
    createdByEmail: v.optional(v.string()),
    createdAt: v.number(),
    updatedAt: v.number(),
    resolvedAt: v.optional(v.number()),
    resolvedByClerkId: v.optional(v.string()),
  })
    .index("by_created", ["createdAt"])
    .index("by_user_created", ["createdByUserId", "createdAt"])
    .index("by_anon_created", ["createdByAnonId", "createdAt"])
    .index("by_status_updated", ["status", "updatedAt"]),
});
