import Link from "next/link";

import SiteHeader from "@/components/SiteHeader";
import { DEFAULT_GLOBAL_LIMITS, DEFAULT_LIMITS } from "@/lib/limits";

const enforcementSteps = [
  {
    title: "1. Validate per-user plan limits",
    detail:
      "Convex checks files per job, file size, daily jobs, daily minutes, and active job concurrency before enqueue.",
    refs: "convex/jobs.ts -> checkPlanLimits()",
  },
  {
    title: "2. Validate shared global pool",
    detail:
      "Convex checks global daily jobs, global daily minutes, and global active concurrency across all users.",
    refs: "convex/jobs.ts -> checkGlobalLimits()",
  },
  {
    title: "3. Validate monthly budget state",
    detail:
      "If monthly usage is exhausted, new jobs are blocked. Heavy tools (OCR/PDF-A) can be paused earlier.",
    refs: "convex/lib/budget.ts + convex/jobs.ts",
  },
  {
    title: "4. Queue and process safely",
    detail:
      "Jobs are queued in Convex, claimed by worker with lease+heartbeat, then marked succeeded/failed with usage increments.",
    refs: "convex/jobs.ts + worker/worker.py",
  },
];

const hostedModel = [
  "All users share the same global pool for fairness and uptime.",
  "Limits protect free-tier budgets and avoid queue collapse during spikes.",
  "Heavy tools are gated first because OCR/PDF-A are materially more expensive.",
];

const selfHostModel = [
  "You control infrastructure, so limits can be increased or removed by config.",
  "In local development, ZENPDF_DEV_MODE=1 bypasses enforcement checks.",
  "Worker scale and cost are fully your decision when self-hosting.",
];

