import { queryGeneric as query } from "convex/server";

import { resolveBudgetState } from "./lib/budget";
import { resolveGlobalLimits } from "./lib/limits";
import { resolveGlobalUsageCounter } from "./lib/usage";

export const getCapacitySnapshot = query({
  args: {},
  handler: async (ctx) => {
    const now = Date.now();
    const budget = await resolveBudgetState(ctx, now);
    const limits = await resolveGlobalLimits(ctx);
    const { counter } = await resolveGlobalUsageCounter(ctx, now);

    return {
      budget,
      limits,
      usage: counter ?? {
        periodStart: now,
        jobsUsed: 0,
        minutesUsed: 0,
        bytesProcessed: 0,
      },
    };
  },
});
