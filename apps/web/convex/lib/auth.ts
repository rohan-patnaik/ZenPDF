import type {
  GenericDataModel,
  GenericMutationCtx,
  GenericQueryCtx,
  UserIdentity,
} from "convex/server";
import type { Id } from "convex/values";

type MutationCtx = GenericMutationCtx<GenericDataModel>;
type QueryCtx = GenericQueryCtx<GenericDataModel>;
type Ctx = MutationCtx | QueryCtx;

export type ResolvedUser = {
  identity: UserIdentity | null;
  userId: Id<"users"> | undefined;
  tier: "ANON" | "FREE_ACCOUNT" | "PREMIUM";
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

  if (existing) {
    return { identity, userId: existing._id, tier: existing.tier };
  }

  return { identity, userId: undefined, tier: "FREE_ACCOUNT" };
};

export const resolveOrCreateUser = async (
  ctx: MutationCtx,
): Promise<ResolvedUser> => {
  const resolved = await resolveUser(ctx);
  if (!resolved.identity || resolved.userId) {
    return resolved;
  }

  const now = Date.now();
  const userId = await ctx.db.insert("users", {
    clerkUserId: resolved.identity.subject,
    email: resolved.identity.email,
    name: resolved.identity.name ?? resolved.identity.nickname,
    tier: "FREE_ACCOUNT",
    createdAt: now,
    updatedAt: now,
  });

  return { ...resolved, userId, tier: "FREE_ACCOUNT" };
};
