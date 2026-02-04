import { v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import { mutation, query } from "./_generated/server";
import type { MutationCtx, QueryCtx } from "./_generated/server";

import { resolveOrCreateUser, resolveUser } from "./lib/auth";
import { normalizeEmail } from "./lib/email";
import { throwFriendlyError } from "./lib/errors";

type Ctx = QueryCtx | MutationCtx;

const requirePremiumUser = async (ctx: Ctx): Promise<Id<"users">> => {
  const { userId, tier } = await resolveUser(ctx);
  if (!userId) {
    throwFriendlyError("USER_SESSION_REQUIRED");
  }
  if (tier !== "PREMIUM") {
    throwFriendlyError("USER_LIMIT_PREMIUM_REQUIRED");
  }
  return userId as Id<"users">;
};

const requirePremiumUserForMutation = async (
  ctx: MutationCtx,
): Promise<Id<"users">> => {
  const { userId, tier } = await resolveOrCreateUser(ctx);
  if (!userId) {
    throwFriendlyError("USER_SESSION_REQUIRED");
  }
  if (tier !== "PREMIUM") {
    throwFriendlyError("USER_LIMIT_PREMIUM_REQUIRED");
  }
  return userId as Id<"users">;
};

const resolveTeamMembership = async (ctx: Ctx, teamId: Id<"teams">, userId: Id<"users">) =>
  await ctx.db
    .query("teamMembers")
    .withIndex("by_team_user", (q) => q.eq("teamId", teamId).eq("userId", userId))
    .unique();

const requireTeamOwner = async (ctx: Ctx, teamId: Id<"teams">, userId: Id<"users">) => {
  const membership = await resolveTeamMembership(ctx, teamId, userId);
  if (!membership || membership.role !== "owner") {
    throwFriendlyError("USER_INPUT_INVALID", { reason: "team_access" });
  }
  return membership;
};

type TeamMemberSummary = {
  _id: Id<"teamMembers">;
  userId: Id<"users">;
  role: string;
  name?: string;
  email?: string;
};

type TeamSummary = {
  _id: Id<"teams">;
  name: string;
  ownerId: Id<"users">;
  createdAt: number;
  isOwner: boolean;
  members: TeamMemberSummary[];
};

export const listTeams = query({
  args: {},
  handler: async (ctx) => {
    const { userId, tier } = await resolveUser(ctx);
    if (!userId || tier !== "PREMIUM") {
      return [] as TeamSummary[];
    }
    const memberships = await ctx.db
      .query("teamMembers")
      .withIndex("by_user", (q) => q.eq("userId", userId))
      .collect();

    if (memberships.length === 0) {
      return [] as TeamSummary[];
    }

    const teamIds = memberships.map((membership) => membership.teamId);
    const membershipByTeam = new Map<Id<"teams">, string>();
    memberships.forEach((membership) => {
      membershipByTeam.set(membership.teamId, membership.role);
    });

    const teams = await Promise.all(teamIds.map((teamId) => ctx.db.get(teamId)));
    const membersByTeam = new Map<Id<"teams">, (typeof memberships)[number][]>();

    const teamMembersLists = await Promise.all(
      teamIds.map((teamId) =>
        ctx.db
          .query("teamMembers")
          .withIndex("by_team", (q) => q.eq("teamId", teamId))
          .collect(),
      ),
    );

    teamMembersLists.forEach((members, index) => {
      membersByTeam.set(teamIds[index], members);
    });

    const memberUserIds = Array.from(
      new Set(teamMembersLists.flat().map((member) => member.userId)),
    );
    const memberUsers = await Promise.all(memberUserIds.map((id) => ctx.db.get(id)));
    const userById = new Map<Id<"users">, { name?: string; email?: string }>();
    memberUsers.forEach((user) => {
      if (user) {
        userById.set(user._id, { name: user.name ?? undefined, email: user.email ?? undefined });
      }
    });

    return teams
      .filter((team): team is NonNullable<typeof team> => Boolean(team))
      .map((team) => {
        const members = membersByTeam.get(team._id) ?? [];
        const memberSummaries = members.map((member) => {
          const details = userById.get(member.userId);
          return {
            _id: member._id,
            userId: member.userId,
            role: member.role,
            name: details?.name,
            email: details?.email,
          };
        });

        return {
          _id: team._id,
          name: team.name,
          ownerId: team.ownerId,
          createdAt: team.createdAt,
          isOwner: membershipByTeam.get(team._id) === "owner",
          members: memberSummaries,
        } as TeamSummary;
      });
  },
});

export const createTeam = mutation({
  args: { name: v.string() },
  handler: async (ctx, args) => {
    const userId = await requirePremiumUserForMutation(ctx);
    const name = args.name.trim();
    if (!name) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "missing_name" });
    }

    const now = Date.now();
    const teamId = await ctx.db.insert("teams", {
      name,
      ownerId: userId,
      createdAt: now,
    });
    await ctx.db.insert("teamMembers", {
      teamId,
      userId,
      role: "owner",
      createdAt: now,
    });

    return { teamId };
  },
});

export const addTeamMember = mutation({
  args: { teamId: v.id("teams"), email: v.string() },
  handler: async (ctx, args) => {
    const userId = await requirePremiumUserForMutation(ctx);
    await requireTeamOwner(ctx, args.teamId, userId);
    const email = normalizeEmail(args.email);
    if (!email) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "missing_email" });
    }

    const user = await ctx.db
      .query("users")
      .withIndex("by_email", (q) => q.eq("email", email))
      .unique();
    if (!user) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "unknown_user" });
    }

    const userRecord = user as NonNullable<typeof user>;
    const existing = await resolveTeamMembership(ctx, args.teamId, userRecord._id);
    if (existing) {
      return { memberId: existing._id, alreadyMember: true };
    }

    const now = Date.now();
    const memberId = await ctx.db.insert("teamMembers", {
      teamId: args.teamId,
      userId: userRecord._id,
      role: "member",
      createdAt: now,
    });

    return { memberId, alreadyMember: false };
  },
});

export const removeTeamMember = mutation({
  args: { teamId: v.id("teams"), memberId: v.id("teamMembers") },
  handler: async (ctx, args) => {
    const userId = await requirePremiumUserForMutation(ctx);
    await requireTeamOwner(ctx, args.teamId, userId);
    const member = await ctx.db.get(args.memberId);
    if (!member || member.teamId !== args.teamId) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "member_not_found" });
    }
    const memberRecord = member as NonNullable<typeof member>;
    if (memberRecord.role === "owner") {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "owner_remove" });
    }

    await ctx.db.delete(args.memberId);
    return { success: true };
  },
});
