"use client";

import { useState } from "react";
import Link from "next/link";
import { SignInButton, SignedIn, SignedOut } from "@clerk/nextjs";
import { useQuery } from "convex/react";

import SiteHeader from "@/components/SiteHeader";
import { getOrCreateAnonId } from "@/lib/anon-id";
import { resolveCapacityState } from "@/lib/capacity";
import { api } from "@/lib/convex";
import { FRIENDLY_ERRORS } from "@/lib/errors";
import { formatBytes, formatPercent } from "@/lib/formatters";
import { DEFAULT_LIMITS, PlanLimits, PlanTier } from "@/lib/limits";

const TIER_LABELS: Record<PlanTier, string> = {
  ANON: "ANON",
  FREE_ACCOUNT: "SIGNED IN",
};

const TIER_DETAILS: Record<PlanTier, { name: string; description: string; note: string }> = {
  ANON: {
    name: "Anon access",
    description: "No sign-in required. Jobs stay tied to this browser only.",
    note: "Sign in to unlock higher daily caps and saved job history.",
  },
  FREE_ACCOUNT: {
    name: "Signed-in access",
    description: "Google sign-in with higher daily limits and saved job history.",
    note: "All tools are free. Limits are only for shared fair-use.",
  },
};

const capacityCopy = {
  available: {
    label: "Available",
    detail: "All tools are active and operating within the monthly budget.",
    tone: "status-pill--success",
  },
  limited: {
    label: "Limited",
    detail: "Heavy tools are paused to keep ZenPDF within budget.",
    tone: "status-pill--warning",
  },
  at_capacity: {
    label: "At Capacity",
    detail: "Monthly budget reached. New jobs will resume next cycle.",
    tone: "status-pill--error",
  },
};

const errorCatalogOrder = [
  "USER_LIMIT_FILE_TOO_LARGE",
  "USER_LIMIT_SIZE_REQUIRED",
  "USER_INPUT_INVALID",
  "USER_LIMIT_MAX_FILES",
  "USER_LIMIT_CONCURRENT_JOBS",
  "USER_LIMIT_DAILY_JOBS",
  "USER_LIMIT_DAILY_MINUTES",
  "USER_SESSION_REQUIRED",
  "SERVICE_CAPACITY_TEMPORARY",
  "SERVICE_CAPACITY_MONTHLY_BUDGET",
] as const;

const errorCatalog = errorCatalogOrder.map((code) => ({
  code,
  ...FRIENDLY_ERRORS[code],
}));

const buildPlanBullets = (tier: PlanTier, limits: PlanLimits) => {
  const bullets = [
    `${limits.maxFilesPerJob} ${limits.maxFilesPerJob === 1 ? "file" : "files"} per job`,
    `${formatBytes(limits.maxMbPerFile * 1024 * 1024)} per file`,
    `${limits.maxJobsPerDay} jobs per day`,
    `${limits.maxDailyMinutes} minutes per day`,
  ];
  if (tier === "ANON") {
    bullets.push("Local-only job history");
  } else {
    bullets.push("Saved job history");
  }
  return bullets;
};

const UsageBar = ({
  label,
  value,
  limit,
  helper,
  badge,
}: {
  label: string;
  value: number;
  limit: number;
  helper: string;
  badge?: string;
}) => {
  const safeLimit = Number.isFinite(limit) && limit > 0 ? limit : 1;
  const percent =
    Number.isFinite(value) && limit > 0
      ? Math.min((value / safeLimit) * 100, 100)
      : 0;
  const statusLabel = badge ?? formatPercent(value, safeLimit);

  return (
    <div className="paper-card p-5">
      <div className="flex items-start justify-between gap-4">
        <div>
          <p className="text-sm font-semibold text-ink-900">{label}</p>
          <p className="mt-1 text-xs text-ink-500">{helper}</p>
        </div>
        <span className="status-pill">{statusLabel}</span>
      </div>
      <div className="mt-3 h-2 rounded-full bg-paper-200">
        <div
          className="h-2 rounded-full bg-forest-600"
          style={{ width: `${percent}%` }}
        />
      </div>
    </div>
  );
};

const PlanCard = ({ tier, limits }: { tier: PlanTier; limits: PlanLimits }) => {
  const details = TIER_DETAILS[tier];
  const bullets = buildPlanBullets(tier, limits);

  return (
    <div className="paper-card flex h-full flex-col p-6">
      <div className="min-h-[7.25rem]">
        <span className="ink-label">{TIER_LABELS[tier]}</span>
        <h3 className="mt-2 text-lg font-semibold text-ink-900">{details.name}</h3>
        <p className="mt-2 text-sm text-ink-500">{details.description}</p>
      </div>
      <ul className="mt-4 flex-1 space-y-2 text-sm text-ink-700">
        {bullets.map((bullet) => (
          <li key={bullet} className="flex items-center gap-2">
            <span className="h-1.5 w-1.5 rounded-full bg-forest-600" />
            {bullet}
          </li>
        ))}
      </ul>
      <div className="surface-muted mt-4 px-3 py-2 text-xs text-ink-600">{details.note}</div>
    </div>
  );
};

