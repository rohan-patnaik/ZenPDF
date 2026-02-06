import Link from "next/link";

import SiteHeader from "@/components/SiteHeader";
import { formatBytes } from "@/lib/formatters";
import { DEFAULT_LIMITS } from "@/lib/limits";

const toolShelves = [
  {
    title: "Combine & Organize",
    description: "Merge, split, and re-sequence large dossiers without losing page fidelity.",
    items: ["Merge PDF", "Split PDF", "Organize PDF"],
  },
  {
    title: "Convert & Export",
    description: "Convert across document formats for sharing and archiving.",
    items: ["PDF to Word", "PDF to PowerPoint", "PDF to Excel", "PDF to JPG"],
  },
  {
    title: "Protect & Repair",
    description: "Lock, unlock, repair, and compare versions while keeping text intact.",
    items: ["Protect & unlock", "Compare versions", "Repair PDFs"],
  },
  {
    title: "Edit & Sign",
    description: "Apply edits, visible signatures, page numbers, and redactions.",
    items: ["Edit PDF", "Sign PDF", "Page numbers", "Redact PDF"],
  },
  {
    title: "Capture & OCR",
    description: "Convert scans and image captures into searchable PDFs.",
    items: ["Scan to PDF", "OCR PDF", "PDF to PDF/A", "Crop PDF"],
  },
];
const totalToolCount = toolShelves.reduce((count, shelf) => count + shelf.items.length, 0);

const { ANON, FREE_ACCOUNT } = DEFAULT_LIMITS;

const planSnapshots = [
  {
    tier: "ANON",
    description: "No sign-in required for quick one-off work.",
    details: [
      `${ANON.maxFilesPerJob} ${ANON.maxFilesPerJob === 1 ? "file" : "files"} per job`,
      `${formatBytes(ANON.maxMbPerFile * 1024 * 1024)} per file`,
      `${ANON.maxJobsPerDay} jobs per day`,
    ],
  },
  {
    tier: "FREE ACCOUNT",
    description: "Google sign-in adds history and higher daily limits.",
    details: [
      `${FREE_ACCOUNT.maxFilesPerJob} files per job`,
      `${formatBytes(FREE_ACCOUNT.maxMbPerFile * 1024 * 1024)} per file`,
      `${FREE_ACCOUNT.maxJobsPerDay} jobs per day`,
    ],
  },
];

const steps = [
  {
    title: "Collect",
    copy: "Upload files through secure, signed links to Convex storage.",
  },
  {
    title: "Compose",
    copy: "Queue a job, then let the worker run the heavy PDF lifting.",
  },
  {
    title: "Deliver",
    copy: "Download the results with clear output names and file sizes.",
  },
];

const practicalRoutes = [
  {
    title: "Run a PDF task",
    description: "Choose a tool, upload files, and queue processing jobs.",
    href: "/tools",
    cta: "Open tools",
  },
  {
    title: "Check limits first",
    description: "Review your daily caps and shared capacity before heavy work.",
    href: "/usage-capacity",
    cta: "Open usage",
  },
  {
    title: "Need unlimited usage",
    description: "Run ZenPDF locally when you need full control and no cloud caps.",
    href: "/usage-capacity",
    cta: "See self-host path",
  },
];

const ToolCard = ({
  title,
  description,
  items,
}: {
  title: string;
  description: string;
  items: string[];
}) => (
  <div className="paper-card flex h-full flex-col gap-4 p-6">
    <div>
      <h3 className="text-lg font-semibold">{title}</h3>
      <p className="mt-1 text-sm text-ink-500">{description}</p>
    </div>
    <ul className="mt-auto space-y-2 text-sm text-ink-700">
      {items.map((item) => (
        <li key={item} className="flex items-center gap-2">
          <span className="h-1.5 w-1.5 rounded-full bg-forest-600" />
          {item}
        </li>
      ))}
    </ul>
  </div>
);

