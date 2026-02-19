import { ConvexError, v } from "convex/values";

import type { Id } from "./_generated/dataModel";
import { mutation, query } from "./_generated/server";
import type { MutationCtx, QueryCtx } from "./_generated/server";
import { resolveOrCreateUser } from "./lib/auth";

const HEADING_MIN_CHARS = 4;
const HEADING_MAX_CHARS = 120;
const MESSAGE_MIN_CHARS = 12;
const MESSAGE_MAX_CHARS = 2000;

const RATE_LIMIT_MIN_INTERVAL_MS = 60_000;
const RATE_LIMIT_WINDOW_MS = 60 * 60 * 1000;
const RATE_LIMIT_MAX_PER_WINDOW = 5;

const MAX_FEEDBACK_LIST = 200;
const DEFAULT_FEEDBACK_LIST = 100;

type Ctx = QueryCtx | MutationCtx;

const normalizeWhitespace = (value: string) => value.replace(/\s+/g, " ").trim();

const parseCsvSet = (
  value: string | undefined,
  normalize?: (item: string) => string,
) => {
  const set = new Set<string>();
  for (const part of (value ?? "").split(",")) {
    const trimmed = part.trim();
    if (!trimmed) {
      continue;
    }
    set.add(normalize ? normalize(trimmed) : trimmed);
  }
  return set;
};

const resolveAdminSets = () => {
  const adminClerkIds = parseCsvSet(process.env.ZENPDF_FEEDBACK_ADMIN_CLERK_IDS);
  const adminEmails = parseCsvSet(
    process.env.ZENPDF_FEEDBACK_ADMIN_EMAILS,
    (value) => value.toLowerCase(),
  );
  return { adminClerkIds, adminEmails };
};

const isIdentityAdmin = (
  identity: { subject: string; email?: string | null },
) => {
  const { adminClerkIds, adminEmails } = resolveAdminSets();

  if (adminClerkIds.size === 0 && adminEmails.size === 0) {
    return process.env.NODE_ENV !== "production";
  }

  if (adminClerkIds.has(identity.subject)) {
    return true;
  }

  if (identity.email && adminEmails.has(identity.email.toLowerCase())) {
    return true;
  }

  return false;
};

const isFeedbackAdmin = async (ctx: Ctx) => {
  const identity = await ctx.auth.getUserIdentity();
  if (!identity) {
    return false;
  }
  return isIdentityAdmin({
    subject: identity.subject,
    email: identity.email,
  });
};

const assertFeedbackAdmin = async (ctx: MutationCtx) => {
  const identity = await ctx.auth.getUserIdentity();
  if (!identity) {
    throw new ConvexError({
      code: "USER_SESSION_REQUIRED",
      message: "Sign in is required to manage feedback state.",
    });
  }

  if (
    !isIdentityAdmin({
      subject: identity.subject,
      email: identity.email,
    })
  ) {
    throw new ConvexError({
      code: "FORBIDDEN",
      message: "Only the ZenPDF owner can resolve feedback items.",
    });
  }

  return identity;
};

const validateSubmission = (heading: string, message: string) => {
  const safeHeading = normalizeWhitespace(heading);
  const safeMessage = message.trim();

  if (
    safeHeading.length < HEADING_MIN_CHARS ||
    safeHeading.length > HEADING_MAX_CHARS
  ) {
    throw new ConvexError({
      code: "USER_INPUT_INVALID",
      message: `Heading must be ${HEADING_MIN_CHARS}-${HEADING_MAX_CHARS} characters.`,
    });
  }

  if (
    safeMessage.length < MESSAGE_MIN_CHARS ||
    safeMessage.length > MESSAGE_MAX_CHARS
  ) {
    throw new ConvexError({
      code: "USER_INPUT_INVALID",
      message: `Message must be ${MESSAGE_MIN_CHARS}-${MESSAGE_MAX_CHARS} characters.`,
    });
  }

  return { safeHeading, safeMessage };
};

const normalizeAnonId = (anonId: string | undefined) => {
  const value = anonId?.trim();
  if (!value) {
    return undefined;
  }
  if (value.length < 8 || value.length > 128) {
    throw new ConvexError({
      code: "USER_INPUT_INVALID",
      message: "Feedback session id is invalid. Refresh and try again.",
    });
  }
  return value;
};