export default function UsageCapacityPage() {
  const [anonId] = useState(() => getOrCreateAnonId());
  const snapshot = useQuery(
    api.capacity.getUsageSnapshot,
    anonId ? { anonId } : {},
  );

  const tier = snapshot?.tier ?? "ANON";
  const planLimits = snapshot?.planLimits ?? DEFAULT_LIMITS[tier];
  const plans = snapshot?.plans ?? DEFAULT_LIMITS;
  const signedInLimits = plans.FREE_ACCOUNT ?? DEFAULT_LIMITS.FREE_ACCOUNT;
  const budget = snapshot?.budget ?? {
    monthlyBudgetUsage: 0,
    heavyToolsEnabled: true,
    status: "available",
  };
  const usage = snapshot?.usage ?? {
    periodStart: 0,
    jobsUsed: 0,
    minutesUsed: 0,
    bytesProcessed: 0,
  };
  const globalLimits = snapshot?.globalLimits ?? {
    maxConcurrentJobs: 0,
    maxJobsPerDay: 0,
    maxDailyMinutes: 0,
    jobMaxAttempts: 0,
    leaseDurationMs: 0,
    artifactTtlHours: 24,
  };
  const globalUsage = snapshot?.globalUsage ?? {
    periodStart: 0,
    jobsUsed: 0,
    minutesUsed: 0,
    bytesProcessed: 0,
  };

  const jobsUsed = Math.max(usage.jobsUsed, 0);
  const minutesUsed = Math.max(Math.round(usage.minutesUsed), 0);
  const poolJobsUsed = Math.max(globalUsage.jobsUsed, 0);
  const poolMinutesUsed = Math.max(Math.round(globalUsage.minutesUsed), 0);
  const poolJobsRemaining = Math.max(globalLimits.maxJobsPerDay - poolJobsUsed, 0);
  const poolMinutesRemaining = Math.max(globalLimits.maxDailyMinutes - poolMinutesUsed, 0);
  const budgetPercent = formatPercent(budget.monthlyBudgetUsage, 1);
  const capacityState = resolveCapacityState({
    monthlyBudgetUsage: budget.monthlyBudgetUsage,
    heavyToolsEnabled: budget.heavyToolsEnabled,
  });
  const capacityStatus = capacityCopy[capacityState];
  const tierDetails = TIER_DETAILS[tier];

  return (
    <div className="relative">
      <SiteHeader />
      <main className="mx-auto w-full max-w-6xl px-4 pb-14 pt-5 sm:px-6">
        <section className="paper-card p-8">
          <span className="ink-label">Usage & Capacity</span>
          <h1 className="mt-2 text-3xl">Know every limit before you upload.</h1>
          <p className="mt-3 max-w-2xl text-sm text-ink-700">
            ZenPDF is fully free for all tools. Limits exist only to keep shared
            infrastructure stable and available for everyone.
          </p>
          <div className="mt-4 grid gap-2 sm:grid-cols-3">
            <div className="surface-muted p-3">
              <p className="ink-label">Use this page when</p>
              <p className="mt-1 text-sm text-ink-700">A job failed due to limits.</p>
            </div>
            <div className="surface-muted p-3">
              <p className="ink-label">Use this page when</p>
              <p className="mt-1 text-sm text-ink-700">You are planning larger file batches.</p>
            </div>
            <div className="surface-muted p-3">
              <p className="ink-label">Use this page when</p>
              <p className="mt-1 text-sm text-ink-700">You need current pool status before OCR/PDF-A.</p>
            </div>
          </div>

          <div className="mt-5 flex flex-wrap items-center gap-2">
            <span className={`status-pill ${capacityStatus.tone}`}>
              Status: {capacityStatus.label}
            </span>
            <span className="status-pill">{capacityStatus.detail}</span>
            {!snapshot && <span className="status-pill">Loading latest snapshot...</span>}
          </div>
        </section>

        <section className="mt-8 grid gap-6 lg:grid-cols-[1.2fr_0.8fr]">
          <div className="space-y-6">
            <div>
              <div className="flex flex-wrap items-center justify-between gap-3">
                <h2 className="text-2xl">Your daily usage</h2>
                <span className="status-pill">{TIER_LABELS[tier]}</span>
              </div>
              <p className="mt-2 text-sm text-ink-700">
                Metrics reset daily and are enforced before processing begins.
              </p>
            </div>

            <div className="grid gap-4 md:grid-cols-2">
              <UsageBar
                label="Jobs today"
                value={jobsUsed}
                limit={planLimits.maxJobsPerDay}
                helper={`${jobsUsed} of ${planLimits.maxJobsPerDay} jobs used`}
              />
              <UsageBar
                label="Processing minutes"
                value={minutesUsed}
                limit={planLimits.maxDailyMinutes}
                helper={`${minutesUsed} of ${planLimits.maxDailyMinutes} minutes used`}
              />
              <UsageBar
                label="Max file size"
                value={planLimits.maxMbPerFile}
                limit={signedInLimits.maxMbPerFile}
                helper={`${formatBytes(planLimits.maxMbPerFile * 1024 * 1024)} per file`}
              />
              <UsageBar
                label="Max files per job"
                value={planLimits.maxFilesPerJob}
                limit={signedInLimits.maxFilesPerJob}
                helper={`${planLimits.maxFilesPerJob} files per job`}
              />
              <UsageBar
                label="Concurrent jobs"
                value={planLimits.maxConcurrentJobs}
                limit={signedInLimits.maxConcurrentJobs}
                helper={`${planLimits.maxConcurrentJobs} jobs at once`}
              />
              <UsageBar
                label="Heavy tools availability"
                value={budget.heavyToolsEnabled ? 1 : 0}
                limit={1}
                badge={budget.heavyToolsEnabled ? "On" : "Paused"}
                helper={
                  budget.heavyToolsEnabled
                    ? "OCR + PDF/A enabled"
                    : "OCR + PDF/A paused to protect budget"
                }
              />
            </div>

            <div>
              <h2 className="text-2xl">Shared daily pool</h2>
              <p className="mt-2 text-sm text-ink-700">
                This pool is shared across all users and is enforced server-side.
              </p>
              <div className="mt-4 grid gap-4 md:grid-cols-2">
                <UsageBar
                  label="Pool jobs used"
                  value={poolJobsUsed}
                  limit={globalLimits.maxJobsPerDay}
                  helper={`${poolJobsUsed} of ${globalLimits.maxJobsPerDay} used · ${poolJobsRemaining} remaining`}
                />
                <UsageBar
                  label="Pool minutes used"
                  value={poolMinutesUsed}
                  limit={globalLimits.maxDailyMinutes}
                  helper={`${poolMinutesUsed} of ${globalLimits.maxDailyMinutes} used · ${poolMinutesRemaining} remaining`}
                />
                <UsageBar
                  label="Pool concurrency cap"
                  value={globalLimits.maxConcurrentJobs}
                  limit={globalLimits.maxConcurrentJobs}
                  helper={`${globalLimits.maxConcurrentJobs} jobs can run concurrently`}
                />
                <UsageBar
                  label="Monthly budget usage"
                  value={budget.monthlyBudgetUsage}
                  limit={1}
                  helper={`${budgetPercent} of the monthly budget used`}
                />
              </div>
            </div>
          </div>

          <aside className="paper-card flex flex-col gap-4 p-6">
            <span className="ink-label">Your access</span>
            <div>
              <h3 className="text-xl font-semibold text-ink-900">{tierDetails.name}</h3>
              <p className="mt-2 text-sm text-ink-700">{tierDetails.description}</p>
            </div>
            <div className="surface-muted p-4 text-sm text-ink-700">{tierDetails.note}</div>

            <SignedOut>
              <SignInButton mode="modal">
                <button className="paper-button" type="button">
                  Sign in to unlock higher limits
                </button>
              </SignInButton>
            </SignedOut>

            <SignedIn>
              <div className="alert alert--success">
                Signed-in limits are active. You still share the same global pool with all users.
              </div>
            </SignedIn>

            <Link className="paper-button--ghost" href="/self-host">
              Run ZenPDF locally
            </Link>
            <Link className="paper-button" href="/tools">
              Return to tools
            </Link>
          </aside>
        </section>

        <section className="mt-10">
          <span className="ink-label">Access tiers</span>
          <h2 className="mt-2 text-2xl">Transparent limits for fair use.</h2>
          <p className="mt-2 max-w-2xl text-sm text-ink-700">
            Limits reset daily and are enforced server-side for every tool.
          </p>
          <div className="mt-6 grid gap-4 md:grid-cols-2">
            {(["ANON", "FREE_ACCOUNT"] as PlanTier[]).map((plan) => (
              <PlanCard key={plan} tier={plan} limits={plans[plan]} />
            ))}
          </div>
        </section>

        <section className="mt-10">
          <h2 className="text-2xl">Friendly error catalog</h2>
          <p className="mt-2 text-sm text-ink-700">
            When a limit is hit, ZenPDF responds with a stable code and a plain-language message.
          </p>
          <div className="mt-6 grid gap-3 md:grid-cols-2">
            {errorCatalog.map((error) => (
              <details key={error.code} className="paper-card group p-4">
                <summary className="cursor-pointer list-none">
                  <div className="text-[0.7rem] font-semibold uppercase tracking-[0.14em] text-ink-500">
                    {error.code}
                  </div>
                  <p className="mt-2 text-sm text-ink-900">{error.message}</p>
                  <p className="mt-2 text-xs text-ink-500 group-open:hidden">Click to view guidance</p>
                </summary>
                <p className="mt-2 text-xs text-ink-600">{error.next}</p>
              </details>
            ))}
          </div>
        </section>
      </main>
    </div>
  );
}
