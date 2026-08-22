import { cronJobs, makeFunctionReference } from "convex/server";

const crons = cronJobs();

const cleanupArtifacts = makeFunctionReference<"mutation">(
  "cleanup:cleanupExpiredArtifacts",
);

const cleanupStorageOrphans = makeFunctionReference<"action">(
  "storage_cleanup:runStorageCleanup",
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
  cleanupStorageOrphans,
  {},
);

export default crons;