export default function Home() {
  return (
    <div className="relative">
      <SiteHeader />
      <main className="mx-auto w-full max-w-6xl px-4 pb-14 pt-5 sm:px-6">
        <section className="grid gap-6 lg:grid-cols-[1.15fr_0.85fr]">
          <div className="paper-card p-8 fade-up">
            <span className="ink-label">Open-source PDF workbench</span>
            <h1 className="mt-3 text-3xl leading-tight sm:text-4xl">
              Clean, reliable PDF workflows with transparent limits.
            </h1>
            <p className="mt-4 max-w-2xl text-base text-ink-700">
              ZenPDF keeps document processing predictable with clear capacity rules,
              friendly error feedback, and server-side enforcement. Upload, configure,
              and run tools in a guided flow.
            </p>
            <div className="mt-6 flex flex-wrap gap-3">
              <Link className="paper-button" href="/tools">
                Start with a file
              </Link>
              <Link className="paper-button--ghost" href="/usage-capacity">
                View usage limits
              </Link>
            </div>
          </div>

          <div className="paper-stack p-6 fade-up" style={{ animationDelay: "0.08s" }}>
            <div className="space-y-4">
              <div className="paper-card p-4">
                <div className="flex items-center justify-between gap-3">
                  <span className="ink-label">Status</span>
                  <span className="status-pill status-pill--success">Available</span>
                </div>
                <p className="mt-2 text-sm text-ink-700">
                  Capacity is monitored hourly to protect free-tier budgets.
                </p>
              </div>

              <div className="paper-card p-4">
                <div className="flex items-center justify-between gap-3">
                  <span className="ink-label">Active tools</span>
                  <span className="status-pill">{totalToolCount} available</span>
                </div>
                <p className="mt-2 text-sm text-ink-700">
                  Heavy tools can pause when monthly budget limits are reached.
                </p>
              </div>

              <div className="paper-card p-4">
                <div className="flex items-center justify-between gap-3">
                  <span className="ink-label">Storage</span>
                  <span className="status-pill">Convex Files</span>
                </div>
                <p className="mt-2 text-sm text-ink-700">
                  Files expire automatically to keep storage lean and controlled.
                </p>
              </div>
            </div>
          </div>
        </section>

        <section className="mt-10">
          <span className="ink-label">Choose your route</span>
          <h2 className="mt-2 text-2xl">Go to the right place for your task.</h2>
          <div className="mt-4 grid gap-3 md:grid-cols-3">
            {practicalRoutes.map((route) => (
              <div key={route.title} className="paper-card flex flex-col gap-3 p-5">
                <h3 className="text-base font-semibold text-ink-900">{route.title}</h3>
                <p className="text-sm text-ink-700">{route.description}</p>
                <Link className="paper-button--ghost mt-auto w-fit" href={route.href}>
                  {route.cta}
                </Link>
              </div>
            ))}
          </div>
        </section>

        <section className="mt-10">
          <div className="flex flex-wrap items-end justify-between gap-3">
            <div>
              <span className="ink-label">Tool shelf</span>
              <h2 className="mt-2 text-2xl">Everyday PDF operations in one desk.</h2>
            </div>
            <Link className="paper-button--ghost" href="/tools">
              Open tools
            </Link>
          </div>
          <div className="ink-divider mt-4" />
          <div className="mt-6 grid gap-4 md:grid-cols-2">
            {toolShelves.map((tool) => (
              <ToolCard key={tool.title} {...tool} />
            ))}
          </div>
        </section>

        <section className="mt-10 grid gap-4 lg:grid-cols-[1.2fr_0.8fr]">
          <div className="paper-card p-6 sm:p-8">
            <span className="ink-label">Plans</span>
            <h2 className="mt-2 text-2xl">Limits shown up front.</h2>
            <p className="mt-3 text-sm text-ink-700">
              Every cap is visible and enforced server-side. If capacity tightens,
              heavy tools pause first.
            </p>
            <div className="mt-5 grid gap-3 md:grid-cols-2">
              {planSnapshots.map((plan) => (
                <div key={plan.tier} className="surface-muted p-4">
                  <h3 className="text-base font-semibold text-ink-900">{plan.tier}</h3>
                  <p className="mt-1 text-xs text-ink-500">{plan.description}</p>
                  <ul className="mt-3 space-y-2 text-sm text-ink-700">
                    {plan.details.map((detail) => (
                      <li key={detail} className="flex items-center gap-2">
                        <span className="h-1.5 w-1.5 rounded-full bg-forest-600" />
                        {detail}
                      </li>
                    ))}
                  </ul>
                </div>
              ))}
            </div>
          </div>

          <div className="paper-card p-6 sm:p-8">
            <span className="ink-label">Workflow</span>
            <h2 className="mt-2 text-2xl">From upload to result.</h2>
            <div className="mt-4 space-y-4">
              {steps.map((step, index) => (
                <div key={step.title} className="flex items-start gap-3">
                  <div className="flex h-8 w-8 items-center justify-center rounded-lg border border-paper-300 bg-paper-100 text-xs font-semibold text-forest-700">
                    0{index + 1}
                  </div>
                  <div>
                    <h3 className="text-base font-semibold text-ink-900">{step.title}</h3>
                    <p className="text-sm text-ink-700">{step.copy}</p>
                  </div>
                </div>
              ))}
            </div>
            <div className="alert alert--success mt-6">
              All tools are free to use. Limits are for shared pool fairness.
            </div>
          </div>
        </section>
      </main>

      <footer className="mx-auto w-full max-w-6xl px-4 pb-12 sm:px-6">
        <div className="paper-card flex flex-col gap-4 p-6 sm:flex-row sm:items-center sm:justify-between">
          <div>
            <span className="ink-label">Open source</span>
            <p className="mt-1 text-sm text-ink-700">
              ZenPDF can be run locally when you need unlimited capacity and full control.
            </p>
          </div>
          <Link className="paper-button" href="/usage-capacity">
            Review usage limits
          </Link>
        </div>
      </footer>
    </div>
  );
}
