export type PlanTier = "ANON" | "FREE_ACCOUNT" | "PREMIUM";

export type PlanLimits = {
  maxFilesPerJob: number;
  maxMbPerFile: number;
  maxConcurrentJobs: number;
  maxJobsPerDay: number;
  maxDailyMinutes: number;
};

export const DEFAULT_LIMITS: Record<PlanTier, PlanLimits> = {
  ANON: {
    maxFilesPerJob: 1,
    maxMbPerFile: 10,
    maxConcurrentJobs: 1,
    maxJobsPerDay: 3,
    maxDailyMinutes: 10,
  },
  FREE_ACCOUNT: {
    maxFilesPerJob: 3,
    maxMbPerFile: 50,
    maxConcurrentJobs: 2,
    maxJobsPerDay: 25,
    maxDailyMinutes: 60,
  },
  PREMIUM: {
    maxFilesPerJob: 10,
    maxMbPerFile: 250,
    maxConcurrentJobs: 4,
    maxJobsPerDay: 200,
    maxDailyMinutes: 240,
  },
};
