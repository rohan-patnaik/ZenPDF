import type { MutationCtx, QueryCtx } from "../_generated/server";

import { monthKey } from "./time";

export type CapacityStatus = "available" | "limited" | "at_capacity";

type Ctx = MutationCtx | QueryCtx;

export type BudgetSnapshot = {
  monthlyBudgetUsage: number;
  heavyToolsEnabled: boolean;
  status: CapacityStatus;
};

export const resolveCapacityStatus = (
  usage: number,
  heavyToolsEnabled: boolean,
): CapacityStatus => {
  if (usage >= 1) {
    return "at_capacity";
  }

  if (usage >= 0.8 || !heavyToolsEnabled) {
    return "limited";
  }

  return "available";
};

export const resolveBudgetState = async (ctx: Ctx, timestamp: number) => {
  const key = monthKey(timestamp);
  const stored = await ctx.db
    .query("budgetState")
    .withIndex("by_month", (q) => q.eq("month", key))
    .unique();

  if (!stored) {
    return {
      monthlyBudgetUsage: 0,
      heavyToolsEnabled: true,
      status: "available",
    } satisfies BudgetSnapshot;
  }

  return {
    monthlyBudgetUsage: stored.monthlyBudgetUsage,
    heavyToolsEnabled: stored.heavyToolsEnabled,
    status: resolveCapacityStatus(
      stored.monthlyBudgetUsage,
      stored.heavyToolsEnabled,
    ),
  } satisfies BudgetSnapshot;
};
