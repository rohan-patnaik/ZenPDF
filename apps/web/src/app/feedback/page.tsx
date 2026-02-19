"use client";

import Link from "next/link";
import { useMutation, useQuery } from "convex/react";
import { useMemo, useState } from "react";

import SiteHeader from "@/components/SiteHeader";
import { api } from "@/lib/convex";

const dateFormatter = new Intl.DateTimeFormat(undefined, {
  dateStyle: "medium",
  timeStyle: "short",
});

const timeAgoFormatter = new Intl.RelativeTimeFormat(undefined, {
  numeric: "auto",
});

const formatDate = (timestamp: number) => dateFormatter.format(timestamp);

const maskEmail = (email: string) => {
  const [localPart, domainPart] = email.split("@");
  if (!localPart || !domainPart) {
    return "masked";
  }

  if (localPart.length <= 2) {
    return `${localPart[0] ?? "*"}*@${domainPart}`;
  }

  return `${localPart[0]}${"*".repeat(Math.max(localPart.length - 2, 1))}${localPart[localPart.length - 1]}@${domainPart}`;
};

const formatRelative = (timestamp: number) => {
  const deltaSeconds = Math.round((timestamp - Date.now()) / 1000);
  const absSeconds = Math.abs(deltaSeconds);

  if (absSeconds < 60) {
    return timeAgoFormatter.format(deltaSeconds, "second");
  }
  if (absSeconds < 3600) {
    return timeAgoFormatter.format(Math.round(deltaSeconds / 60), "minute");
  }
  if (absSeconds < 86400) {
    return timeAgoFormatter.format(Math.round(deltaSeconds / 3600), "hour");
  }
  return timeAgoFormatter.format(Math.round(deltaSeconds / 86400), "day");
};

export default function FeedbackPage() {
  const [error, setError] = useState("");
  const [pendingId, setPendingId] = useState<string | null>(null);

  const feedback = useQuery(api.feedback.listFeedback, {
    limit: 150,
  });
  const setFeedbackResolved = useMutation(api.feedback.setFeedbackResolved);

  const counts = useMemo(() => {
    const items = feedback?.items ?? [];
    return {
      total: items.length,
      open: items.filter((item) => item.status === "open").length,
      resolved: items.filter((item) => item.status === "resolved").length,
    };
  }, [feedback?.items]);

  const toggleResolved = async (feedbackId: string, nextResolved: boolean) => {
    try {
      setPendingId(feedbackId);
      setError("");
      await setFeedbackResolved({
        feedbackId,
        resolved: nextResolved,
      });
    } catch (mutationError) {
      const fallback = "Could not update this feedback item right now.";
      if (
        mutationError &&
        typeof mutationError === "object" &&
        "data" in mutationError &&
        mutationError.data &&
        typeof mutationError.data === "object" &&
        "message" in mutationError.data &&
        typeof mutationError.data.message === "string"
      ) {
        setError(mutationError.data.message);
      } else {
        setError(fallback);
      }
    } finally {
      setPendingId(null);
    }
  };

  return (
    <div className="relative">
      <SiteHeader />
      <main className="mx-auto w-full max-w-6xl px-4 pb-14 pt-5 sm:px-6">
        <section className="paper-card p-5 sm:p-8">
          <span className="ink-label">Feedback board</span>
          <div className="mt-2 flex flex-wrap items-start justify-between gap-3">
            <div className="max-w-2xl">
              <h1 className="text-2xl sm:text-3xl">Track product feedback transparently.</h1>
              <p className="mt-3 text-sm text-ink-700">
                Items are sorted with open issues first. Resolved items stay visible for everyone.
              </p>
            </div>
            <Link href="/tools" className="paper-button--ghost w-auto">
              Return to tools
            </Link>
          </div>

          <div className="mt-5 grid gap-2 sm:grid-cols-3">
            <div className="surface-muted p-3">
              <p className="ink-label">Open</p>
              <p className="mt-1 text-xl font-semibold text-ink-900">{counts.open}</p>
            </div>
            <div className="surface-muted p-3">
              <p className="ink-label">Resolved</p>
              <p className="mt-1 text-xl font-semibold text-ink-900">{counts.resolved}</p>
            </div>
            <div className="surface-muted p-3">
              <p className="ink-label">Total</p>
              <p className="mt-1 text-xl font-semibold text-ink-900">{counts.total}</p>
            </div>
          </div>

          {!feedback ? <p className="status-pill mt-4">Loading feedback...</p> : null}
          {error ? <p className="alert alert--error mt-4">{error}</p> : null}
        </section>

        <section className="mt-4 space-y-3">
          {feedback && feedback.items.length > 0 ? (
            feedback.items.map((item) => {
              const isResolved = item.status === "resolved";
              const isBusy = pendingId === item._id;

              return (
                <article key={item._id} className="paper-card p-4 sm:p-5">
                  <div className="flex flex-wrap items-start justify-between gap-3">
                    <div>
                      <h2 className="text-lg font-semibold text-ink-900">{item.heading}</h2>
                      <p className="mt-1 text-sm text-ink-700 whitespace-pre-wrap">{item.message}</p>
                    </div>
                    <span
                      className={`status-pill ${
                        isResolved ? "" : "status-pill--success"
                      }`}
                    >
                      {isResolved ? "Resolved" : "Open"}
                    </span>
                  </div>

                  <div className="mt-3 flex flex-wrap items-center gap-x-4 gap-y-1 text-xs text-ink-500">
                    <span>{item.authorLabel}</span>
                    {feedback.canResolve && item.creatorEmail ? (
                      <span>{maskEmail(item.creatorEmail)}</span>
                    ) : null}
                    <span title={formatDate(item.createdAt)}>Opened {formatRelative(item.createdAt)}</span>
                    {item.resolvedAt ? (
                      <span title={formatDate(item.resolvedAt)}>
                        Resolved {formatRelative(item.resolvedAt)}
                      </span>
                    ) : null}
                  </div>

                  {feedback.canResolve ? (
                    <label className="mt-3 inline-flex items-center gap-2 text-sm text-ink-700">
                      <input
                        type="checkbox"
                        className="h-4 w-4 rounded border-paper-300 text-forest-600"
                        checked={isResolved}
                        disabled={isBusy}
                        onChange={(event) => {
                          void toggleResolved(item._id, event.target.checked);
                        }}
                      />
                      Mark as resolved
                    </label>
                  ) : null}
                </article>
              );
            })
          ) : feedback ? (
            <div className="paper-card p-5 sm:p-6">
              <p className="text-sm text-ink-700">
                No feedback yet. Use the Feedback button in the top navbar to submit the first item.
              </p>
            </div>
          ) : null}
        </section>
      </main>
    </div>
  );
}
