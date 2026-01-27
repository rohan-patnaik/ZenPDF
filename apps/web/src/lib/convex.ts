import { makeFunctionReference } from "convex/server";

import type { BudgetSnapshot } from "../../convex/lib/budget";
import type { PlanLimits, PlanTier } from "./limits";

type JobInput = { storageId: string; filename: string; sizeBytes?: number };

type UsageCounter = {
  periodStart: number;
  jobsUsed: number;
  minutesUsed: number;
  bytesProcessed: number;
};

type UsageSnapshot = {
  tier: PlanTier;
  planLimits: PlanLimits;
  plans: Record<PlanTier, PlanLimits>;
  usage: UsageCounter;
  budget: BudgetSnapshot;
};

type ViewerSnapshot = {
  tier: PlanTier;
  adsFree: boolean;
  signedIn: boolean;
};

type WorkflowStep = {
  tool: string;
  config?: Record<string, unknown>;
};

type WorkflowSummary = {
  _id: string;
  name: string;
  description?: string;
  steps: WorkflowStep[];
  teamId?: string;
  teamName?: string;
  ownerName?: string;
  ownerEmail?: string;
  createdAt: number;
  updatedAt: number;
  inputKind: string;
  outputKind: string;
  canManage: boolean;
};

type TeamMemberSummary = {
  _id: string;
  userId: string;
  role: string;
  name?: string;
  email?: string;
};

type TeamSummary = {
  _id: string;
  name: string;
  ownerId: string;
  createdAt: number;
  isOwner: boolean;
  members: TeamMemberSummary[];
};

export const api = {
  files: {
    generateUploadUrl: makeFunctionReference<
      "mutation",
      { anonId?: string; workerToken?: string },
      string
    >("files:generateUploadUrl"),
    getOutputDownloadUrl: makeFunctionReference<
      "query",
      { jobId: string; storageId: string; anonId?: string },
      string | null
    >("files:getOutputDownloadUrl"),
  },
  jobs: {
    createJob: makeFunctionReference<
      "mutation",
      { tool: string; inputs: JobInput[]; config?: unknown; anonId?: string },
      { jobId: string; anonId?: string }
    >("jobs:createJob"),
    listJobs: makeFunctionReference<
      "query",
      { anonId?: string },
      unknown[]
    >("jobs:listJobs"),
  },
  capacity: {
    getUsageSnapshot: makeFunctionReference<
      "query",
      { anonId?: string },
      UsageSnapshot
    >("capacity:getUsageSnapshot"),
  },
  users: {
    getViewer: makeFunctionReference<"query", Record<string, never>, ViewerSnapshot>(
      "users:getViewer",
    ),
  },
  workflows: {
    listWorkflows: makeFunctionReference<"query", Record<string, never>, WorkflowSummary[]>(
      "workflows:listWorkflows",
    ),
    createWorkflow: makeFunctionReference<
      "mutation",
      { name: string; description?: string; steps: WorkflowStep[]; teamId?: string },
      { workflowId: string; inputKind: string; outputKind: string }
    >("workflows:createWorkflow"),
    deleteWorkflow: makeFunctionReference<
      "mutation",
      { workflowId: string },
      { success: boolean }
    >("workflows:deleteWorkflow"),
  },
  teams: {
    listTeams: makeFunctionReference<"query", Record<string, never>, TeamSummary[]>(
      "teams:listTeams",
    ),
    createTeam: makeFunctionReference<"mutation", { name: string }, { teamId: string }>(
      "teams:createTeam",
    ),
    addTeamMember: makeFunctionReference<
      "mutation",
      { teamId: string; email: string },
      { memberId: string; alreadyMember: boolean }
    >("teams:addTeamMember"),
    removeTeamMember: makeFunctionReference<
      "mutation",
      { teamId: string; memberId: string },
      { success: boolean }
    >("teams:removeTeamMember"),
  },
};
