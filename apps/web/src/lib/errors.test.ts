import { describe, expect, it } from "vitest";

import { FRIENDLY_ERRORS } from "./errors";

describe("friendly error catalog", () => {
  it("includes required error codes", () => {
    const required = [
      "USER_LIMIT_FILE_TOO_LARGE",
      "USER_LIMIT_DAILY_JOBS",
      "USER_LIMIT_DAILY_MINUTES",
      "USER_LIMIT_MAX_FILES",
      "USER_LIMIT_CONCURRENT_JOBS",
      "USER_SESSION_REQUIRED",
      "USER_LIMIT_SIZE_REQUIRED",
      "SERVICE_CAPACITY_TEMPORARY",
      "SERVICE_CAPACITY_MONTHLY_BUDGET",
    ];

    for (const code of required) {
      expect(FRIENDLY_ERRORS[code as keyof typeof FRIENDLY_ERRORS]).toBeDefined();
    }
  });

  it("provides next steps for each code", () => {
    for (const error of Object.values(FRIENDLY_ERRORS)) {
      expect(error.message.length).toBeGreaterThan(0);
      expect(error.next.length).toBeGreaterThan(0);
    }
  });
});
