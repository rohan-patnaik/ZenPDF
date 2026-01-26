import type { FriendlyErrorCode } from "./errors";
import type { PlanLimits, GlobalLimits } from "./limits";

type JobInput = { sizeBytes?: number };

type UsageSnapshot = {
  jobsUsed: number;
  minutesUsed: number;
};

type LimitCheckResult =
  | { ok: true }
  | { ok: false; code: FriendlyErrorCode; details?: Record<string, number> };

export const checkPlanLimits = (
  inputs: JobInput[],
  limits: PlanLimits,
  usage: UsageSnapshot,
  activeJobs: number,
): LimitCheckResult => {
  if (inputs.length > limits.maxFilesPerJob) {
    return { ok: false, code: "USER_LIMIT_MAX_FILES" };
  }

  for (const input of inputs) {
    const sizeMb = (input.sizeBytes ?? 0) / (1024 * 1024);
    if (sizeMb > limits.maxMbPerFile) {
      return {
        ok: false,
        code: "USER_LIMIT_FILE_TOO_LARGE",
        details: { limitMb: limits.maxMbPerFile },
      };
    }
  }

  if (usage.jobsUsed >= limits.maxJobsPerDay) {
    return { ok: false, code: "USER_LIMIT_DAILY_JOBS" };
  }

  if (usage.minutesUsed >= limits.maxDailyMinutes) {
    return { ok: false, code: "USER_LIMIT_DAILY_MINUTES" };
  }

  if (activeJobs >= limits.maxConcurrentJobs) {
    return { ok: false, code: "USER_LIMIT_CONCURRENT_JOBS" };
  }

  return { ok: true };
};

export const checkGlobalLimits = (
  limits: GlobalLimits,
  usage: UsageSnapshot,
  activeJobs: number,
): LimitCheckResult => {
  if (usage.jobsUsed >= limits.maxJobsPerDay) {
    return { ok: false, code: "SERVICE_CAPACITY_TEMPORARY" };
  }

  if (usage.minutesUsed >= limits.maxDailyMinutes) {
    return { ok: false, code: "SERVICE_CAPACITY_TEMPORARY" };
  }

  if (activeJobs >= limits.maxConcurrentJobs) {
    return { ok: false, code: "SERVICE_CAPACITY_TEMPORARY" };
  }

  return { ok: true };
};
