import { ConvexError } from "convex/values";

export const assertWorkerToken = (token?: string) => {
  const expected = process.env.ZENPDF_WORKER_TOKEN;
  if (!expected || token !== expected) {
    throw new ConvexError({
      code: "UNAUTHORIZED",
      message: "Worker token required.",
    });
  }
};
