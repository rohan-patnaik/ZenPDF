import type { UserIdentity } from "convex/server";

import type { Id } from "../_generated/dataModel";
import type { MutationCtx, QueryCtx } from "../_generated/server";

import { normalizeOptionalEmail } from "./email";

type Ctx = MutationCtx | QueryCtx;

export type ResolvedUser = {
  identity: UserIdentity | null;
  userId: Id<"users"> | undefined;
  tier: "ANON" | "FREE_ACCOUNT";
};

const resolveTier = (identity: UserIdentity | null, storedTier?: ResolvedUser["tier"]) => {
  if (!identity) {
    return { tier: "ANON" as const };
  }
  const tier = storedTier ?? "FREE_ACCOUNT";
  return { tier };
};

export const resolveUser = async (ctx: Ctx): Promise<ResolvedUser> => {
  const identity = await ctx.auth.getUserIdentity();

  if (!identity) {
    return { identity: null, userId: undefined, tier: "ANON" };
  }

  const clerkUserId = identity.subject;
  const existing = await ctx.db
    .query("users")
    .withIndex("by_clerk_id", (q) => q.eq("clerkUserId", clerkUserId))
    .unique();

  const { tier } = resolveTier(identity, existing?.tier);

  if (existing) {
    return { identity, userId: existing._id, tier };
  }

  return { identity, userId: undefined, tier };
};

export const resolveOrCreateUser = async (
  ctx: MutationCtx,
): Promise<ResolvedUser> => {
  const resolved = await resolveUser(ctx);
  if (!resolved.identity) {
    return resolved;
  }

  const now = Date.now();
  if (!resolved.userId) {
    const userId = await ctx.db.insert("users", {
      clerkUserId: resolved.identity.subject,
      email: normalizeOptionalEmail(resolved.identity.email),
      name: resolved.identity.name ?? resolved.identity.nickname,
      tier: resolved.tier,
      createdAt: now,
      updatedAt: now,
    });

    return { ...resolved, userId };
  }

  const existing = await ctx.db.get(resolved.userId);
  if (existing && existing.tier !== resolved.tier) {
    await ctx.db.patch(resolved.userId, {
      tier: resolved.tier,
      updatedAt: now,
    });
  }

  return resolved;
};
