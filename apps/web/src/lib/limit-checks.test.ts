import { describe, expect, it } from "vitest";

import {
  checkGlobalLimits,
  checkPlanLimits,
} from "../../convex/lib/limit_checks";

describe("limit checks", () => {
  it("flags files that exceed plan limits", () => {
    const result = checkPlanLimits(
      [{ sizeBytes: 12 * 1024 * 1024 }],
      {
        maxFilesPerJob: 2,
        maxMbPerFile: 10,
        maxConcurrentJobs: 2,
        maxJobsPerDay: 5,
        maxDailyMinutes: 60,
      },
      { jobsUsed: 0, minutesUsed: 0 },
      0,
    );

    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.code).toBe("USER_LIMIT_FILE_TOO_LARGE");
    }
  });

  it("flags files with missing size metadata", () => {
    const result = checkPlanLimits(
      [{}],
      {
        maxFilesPerJob: 2,
        maxMbPerFile: 10,
        maxConcurrentJobs: 2,
        maxJobsPerDay: 5,
        maxDailyMinutes: 60,
      },
      { jobsUsed: 0, minutesUsed: 0 },
      0,
    );

    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.code).toBe("USER_LIMIT_SIZE_REQUIRED");
    }
  });

  it("flags global capacity when concurrency is full", () => {
    const result = checkGlobalLimits(
      {
        maxConcurrentJobs: 2,
        maxJobsPerDay: 100,
        maxDailyMinutes: 1000,
        jobMaxAttempts: 3,
        leaseDurationMs: 1000,
        artifactTtlHours: 24,
      },
      { jobsUsed: 0, minutesUsed: 0 },
      2,
    );

    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.code).toBe("SERVICE_CAPACITY_TEMPORARY");
    }
  });
});