const enforceRateLimit = async (
  ctx: MutationCtx,
  options: { userId?: Id<"users">; anonId?: string },
  now: number,
) => {
  const windowStart = now - RATE_LIMIT_WINDOW_MS;
  let recent: Array<{ createdAt: number }> = [];

  if (options.userId) {
    recent = await ctx.db
      .query("feedback")
      .withIndex("by_user_created", (q) =>
        q.eq("createdByUserId", options.userId),
      )
      .order("desc")
      .take(RATE_LIMIT_MAX_PER_WINDOW + 2);
  } else if (options.anonId) {
    recent = await ctx.db
      .query("feedback")
      .withIndex("by_anon_created", (q) =>
        q.eq("createdByAnonId", options.anonId),
      )
      .order("desc")
      .take(RATE_LIMIT_MAX_PER_WINDOW + 2);
  }

  if (recent.length === 0) {
    return;
  }

  if (now - recent[0].createdAt < RATE_LIMIT_MIN_INTERVAL_MS) {
    throw new ConvexError({
      code: "RATE_LIMIT_EXCEEDED",
      message: "Please wait about a minute before sending another feedback message.",
    });
  }

  const submittedInWindow = recent.filter(
    (item) => item.createdAt >= windowStart,
  ).length;

  if (submittedInWindow >= RATE_LIMIT_MAX_PER_WINDOW) {
    throw new ConvexError({
      code: "RATE_LIMIT_EXCEEDED",
      message: "Feedback limit reached for this hour. Please try again later.",
    });
  }
};

export const createFeedback = mutation({
  args: {
    heading: v.string(),
    message: v.string(),
    anonId: v.optional(v.string()),
  },
  handler: async (ctx, args) => {
    const now = Date.now();
    const { safeHeading, safeMessage } = validateSubmission(
      args.heading,
      args.message,
    );
    const { identity, userId } = await resolveOrCreateUser(ctx);
    const anonId = userId ? undefined : normalizeAnonId(args.anonId);

    if (!identity && !anonId) {
      throw new ConvexError({
        code: "USER_SESSION_REQUIRED",
        message: "Open this app in a normal browser tab and try again.",
      });
    }

    await enforceRateLimit(
      ctx,
      {
        userId,
        anonId,
      },
      now,
    );

    const feedbackId = await ctx.db.insert("feedback", {
      heading: safeHeading,
      message: safeMessage,
      status: "open",
      createdByUserId: userId,
      createdByAnonId: anonId,
      createdByClerkId: identity?.subject,
      createdByEmail: identity?.email?.toLowerCase(),
      createdAt: now,
      updatedAt: now,
      resolvedAt: undefined,
      resolvedByClerkId: undefined,
    });

    return { feedbackId };
  },
});

export const listFeedback = query({
  args: {
    limit: v.optional(v.number()),
  },
  handler: async (ctx, args) => {
    const safeLimit = Math.max(
      1,
      Math.min(MAX_FEEDBACK_LIST, Math.floor(args.limit ?? DEFAULT_FEEDBACK_LIST)),
    );
    const canResolve = await isFeedbackAdmin(ctx);

    const openItems = await ctx.db
      .query("feedback")
      .withIndex("by_status_updated", (q) => q.eq("status", "open"))
      .order("desc")
      .take(safeLimit);

    const resolvedLimit = Math.max(0, safeLimit - openItems.length);
    const resolvedItems =
      resolvedLimit > 0
        ? await ctx.db
            .query("feedback")
            .withIndex("by_status_updated", (q) => q.eq("status", "resolved"))
            .order("desc")
            .take(resolvedLimit)
        : [];

    const items = [...openItems, ...resolvedItems].map((item) => ({
      _id: item._id,
      heading: item.heading,
      message: item.message,
      status: item.status,
      createdAt: item.createdAt,
      updatedAt: item.updatedAt,
      resolvedAt: item.resolvedAt,
      authorLabel: item.createdByUserId ? "Signed-in user" : "Anonymous user",
      creatorEmail: canResolve ? item.createdByEmail : undefined,
    }));

    return { canResolve, items };
  },
});

export const setFeedbackResolved = mutation({
  args: {
    feedbackId: v.id("feedback"),
    resolved: v.boolean(),
  },
  handler: async (ctx, args) => {
    const identity = await assertFeedbackAdmin(ctx);
    const item = await ctx.db.get(args.feedbackId);
    if (!item) {
      throw new ConvexError({
        code: "USER_INPUT_INVALID",
        message: "Feedback item no longer exists.",
      });
    }

    const now = Date.now();
    await ctx.db.patch(args.feedbackId, {
      status: args.resolved ? "resolved" : "open",
      updatedAt: now,
      resolvedAt: args.resolved ? now : undefined,
      resolvedByClerkId: args.resolved ? identity.subject : undefined,
    });

    return await ctx.db.get(args.feedbackId);
  },
});