export default function CapacityLogicPage() {
  return (
    <div className="relative">
      <SiteHeader />
      <main className="mx-auto w-full max-w-6xl px-4 pb-14 pt-5 sm:px-6">
        <section className="paper-card p-5 sm:p-8">
          <span className="ink-label">Capacity Model</span>
          <h1 className="mt-2 text-2xl sm:text-3xl">Why these limits exist.</h1>
          <p className="mt-3 max-w-3xl text-sm text-ink-700">
            ZenPDF keeps all tools free on hosted deployments by enforcing fair-use limits server-side.
            This page explains how pooled limits work and how self-hosting differs.
          </p>

          <div className="mt-6 grid gap-4 lg:grid-cols-2">
            <div className="surface-muted p-4">
              <p className="ink-label">Hosted deployment</p>
              <ul className="mt-3 space-y-2 text-sm text-ink-700">
                {hostedModel.map((item) => (
                  <li key={item} className="flex gap-2">
                    <span className="mt-2 h-1.5 w-1.5 rounded-full bg-forest-600" />
                    <span>{item}</span>
                  </li>
                ))}
              </ul>
            </div>
            <div className="surface-muted p-4">
              <p className="ink-label">Self-hosting</p>
              <ul className="mt-3 space-y-2 text-sm text-ink-700">
                {selfHostModel.map((item) => (
                  <li key={item} className="flex gap-2">
                    <span className="mt-2 h-1.5 w-1.5 rounded-full bg-forest-600" />
                    <span>{item}</span>
                  </li>
                ))}
              </ul>
            </div>
          </div>
        </section>

        <section className="mt-8 grid gap-6 lg:grid-cols-[1.2fr_0.8fr]">
          <div className="space-y-4">
            <h2 className="text-xl sm:text-2xl">Enforcement order in code</h2>
            {enforcementSteps.map((step) => (
              <article key={step.title} className="surface-muted p-4">
                <h3 className="text-sm font-semibold text-ink-900">{step.title}</h3>
                <p className="mt-1 text-sm text-ink-700">{step.detail}</p>
                <p className="mt-2 text-xs text-ink-500">{step.refs}</p>
              </article>
            ))}
          </div>

          <aside className="paper-card p-5 sm:p-6">
            <span className="ink-label">Current defaults</span>
            <h2 className="mt-2 text-lg sm:text-xl">Hosted baseline numbers</h2>

            <div className="mt-4 space-y-3 text-sm text-ink-700">
              <div className="surface-muted p-3">
                <p className="font-semibold text-ink-900">Anon</p>
                <p>
                  {DEFAULT_LIMITS.ANON.maxFilesPerJob} file/job · {DEFAULT_LIMITS.ANON.maxMbPerFile} MB/file
                </p>
                <p>
                  {DEFAULT_LIMITS.ANON.maxJobsPerDay} jobs/day · {DEFAULT_LIMITS.ANON.maxDailyMinutes} min/day
                </p>
              </div>
              <div className="surface-muted p-3">
                <p className="font-semibold text-ink-900">Signed in</p>
                <p>
                  {DEFAULT_LIMITS.FREE_ACCOUNT.maxFilesPerJob} files/job ·{" "}
                  {DEFAULT_LIMITS.FREE_ACCOUNT.maxMbPerFile} MB/file
                </p>
                <p>
                  {DEFAULT_LIMITS.FREE_ACCOUNT.maxJobsPerDay} jobs/day ·{" "}
                  {DEFAULT_LIMITS.FREE_ACCOUNT.maxDailyMinutes} min/day
                </p>
              </div>
              <div className="surface-muted p-3">
                <p className="font-semibold text-ink-900">Global pool</p>
                <p>
                  {DEFAULT_GLOBAL_LIMITS.maxConcurrentJobs} concurrent jobs ·{" "}
                  {DEFAULT_GLOBAL_LIMITS.maxJobsPerDay} jobs/day
                </p>
                <p>{DEFAULT_GLOBAL_LIMITS.maxDailyMinutes} processing min/day</p>
              </div>
            </div>
          </aside>
        </section>

        <section className="mt-8 paper-card p-5 sm:p-6">
          <span className="ink-label">Runtime architecture</span>
          <h2 className="mt-2 text-xl sm:text-2xl">What is running where</h2>
          <div className="mt-4 grid gap-3 md:grid-cols-2">
            <div className="surface-muted p-4">
              <h3 className="text-sm font-semibold text-ink-900">Backend</h3>
              <p className="mt-1 text-sm text-ink-700">
                Convex stores users, jobs, artifacts, usage counters, global usage counters, plan/global
                limits, and budget state. All limit checks are server-side in Convex mutations.
              </p>
            </div>
            <div className="surface-muted p-4">
              <h3 className="text-sm font-semibold text-ink-900">Workers</h3>
              <p className="mt-1 text-sm text-ink-700">
                A Python worker polls Convex (`claimNextJob`), runs PDF tooling, heartbeats lease, uploads
                outputs, and marks jobs succeeded/failed. Target hosted runtime is Google Cloud Run.
              </p>
            </div>
            <div className="surface-muted p-4">
              <h3 className="text-sm font-semibold text-ink-900">Cloudflare vs Google</h3>
              <p className="mt-1 text-sm text-ink-700">
                This stack does not use Cloudflare Workers for job execution. Worker compute is designed for
                Cloud Run. Cloudflare R2 is optional storage, not worker runtime.
              </p>
            </div>
            <div className="surface-muted p-4">
              <h3 className="text-sm font-semibold text-ink-900">Why numbers look conservative</h3>
              <p className="mt-1 text-sm text-ink-700">
                The defaults are chosen to keep free hosted usage reliable under shared load while preserving
                a smooth queue for everyone.
              </p>
            </div>
          </div>

          <div className="mt-6 flex flex-wrap gap-3">
            <Link className="paper-button" href="/usage-capacity">
              Back to usage & capacity
            </Link>
            <Link className="paper-button--ghost" href="/self-host">
              Open self-host guide
            </Link>
            <Link className="paper-button--ghost" href="/tools">
              Open tools
            </Link>
          </div>
        </section>
      </main>
    </div>
  );
}
