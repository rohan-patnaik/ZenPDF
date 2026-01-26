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

export const isValidTransition = (from: JobStatus, to: JobStatus) =>
  ALLOWED_TRANSITIONS[from].includes(to);

export const assertTransition = (from: JobStatus, to: JobStatus) => {
  if (!isValidTransition(from, to)) {
    throw new Error(`Invalid transition from ${from} to ${to}.`);
  }
};
