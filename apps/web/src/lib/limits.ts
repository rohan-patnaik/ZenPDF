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

export const DEFAULT_LIMITS: Record<PlanTier, PlanLimits> = {
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
