import Link from "next/link";

import SiteHeader from "@/components/SiteHeader";
import { resolveCapacityState } from "@/lib/capacity";
import { FRIENDLY_ERRORS } from "@/lib/errors";
import { formatBytes, formatPercent } from "@/lib/formatters";
import { DEFAULT_LIMITS, PlanTier } from "@/lib/limits";

const tier: PlanTier = "FREE_ACCOUNT";
const tierLimits = DEFAULT_LIMITS[tier];
const premiumLimits = DEFAULT_LIMITS.PREMIUM;

const usageSnapshot = {
  jobsToday: 8,
  jobsLimit: tierLimits.maxJobsPerDay,
  monthlyBudgetUsage: 0.76,
  heavyToolsEnabled: false,
};

const capacityState = resolveCapacityState({
  monthlyBudgetUsage: usageSnapshot.monthlyBudgetUsage,
  heavyToolsEnabled: usageSnapshot.heavyToolsEnabled,
});

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
  "USER_LIMIT_MAX_FILES",
  "USER_LIMIT_CONCURRENT_JOBS",
  "USER_LIMIT_DAILY_JOBS",
  "USER_LIMIT_DAILY_MINUTES",
  "SERVICE_CAPACITY_TEMPORARY",
  "SERVICE_CAPACITY_MONTHLY_BUDGET",
] as const;

const errorCatalog = errorCatalogOrder.map((code) => ({
  code,
  ...FRIENDLY_ERRORS[code],
}));

const UsageBar = ({
  label,
  value,
  limit,
  helper,
}: {
  label: string;
  value: number;
  limit: number;
  helper: string;
}) => {
  const safeLimit = Number.isFinite(limit) && limit > 0 ? limit : 1;
  const percent =
    Number.isFinite(value) && limit > 0
      ? Math.min((value / safeLimit) * 100, 100)
      : 0;

  return (
    <div className="paper-card p-5">
      <div className="flex items-start justify-between gap-4">
        <div>
          <p className="text-sm font-display text-ink-900">{label}</p>
          <p className="text-xs text-ink-500">{helper}</p>
        </div>
        <span className="text-[0.65rem] uppercase tracking-[0.2em] text-ink-500">
          {formatPercent(value, safeLimit)}
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

export default function UsageCapacityPage() {
  const capacityStatus = capacityCopy[capacityState];

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
            <h2 className="text-2xl">Your plan snapshot</h2>
            <UsageBar
              label="Jobs today"
              value={usageSnapshot.jobsToday}
              limit={usageSnapshot.jobsLimit}
              helper={`${usageSnapshot.jobsToday} of ${usageSnapshot.jobsLimit} jobs used`}
            />
            <UsageBar
              label="Max file size"
              value={tierLimits.maxMbPerFile}
              limit={premiumLimits.maxMbPerFile}
              helper={`${formatBytes(tierLimits.maxMbPerFile * 1024 * 1024)} per file`}
            />
            <UsageBar
              label="Max files per job"
              value={tierLimits.maxFilesPerJob}
              limit={premiumLimits.maxFilesPerJob}
              helper={`${tierLimits.maxFilesPerJob} files per job`}
            />
            <UsageBar
              label="Heavy tools availability"
              value={usageSnapshot.heavyToolsEnabled ? 1 : 0}
              limit={1}
              helper={
                usageSnapshot.heavyToolsEnabled
                  ? "OCR + PDF/A enabled"
                  : "OCR + PDF/A paused to protect budget"
              }
            />
          </div>
          <div className="paper-card flex flex-col gap-4 p-6">
            <span className="ink-label">Plan tiers</span>
            <div className="space-y-4 text-sm text-ink-700">
              <p>
                ANON: quick tasks with the smallest caps. FREE accounts unlock
                higher limits and saved history. PREMIUM expands batch sizes and
                enables advanced tools.
              </p>
              <p>
                All limits are enforced on the server. If capacity is limited,
                you will always see a friendly explanation.
              </p>
            </div>
            <div className="mt-auto rounded-[20px] border border-ink-900/10 bg-paper-100 p-4 text-sm text-ink-700">
              Run ZenPDF locally for unlimited usage and full control.
            </div>
            <Link className="paper-button" href="/">
              Return to tools
            </Link>
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
