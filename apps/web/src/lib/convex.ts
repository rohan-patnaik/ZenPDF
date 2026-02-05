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

export const api = {
  files: {
    generateUploadUrl: makeFunctionReference<
      "mutation",
      { anonId?: string; workerToken?: string },
      string
    >("files:generateUploadUrl"),
    getOutputDownloadUrl: makeFunctionReference<
      "query",
      { jobId: string; storageId: string; anonId?: string; allowAnonAccess?: boolean },
      string | null
    >("files:getOutputDownloadUrl"),
  },
  jobs: {
    createJob: makeFunctionReference<
      "mutation",
      {
        tool: string;
        inputs: JobInput[];
        config?: unknown;
        anonId?: string;
      },
      { jobId: string; anonId?: string }
    >("jobs:createJob"),
    listJobs: makeFunctionReference<
      "query",
      { anonId?: string; refreshKey?: number },
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
};
