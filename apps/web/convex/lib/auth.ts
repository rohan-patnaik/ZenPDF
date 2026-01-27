import type { UserIdentity } from "convex/server";

import type { Id } from "../_generated/dataModel";
import type { MutationCtx, QueryCtx } from "../_generated/server";

import { normalizeOptionalEmail } from "./email";

type Ctx = MutationCtx | QueryCtx;

export type ResolvedUser = {
  identity: UserIdentity | null;
  userId: Id<"users"> | undefined;
  tier: "ANON" | "FREE_ACCOUNT" | "PREMIUM";
  adsFree: boolean;
};

const parseEnvList = (value: string | undefined) =>
  value
    ?.split(",")
    .map((entry) => entry.trim())
    .filter(Boolean) ?? [];

const resolveTier = (identity: UserIdentity | null, storedTier?: ResolvedUser["tier"]) => {
  if (!identity) {
    return { tier: "ANON" as const, adsFree: false };
  }

  const premiumEmails = parseEnvList(process.env.ZENPDF_PREMIUM_EMAILS).map((email) =>
    email.toLowerCase(),
  );
  const premiumClerkIds = parseEnvList(process.env.ZENPDF_PREMIUM_CLERK_IDS);
  const hasAllowlist = premiumEmails.length > 0 || premiumClerkIds.length > 0;
  const email = normalizeOptionalEmail(identity.email);
  const isPremium =
    (email ? premiumEmails.includes(email) : false) ||
    premiumClerkIds.includes(identity.subject);

  if (isPremium) {
    return { tier: "PREMIUM" as const, adsFree: true };
  }

  if (hasAllowlist) {
    return { tier: "FREE_ACCOUNT" as const, adsFree: false };
  }

  const tier = storedTier ?? "FREE_ACCOUNT";
  return { tier, adsFree: tier === "PREMIUM" };
};

export const resolveUser = async (ctx: Ctx): Promise<ResolvedUser> => {
  const identity = await ctx.auth.getUserIdentity();

  if (!identity) {
    return { identity: null, userId: undefined, tier: "ANON", adsFree: false };
  }

  const clerkUserId = identity.subject;
  const existing = await ctx.db
    .query("users")
    .withIndex("by_clerk_id", (q) => q.eq("clerkUserId", clerkUserId))
    .unique();

  const { tier, adsFree } = resolveTier(identity, existing?.tier);

  if (existing) {
    return { identity, userId: existing._id, tier, adsFree };
  }

  return { identity, userId: undefined, tier, adsFree };
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
      adsFree: resolved.adsFree,
      createdAt: now,
      updatedAt: now,
    });

    return { ...resolved, userId };
  }

  const existing = await ctx.db.get(resolved.userId);
  const storedAdsFree = existing?.adsFree ?? false;
  if (existing && (existing.tier !== resolved.tier || storedAdsFree !== resolved.adsFree)) {
    await ctx.db.patch(resolved.userId, {
      tier: resolved.tier,
      adsFree: resolved.adsFree,
      updatedAt: now,
    });
  }

  return resolved;
};
