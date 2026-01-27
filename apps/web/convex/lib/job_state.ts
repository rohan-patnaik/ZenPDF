import { ConvexError } from "convex/values";

export type JobStatus =
  | "queued"
  | "running"
  | "succeeded"
  | "failed"
  | "cancelled";

const ALLOWED_TRANSITIONS: Record<JobStatus, JobStatus[]> = {
  queued: ["running", "cancelled"],
  running: ["succeeded", "failed", "cancelled", "queued", "running"],
  succeeded: [],
  failed: [],
  cancelled: [],
};

export const assertTransition = (from: JobStatus, to: JobStatus) => {
  if (!ALLOWED_TRANSITIONS[from].includes(to)) {
    throw new ConvexError({
      code: "INVALID_JOB_TRANSITION",
      message: `Job cannot move from ${from} to ${to}.`,
    });
  }
};
