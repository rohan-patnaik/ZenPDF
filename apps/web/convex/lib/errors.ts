import { ConvexError } from "convex/values";

export const FRIENDLY_ERRORS = {
  USER_LIMIT_FILE_TOO_LARGE: {
    message: "This file is larger than your plan allows.",
    next: "Compress the file or split it into smaller parts.",
  },
  USER_LIMIT_DAILY_JOBS: {
    message: "You have reached today’s job limit.",
    next: "Wait until tomorrow or run ZenPDF locally for unlimited jobs.",
  },
  USER_LIMIT_DAILY_MINUTES: {
    message: "Daily processing minutes are fully used.",
    next: "Try again tomorrow or run ZenPDF locally.",
  },
  USER_LIMIT_MAX_FILES: {
    message: "This job has more files than your plan allows.",
    next: "Reduce the file count or split into multiple jobs.",
  },
  USER_LIMIT_CONCURRENT_JOBS: {
    message: "You already have the maximum number of active jobs.",
    next: "Wait for existing jobs to finish, then retry.",
  },
  USER_LIMIT_PREMIUM_REQUIRED: {
    message: "This tool is available to Premium supporters only.",
    next: "Sign in with a Premium account or run ZenPDF locally.",
  },
  USER_SESSION_REQUIRED: {
    message: "We could not confirm your session for this job.",
    next: "Refresh the page or sign in, then retry.",
  },
  USER_LIMIT_SIZE_REQUIRED: {
    message: "We could not verify the file size for this upload.",
    next: "Try uploading the file again or use a smaller file.",
  },
  USER_INPUT_INVALID: {
    message: "Some of the provided options are invalid.",
    next: "Review the tool inputs and try again.",
  },
  SERVICE_CAPACITY_TEMPORARY: {
    message: "ZenPDF is temporarily at capacity.",
    next: "Retry in a few minutes or run the local stack.",
  },
  SERVICE_CAPACITY_MONTHLY_BUDGET: {
    message: "ZenPDF hit its monthly processing budget.",
    next: "Use the local stack or wait for the new budget cycle.",
  },
} as const;

export type FriendlyErrorCode = keyof typeof FRIENDLY_ERRORS;

export const throwFriendlyError = (
  code: FriendlyErrorCode,
  details?: Record<string, string | number>,
) => {
  const copy = FRIENDLY_ERRORS[code];
  throw new ConvexError({
    code,
    message: copy.message,
    next: copy.next,
    details,
  });
};
