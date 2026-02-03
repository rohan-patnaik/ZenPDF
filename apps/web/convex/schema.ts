import { defineSchema, defineTable } from "convex/server";
import { v } from "convex/values";

const tier = v.union(
  v.literal("ANON"),
  v.literal("FREE_ACCOUNT"),
  v.literal("PREMIUM"),
);

const jobStatus = v.union(
  v.literal("queued"),
  v.literal("running"),
  v.literal("succeeded"),
  v.literal("failed"),
  v.literal("cancelled"),
);

export default defineSchema({
  users: defineTable({
    clerkUserId: v.string(),
    email: v.optional(v.string()),
    name: v.optional(v.string()),
    tier,
    adsFree: v.optional(v.boolean()),
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
    .index("by_expires", ["expiresAt"]),

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

  workflows: defineTable({
    ownerId: v.id("users"),
    teamId: v.optional(v.id("teams")),
    name: v.string(),
    description: v.optional(v.string()),
    steps: v.array(
      v.object({
        tool: v.string(),
        config: v.optional(v.any()),
      }),
    ),
    createdAt: v.number(),
    updatedAt: v.number(),
  })
    .index("by_owner", ["ownerId", "createdAt"])
    .index("by_team", ["teamId", "createdAt"]),

  teams: defineTable({
    name: v.string(),
    ownerId: v.id("users"),
    createdAt: v.number(),
  }).index("by_owner", ["ownerId"]),

  teamMembers: defineTable({
    teamId: v.id("teams"),
    userId: v.id("users"),
    role: v.string(),
    createdAt: v.number(),
  })
    .index("by_team", ["teamId"])
    .index("by_user", ["userId"])
    .index("by_team_user", ["teamId", "userId"]),
});
