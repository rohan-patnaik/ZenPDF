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
  FREE_ACCOUNT: "FREE ACCOUNT",
  PREMIUM: "PREMIUM",
};

const TIER_DETAILS: Record<
  PlanTier,
  { name: string; description: string; note: string }
> = {
  ANON: {
    name: "Anon access",
    description: "No sign-in required. Jobs stay tied to this browser only.",
    note: "Sign in to unlock FREE limits and saved history.",
  },
  FREE_ACCOUNT: {
    name: "Free account",
    description: "Google sign-in with higher limits and saved job history.",
    note: "Supporter mode unlocks OCR, PDF/A, and larger batches.",
  },
  PREMIUM: {
    name: "Premium supporter",
    description: "Feature-flagged supporter mode for OCR and larger workloads.",
    note: "Premium access still respects monthly budget health.",
  },
};

const capacityCopy = {
  available: {
    label: "Available",
    detail: "All tools are active and operating within the monthly budget.",
  },
  limited: {
    label: "Limited",
    detail: "Heavy tools are paused to keep ZenPDF within budget.",
  },
  at_capacity: {
    label: "At Capacity",
    detail: "Monthly budget reached. New jobs will resume next cycle.",
  },
};

const errorCatalogOrder = [
  "USER_LIMIT_FILE_TOO_LARGE",
  "USER_LIMIT_SIZE_REQUIRED",
  "USER_INPUT_INVALID",
  "USER_LIMIT_MAX_FILES",
  "USER_LIMIT_CONCURRENT_JOBS",
  "USER_LIMIT_PREMIUM_REQUIRED",
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
  } else if (tier === "FREE_ACCOUNT") {
    bullets.push("Saved job history");
  } else {
    bullets.push("OCR + PDF/A when enabled");
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
          <p className="text-sm font-display text-ink-900">{label}</p>
          <p className="text-xs text-ink-500">{helper}</p>
        </div>
        <span className="text-[0.65rem] uppercase tracking-[0.2em] text-ink-500">
          {statusLabel}
        </span>
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
    <div className="paper-card flex h-full flex-col gap-4 p-6">
      <div>
        <span className="ink-label">{TIER_LABELS[tier]}</span>
        <h3 className="mt-2 text-xl font-display text-ink-900">{details.name}</h3>
        <p className="mt-2 text-xs text-ink-500">{details.description}</p>
      </div>
      <ul className="space-y-2 text-sm text-ink-700">
        {bullets.map((bullet) => (
          <li key={bullet} className="flex items-center gap-2">
            <span className="h-1.5 w-1.5 rounded-full bg-forest-600" />
            {bullet}
          </li>
        ))}
      </ul>
      <div className="mt-auto rounded-[18px] border border-ink-900/10 bg-paper-100 px-3 py-2 text-xs text-ink-600">
        {details.note}
      </div>
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
  const premiumLimits = plans.PREMIUM ?? DEFAULT_LIMITS.PREMIUM;
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

  const jobsUsed = Math.max(usage.jobsUsed, 0);
  const minutesUsed = Math.max(Math.round(usage.minutesUsed), 0);
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
      <main className="relative z-10 mx-auto w-full max-w-6xl px-6 pb-16">
        <section className="paper-card mt-4 p-8">
          <span className="ink-label">Usage & Capacity</span>
          <h1 className="mt-3 text-4xl">Know every limit before you upload.</h1>
          <p className="mt-3 max-w-2xl text-base text-ink-700">
            ZenPDF keeps usage predictable by enforcing caps per plan and
            monitoring global capacity. When the system nears its monthly
            budget, heavy tools pause first.
          </p>
          <div className="mt-6 flex flex-wrap gap-3">
            <span className="rounded-full border border-forest-600/30 bg-sage-200 px-4 py-2 text-sm uppercase tracking-[0.2em] text-forest-700">
              Status: {capacityStatus.label}
            </span>
            <span className="rounded-full border border-ink-900/10 bg-paper-100 px-4 py-2 text-sm text-ink-700">
              {capacityStatus.detail}
            </span>
          </div>
        </section>

        <section className="mt-8 grid gap-6 lg:grid-cols-[1.2fr_0.8fr]">
          <div className="space-y-5">
            <div className="flex flex-wrap items-center justify-between gap-3">
              <h2 className="text-2xl">Your plan snapshot</h2>
              <span className="rounded-full border border-ink-900/10 bg-paper-100 px-4 py-1 text-xs uppercase tracking-[0.2em] text-ink-600">
                {TIER_LABELS[tier]}
              </span>
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
                label="Monthly budget usage"
                value={budget.monthlyBudgetUsage}
                limit={1}
                helper={`${budgetPercent} of the monthly budget used`}
              />
              <UsageBar
                label="Max file size"
                value={planLimits.maxMbPerFile}
                limit={premiumLimits.maxMbPerFile}
                helper={`${formatBytes(planLimits.maxMbPerFile * 1024 * 1024)} per file`}
              />
              <UsageBar
                label="Max files per job"
                value={planLimits.maxFilesPerJob}
                limit={premiumLimits.maxFilesPerJob}
                helper={`${planLimits.maxFilesPerJob} files per job`}
              />
              <UsageBar
                label="Concurrent jobs"
                value={planLimits.maxConcurrentJobs}
                limit={premiumLimits.maxConcurrentJobs}
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
          </div>
          <div className="paper-card flex flex-col gap-4 p-6">
            <span className="ink-label">Your access</span>
            <div>
              <h3 className="text-2xl font-display text-ink-900">
                {tierDetails.name}
              </h3>
              <p className="mt-2 text-sm text-ink-700">
                {tierDetails.description}
              </p>
            </div>
            <div className="rounded-[20px] border border-ink-900/10 bg-paper-100 p-4 text-sm text-ink-700">
              {tierDetails.note}
            </div>
            <SignedOut>
              <SignInButton mode="modal">
                <button className="paper-button" type="button">
                  Sign in to unlock free limits
                </button>
              </SignInButton>
            </SignedOut>
            <SignedIn>
              <div className="rounded-[20px] border border-forest-600/30 bg-sage-200/70 p-4 text-sm text-forest-700">
                {tier === "PREMIUM"
                  ? "Supporter mode is enabled per account and may pause when capacity is limited."
                  : "Supporter mode is available for Premium accounts and may pause when capacity is limited."}
              </div>
            </SignedIn>
            <div className="mt-auto rounded-[20px] border border-ink-900/10 bg-paper-100 p-4 text-sm text-ink-700">
              Run ZenPDF locally for unlimited usage and full control.
            </div>
            <Link className="paper-button" href="/tools">
              Return to tools
            </Link>
          </div>
        </section>

        <section className="mt-10">
          <span className="ink-label">Plan tiers</span>
          <h2 className="mt-2 text-2xl">Choose the right tier for the workload.</h2>
          <p className="mt-2 max-w-2xl text-sm text-ink-700">
            Limits reset daily and are enforced server-side. Upgrade tiers when you
            need larger batches, saved history, or OCR-heavy workflows.
          </p>
          <div className="mt-6 grid gap-4 md:grid-cols-3">
            {(["ANON", "FREE_ACCOUNT", "PREMIUM"] as PlanTier[]).map((plan) => (
              <PlanCard key={plan} tier={plan} limits={plans[plan]} />
            ))}
          </div>
        </section>

        <section className="mt-10">
          <h2 className="text-2xl">Friendly error catalog</h2>
          <p className="mt-2 text-sm text-ink-700">
            When a limit is hit, ZenPDF responds with a stable code and a
            plain-language message.
          </p>
          <div className="mt-6 grid gap-4 md:grid-cols-2">
            {errorCatalog.map((error) => (
              <div key={error.code} className="paper-card p-5">
                <div className="text-xs uppercase tracking-[0.2em] text-ink-500">
                  {error.code}
                </div>
                <p className="mt-2 text-sm text-ink-900">{error.message}</p>
                <p className="mt-2 text-xs text-ink-500">{error.next}</p>
              </div>
            ))}
          </div>
        </section>
      </main>
    </div>
  );
}
