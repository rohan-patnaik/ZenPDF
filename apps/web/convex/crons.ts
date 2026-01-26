import { cronJobs, makeFunctionReference } from "convex/server";

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

export default crons;
