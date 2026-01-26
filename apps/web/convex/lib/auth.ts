import type {
  GenericDataModel,
  GenericMutationCtx,
  GenericQueryCtx,
  UserIdentity,
} from "convex/server";

type Ctx =
  | GenericMutationCtx<GenericDataModel>
  | GenericQueryCtx<GenericDataModel>;

export type ResolvedUser = {
  identity: UserIdentity | null;
  userId: string | undefined;
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

  const now = Date.now();
  const userId = await ctx.db.insert("users", {
    clerkUserId,
    email: identity.email,
    name: identity.name ?? identity.nickname,
    tier: "FREE_ACCOUNT",
    createdAt: now,
    updatedAt: now,
  });

  return { identity, userId, tier: "FREE_ACCOUNT" };
};
