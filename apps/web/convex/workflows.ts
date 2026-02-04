import { v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import { mutation, query } from "./_generated/server";
import type { MutationCtx, QueryCtx } from "./_generated/server";

import { resolveUser } from "./lib/auth";
import { throwFriendlyError } from "./lib/errors";
import {
  compileWorkflow,
  type WorkflowStep,
  type WorkflowAssetKind,
} from "./lib/workflow_compiler";

const workflowStep = v.object({
  tool: v.string(),
  config: v.optional(v.any()),
});

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

const resolveTeamMembership = async (ctx: Ctx, teamId: Id<"teams">, userId: Id<"users">) =>
  await ctx.db
    .query("teamMembers")
    .withIndex("by_team_user", (q) => q.eq("teamId", teamId).eq("userId", userId))
    .unique();

type WorkflowListItem = {
  _id: Id<"workflows">;
  name: string;
  description?: string;
  steps: WorkflowStep[];
  teamId?: Id<"teams">;
  teamName?: string;
  ownerName?: string;
  ownerEmail?: string;
  createdAt: number;
  updatedAt: number;
  inputKind: WorkflowAssetKind;
  outputKind: WorkflowAssetKind;
  canManage: boolean;
};

export const listWorkflows = query({
  args: {},
  handler: async (ctx) => {
    const { userId, tier } = await resolveUser(ctx);
    if (!userId || tier !== "PREMIUM") {
      return [] as WorkflowListItem[];
    }

    const memberships = await ctx.db
      .query("teamMembers")
      .withIndex("by_user", (q) => q.eq("userId", userId))
      .collect();

    const teamRoles = new Map<Id<"teams">, string>();
    const teamIds = memberships.map((membership) => {
      teamRoles.set(membership.teamId, membership.role);
      return membership.teamId;
    });

    const teams = await Promise.all(teamIds.map((teamId) => ctx.db.get(teamId)));
    const teamNameById = new Map<Id<"teams">, string>();
    teams.forEach((team) => {
      if (team) {
        teamNameById.set(team._id, team.name);
      }
    });

    const personalWorkflows = await ctx.db
      .query("workflows")
      .withIndex("by_owner", (q) => q.eq("ownerId", userId))
      .filter((q) => q.eq(q.field("teamId"), undefined))
      .order("desc")
      .collect();

    const teamWorkflowsNested = await Promise.all(
      teamIds.map((teamId) =>
        ctx.db
          .query("workflows")
          .withIndex("by_team", (q) => q.eq("teamId", teamId))
          .order("desc")
          .collect(),
      ),
    );
    const teamWorkflows = teamWorkflowsNested.flat();
    const allWorkflows = [...personalWorkflows, ...teamWorkflows];

    const ownerIds = Array.from(
      new Set(allWorkflows.map((workflow) => workflow.ownerId)),
    );
    const ownerRecords = await Promise.all(ownerIds.map((ownerId) => ctx.db.get(ownerId)));
    const ownerById = new Map<Id<"users">, { name?: string; email?: string }>();
    ownerRecords.forEach((owner) => {
      if (owner) {
        ownerById.set(owner._id, { name: owner.name ?? undefined, email: owner.email ?? undefined });
      }
    });

    const buildItem = (workflow: (typeof allWorkflows)[number]): WorkflowListItem => {
      const compiled = compileWorkflow(workflow.steps);
      const owner = ownerById.get(workflow.ownerId);
      const teamName = workflow.teamId ? teamNameById.get(workflow.teamId) : undefined;
      const role = workflow.teamId ? teamRoles.get(workflow.teamId) : undefined;
      const canManage =
        !workflow.teamId || role === "owner" || workflow.ownerId === userId;

      return {
        _id: workflow._id,
        name: workflow.name,
        description: workflow.description ?? undefined,
        steps: workflow.steps,
        teamId: workflow.teamId ?? undefined,
        teamName,
        ownerName: owner?.name,
        ownerEmail: owner?.email,
        createdAt: workflow.createdAt,
        updatedAt: workflow.updatedAt,
        inputKind: compiled.inputKind,
        outputKind: compiled.outputKind,
        canManage,
      };
    };

    return allWorkflows.map(buildItem);
  },
});

export const createWorkflow = mutation({
  args: {
    name: v.string(),
    description: v.optional(v.string()),
    steps: v.array(workflowStep),
    teamId: v.optional(v.id("teams")),
  },
  handler: async (ctx, args) => {
    const userId = await requirePremiumUser(ctx);
    const name = args.name.trim();
    if (!name) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "missing_name" });
    }

    const description = args.description?.trim();
    const steps = args.steps.map((step) => ({
      tool: step.tool,
      config: step.config && Object.keys(step.config).length > 0 ? step.config : undefined,
    }));
    const compiled = compileWorkflow(steps);

    let teamId: Id<"teams"> | undefined;
    if (args.teamId) {
      const team = await ctx.db.get(args.teamId);
      if (!team) {
        throwFriendlyError("USER_INPUT_INVALID", { reason: "unknown_team" });
      }
      const membership = await resolveTeamMembership(ctx, args.teamId, userId);
      if (!membership) {
        throwFriendlyError("USER_INPUT_INVALID", { reason: "team_access" });
      }
      teamId = args.teamId;
    }

    const now = Date.now();
    const workflowId = await ctx.db.insert("workflows", {
      ownerId: userId,
      teamId,
      name,
      description: description && description.length > 0 ? description : undefined,
      steps,
      createdAt: now,
      updatedAt: now,
    });

    return {
      workflowId,
      inputKind: compiled.inputKind,
      outputKind: compiled.outputKind,
    };
  },
});

export const deleteWorkflow = mutation({
  args: { workflowId: v.id("workflows") },
  handler: async (ctx, args) => {
    const userId = await requirePremiumUser(ctx);
    const workflow = await ctx.db.get(args.workflowId);
    if (!workflow) {
      return { success: false };
    }

    if (workflow.teamId) {
      const membership = await resolveTeamMembership(ctx, workflow.teamId, userId);
      if (!membership) {
        throwFriendlyError("USER_INPUT_INVALID", { reason: "team_access" });
      }
      const membershipRecord = membership as NonNullable<typeof membership>;
      const canDelete =
        membershipRecord.role === "owner" || workflow.ownerId === userId;
      if (!canDelete) {
        throwFriendlyError("USER_INPUT_INVALID", { reason: "not_owner" });
      }
    } else if (workflow.ownerId !== userId) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "not_owner" });
    }

    await ctx.db.delete(args.workflowId);
    return { success: true };
  },
});
