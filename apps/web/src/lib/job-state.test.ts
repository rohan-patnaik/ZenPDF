import { describe, expect, it } from "vitest";

import { assertTransition, isValidTransition } from "./job-state";

describe("job state transitions", () => {
  it("allows expected transitions", () => {
    expect(isValidTransition("queued", "running")).toBe(true);
    expect(isValidTransition("running", "succeeded")).toBe(true);
    expect(isValidTransition("running", "running")).toBe(true);
  });

  it("rejects invalid transitions", () => {
    expect(isValidTransition("queued", "succeeded")).toBe(false);
    expect(() => assertTransition("failed", "running")).toThrow();
  });
});
