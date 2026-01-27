import { makeFunctionReference } from "convex/server";

type JobInput = { storageId: string; filename: string; sizeBytes?: number };

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
};
