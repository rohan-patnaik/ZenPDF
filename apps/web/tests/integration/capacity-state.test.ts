import { describe, expect, it } from "vitest";

import { resolveCapacityState } from "../../src/lib/capacity";

describe("resolveCapacityState", () => {
  it("returns at_capacity when budget is exceeded", () => {
    expect(
      resolveCapacityState({ monthlyBudgetUsage: 1.05, heavyToolsEnabled: true }),
    ).toBe("at_capacity");
  });

  it("returns limited when heavy tools are disabled", () => {
    expect(
      resolveCapacityState({ monthlyBudgetUsage: 0.2, heavyToolsEnabled: false }),
    ).toBe("limited");
  });

  it("returns available when budget is healthy", () => {
    expect(
      resolveCapacityState({ monthlyBudgetUsage: 0.2, heavyToolsEnabled: true }),
    ).toBe("available");
  });
});
