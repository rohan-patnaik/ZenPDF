import type { MutationCtx, QueryCtx } from "../_generated/server";

export type PlanTier = "ANON" | "FREE_ACCOUNT";

export type PlanLimits = {
  maxFilesPerJob: number;
  maxMbPerFile: number;
  maxConcurrentJobs: number;
  maxJobsPerDay: number;
  maxDailyMinutes: number;
};

export type GlobalLimits = {
  maxConcurrentJobs: number;
  maxJobsPerDay: number;
  maxDailyMinutes: number;
  jobMaxAttempts: number;
  leaseDurationMs: number;
  artifactTtlHours: number;
};

const DEFAULT_PLAN_LIMITS: Record<PlanTier, PlanLimits> = {
  ANON: {
    maxFilesPerJob: 1,
    maxMbPerFile: 25,
    maxConcurrentJobs: 1,
    maxJobsPerDay: 5,
    maxDailyMinutes: 10,
  },
  FREE_ACCOUNT: {
    maxFilesPerJob: 3,
    maxMbPerFile: 75,
    maxConcurrentJobs: 2,
    maxJobsPerDay: 30,
    maxDailyMinutes: 60,
  },
};

const DEFAULT_GLOBAL_LIMITS: GlobalLimits = {
  maxConcurrentJobs: 3,
  maxJobsPerDay: 200,
  maxDailyMinutes: 120,
  jobMaxAttempts: 3,
  leaseDurationMs: 2 * 60 * 1000,
  artifactTtlHours: 24,
};

type Ctx = MutationCtx | QueryCtx;

const parseEnvNumber = (value: string | undefined, fallback: number) => {
  if (!value) {
    return fallback;
  }
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
};

export const resolvePlanLimits = async (ctx: Ctx, tier: PlanTier) => {
  const stored = await ctx.db
    .query("planLimits")
    .withIndex("by_tier", (q) => q.eq("tier", tier))
    .unique();

  const base = stored
    ? {
        maxFilesPerJob: stored.maxFilesPerJob,
        maxMbPerFile: stored.maxMbPerFile,
        maxConcurrentJobs: stored.maxConcurrentJobs,
        maxJobsPerDay: stored.maxJobsPerDay,
        maxDailyMinutes: stored.maxDailyMinutes,
      }
    : DEFAULT_PLAN_LIMITS[tier];

  const prefix = `ZENPDF_${tier}_`;

  return {
    maxFilesPerJob: parseEnvNumber(
      process.env[`${prefix}MAX_FILES_PER_JOB`],
      base.maxFilesPerJob,
    ),
    maxMbPerFile: parseEnvNumber(
      process.env[`${prefix}MAX_MB_PER_FILE`],
      base.maxMbPerFile,
    ),
    maxConcurrentJobs: parseEnvNumber(
      process.env[`${prefix}MAX_CONCURRENT_JOBS`],
      base.maxConcurrentJobs,
    ),
    maxJobsPerDay: parseEnvNumber(
      process.env[`${prefix}MAX_JOBS_PER_DAY`],
      base.maxJobsPerDay,
    ),
    maxDailyMinutes: parseEnvNumber(
      process.env[`${prefix}MAX_DAILY_MINUTES`],
      base.maxDailyMinutes,
    ),
  } satisfies PlanLimits;
};

export const resolveGlobalLimits = async (ctx: Ctx) => {
  const stored = await ctx.db.query("globalLimits").first();
  const base = stored
    ? {
        maxConcurrentJobs: stored.maxConcurrentJobs,
        maxJobsPerDay: stored.maxJobsPerDay,
        maxDailyMinutes: stored.maxDailyMinutes,
        jobMaxAttempts: stored.jobMaxAttempts,
        leaseDurationMs: stored.leaseDurationMs,
        artifactTtlHours: stored.artifactTtlHours,
      }
    : DEFAULT_GLOBAL_LIMITS;

  return {
    maxConcurrentJobs: parseEnvNumber(
      process.env.ZENPDF_GLOBAL_MAX_CONCURRENT_JOBS,
      base.maxConcurrentJobs,
    ),
    maxJobsPerDay: parseEnvNumber(
      process.env.ZENPDF_GLOBAL_MAX_JOBS_PER_DAY,
      base.maxJobsPerDay,
    ),
    maxDailyMinutes: parseEnvNumber(
      process.env.ZENPDF_GLOBAL_MAX_DAILY_MINUTES,
      base.maxDailyMinutes,
    ),
    jobMaxAttempts: parseEnvNumber(
      process.env.ZENPDF_GLOBAL_JOB_MAX_ATTEMPTS,
      base.jobMaxAttempts,
    ),
    leaseDurationMs: parseEnvNumber(
      process.env.ZENPDF_GLOBAL_LEASE_DURATION_MS,
      base.leaseDurationMs,
    ),
    artifactTtlHours: parseEnvNumber(
      process.env.ZENPDF_GLOBAL_ARTIFACT_TTL_HOURS,
      base.artifactTtlHours,
    ),
  } satisfies GlobalLimits;
};
