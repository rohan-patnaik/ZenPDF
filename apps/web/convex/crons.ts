import { cronJobs, makeFunctionReference } from "convex/server";

import { internal } from "./_generated/api";

const crons = cronJobs();

const cleanupArtifacts = makeFunctionReference<"mutation">(
  "cleanup:cleanupExpiredArtifacts",
);

crons.interval(
  "cleanup expired artifacts",
  { hours: 1 },
  cleanupArtifacts,
  { batchSize: 200 },
);

crons.interval(
  "bounded storage orphan sweep",
  { hours: 1 },
  internal.storage_cleanup.runStorageCleanup,
  {},
);

export default crons;
