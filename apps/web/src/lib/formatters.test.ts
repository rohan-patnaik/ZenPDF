import { describe, expect, it } from "vitest";

import { formatBytes, formatPercent } from "./formatters";

describe("formatBytes", () => {
  it("handles zero or invalid values", () => {
    expect(formatBytes(0)).toBe("0 B");
    expect(formatBytes(-5)).toBe("0 B");
  });

  it("formats values with appropriate units", () => {
    expect(formatBytes(900)).toBe("900 B");
    expect(formatBytes(1024)).toBe("1 KB");
    expect(formatBytes(5 * 1024 * 1024)).toBe("5.0 MB");
  });
});

describe("formatPercent", () => {
  it("caps percentages between 0 and 100", () => {
    expect(formatPercent(20, 200)).toBe("10%");
    expect(formatPercent(400, 200)).toBe("100%");
    expect(formatPercent(-5, 200)).toBe("0%");
  });

  it("handles invalid totals", () => {
    expect(formatPercent(10, 0)).toBe("0%");
  });
});
