import type { Id } from "../_generated/dataModel";
import type { MutationCtx, QueryCtx } from "../_generated/server";
import type { PlanTier } from "./limits";

import { startOfDayUtc } from "./time";

type Ctx = MutationCtx | QueryCtx;

export const resolveUsageCounter = async (
  ctx: Ctx,
  userId: Id<"users"> | undefined,
  anonId: string | undefined,
  timestamp: number,
) => {
  const periodStart = startOfDayUtc(timestamp);
  const counter = userId
    ? await ctx.db
        .query("usageCounters")
        .withIndex("by_user", (q) =>
          q.eq("userId", userId).eq("periodStart", periodStart),
        )
        .unique()
    : anonId
      ? await ctx.db
          .query("usageCounters")
          .withIndex("by_anon", (q) =>
            q.eq("anonId", anonId).eq("periodStart", periodStart),
          )
          .unique()
      : null;

  return { counter, periodStart };
};

export const incrementUsage = async (
  ctx: MutationCtx,
  userId: Id<"users"> | undefined,
  anonId: string | undefined,
  tier: PlanTier,
  timestamp: number,
  updates: { jobs?: number; minutes?: number; bytes?: number },
) => {
  const { counter, periodStart } = await resolveUsageCounter(
    ctx,
    userId,
    anonId,
    timestamp,
  );

  const jobsDelta = updates.jobs ?? 0;
  const minutesDelta = updates.minutes ?? 0;
  const bytesDelta = updates.bytes ?? 0;

  if (counter) {
    await ctx.db.patch(counter._id, {
      jobsUsed: counter.jobsUsed + jobsDelta,
      minutesUsed: counter.minutesUsed + minutesDelta,
      bytesProcessed: counter.bytesProcessed + bytesDelta,
    });
  } else {
    await ctx.db.insert("usageCounters", {
      userId,
      anonId,
      tier,
      periodStart,
      jobsUsed: jobsDelta,
      minutesUsed: minutesDelta,
      bytesProcessed: bytesDelta,
    });
  }
};

export const resolveGlobalUsageCounter = async (ctx: Ctx, timestamp: number) => {
  const periodStart = startOfDayUtc(timestamp);
  const counter = await ctx.db
    .query("globalUsageCounters")
    .withIndex("by_period", (q) => q.eq("periodStart", periodStart))
    .unique();

  return { counter, periodStart };
};

export const incrementGlobalUsage = async (
  ctx: MutationCtx,
  timestamp: number,
  updates: { jobs?: number; minutes?: number; bytes?: number },
) => {
  const { counter, periodStart } = await resolveGlobalUsageCounter(ctx, timestamp);

  const jobsDelta = updates.jobs ?? 0;
  const minutesDelta = updates.minutes ?? 0;
  const bytesDelta = updates.bytes ?? 0;

  if (counter) {
    await ctx.db.patch(counter._id, {
      jobsUsed: counter.jobsUsed + jobsDelta,
      minutesUsed: counter.minutesUsed + minutesDelta,
      bytesProcessed: counter.bytesProcessed + bytesDelta,
    });
  } else {
    await ctx.db.insert("globalUsageCounters", {
      periodStart,
      jobsUsed: jobsDelta,
      minutesUsed: minutesDelta,
      bytesProcessed: bytesDelta,
    });
  }
};
