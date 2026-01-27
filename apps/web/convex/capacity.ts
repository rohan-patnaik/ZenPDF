import { queryGeneric as query } from "convex/server";
import { v } from "convex/values";

import { resolveUser } from "./lib/auth";
import { resolveBudgetState } from "./lib/budget";
import { resolveGlobalLimits, resolvePlanLimits } from "./lib/limits";
import { resolveGlobalUsageCounter, resolveUsageCounter } from "./lib/usage";

export const getCapacitySnapshot = query({
  args: {},
  handler: async (ctx) => {
    const now = Date.now();
    const budget = await resolveBudgetState(ctx, now);
    const limits = await resolveGlobalLimits(ctx);
    const { counter, periodStart } = await resolveGlobalUsageCounter(ctx, now);

    return {
      budget,
      limits,
      usage: counter ?? {
        periodStart,
        jobsUsed: 0,
        minutesUsed: 0,
        bytesProcessed: 0,
      },
    };
  },
});

export const getUsageSnapshot = query({
  args: { anonId: v.optional(v.string()) },
  handler: async (ctx, args) => {
    const now = Date.now();
    const { userId, tier } = await resolveUser(ctx);
    const anonId = args.anonId?.trim() || undefined;
    const resolvedAnonId = userId ? undefined : anonId;
    const [planLimits, anonLimits, freeLimits, premiumLimits, budget, usageResult] =
      await Promise.all([
        resolvePlanLimits(ctx, tier),
        resolvePlanLimits(ctx, "ANON"),
        resolvePlanLimits(ctx, "FREE_ACCOUNT"),
        resolvePlanLimits(ctx, "PREMIUM"),
        resolveBudgetState(ctx, now),
        resolveUsageCounter(ctx, userId, resolvedAnonId, now),
      ]);
    const planSnapshot = {
      ANON: anonLimits,
      FREE_ACCOUNT: freeLimits,
      PREMIUM: premiumLimits,
    };
    const { counter, periodStart } = usageResult;

    return {
      tier,
      planLimits,
      plans: planSnapshot,
      budget,
      usage: counter ?? {
        periodStart,
        jobsUsed: 0,
        minutesUsed: 0,
        bytesProcessed: 0,
      },
    };
  },
});
