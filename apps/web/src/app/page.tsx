import Link from "next/link";

import SiteHeader from "@/components/SiteHeader";

const toolShelves = [
  {
    title: "Combine & Organize",
    description: "Merge, split, and re-sequence large dossiers without losing page fidelity.",
    items: ["Merge PDFs", "Split by ranges", "Reorder and rotate"],
    badge: "Standard",
  },
  {
    title: "Convert & Export",
    description: "Translate scans and images into polished PDFs or shareable JPGs.",
    items: ["Image to PDF", "PDF to JPG", "PDF compression"],
    badge: "Standard",
  },
  {
    title: "Protect & Repair",
    description: "Lock, unlock, and repair files with a gentle pass that keeps text intact.",
    items: ["Protect & unlock", "Repair damaged PDFs", "Remove pages"],
    badge: "Standard",
  },
  {
    title: "Annotate & Redact",
    description: "Mark up, crop, watermark, and redact with clarity-first controls.",
    items: ["Watermark", "Page numbers", "Redaction"],
    badge: "Standard",
  },
  {
    title: "Workflow Studio",
    description: "Save multi-step pipelines and re-run them in seconds.",
    items: ["Saved presets", "Batch queues", "Team templates"],
    badge: "Premium",
  },
  {
    title: "OCR Conversions",
    description: "Turn scans into Word, Excel, or PDF/A exports when capacity allows.",
    items: ["PDF to Word (OCR)", "PDF to Excel (OCR)", "PDF/A export"],
    badge: "Premium",
  },
];

const planSnapshots = [
  {
    tier: "ANON",
    description: "Quick one-off tasks with slim file and daily limits.",
    details: ["1 file per job", "10 MB per file", "3 jobs per day"],
  },
  {
    tier: "FREE ACCOUNT",
    description: "Signed-in users get more room plus saved job history.",
    details: ["3 files per job", "50 MB per file", "25 jobs per day"],
  },
  {
    tier: "PREMIUM",
    description: "Supporter mode unlocks higher limits and advanced tools.",
    details: ["10 files per job", "250 MB per file", "OCR conversions"],
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
    copy: "Download the results or save a reusable workflow for later.",
  },
];

const ToolCard = ({
  title,
  description,
  items,
  badge,
}: {
  title: string;
  description: string;
  items: string[];
  badge: string;
}) => (
  <div className="paper-card flex h-full flex-col gap-4 p-6">
    <div className="flex items-center justify-between">
      <div className="flex items-center gap-3">
        <span className="flex h-11 w-11 items-center justify-center rounded-[16px] border border-ink-900/10 bg-paper-100 text-ink-900">
          <svg
            width="20"
            height="20"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            strokeWidth="1.6"
            strokeLinecap="round"
            strokeLinejoin="round"
          >
            <path d="M6 3h8l4 4v14H6z" />
            <path d="M14 3v5h5" />
            <path d="M9 13h6" />
            <path d="M9 17h6" />
          </svg>
        </span>
        <div>
          <h3 className="text-xl font-display">{title}</h3>
          <p className="text-xs text-ink-500">{description}</p>
        </div>
      </div>
      <span className="rounded-full border border-ink-900/15 bg-rose-100 px-3 py-1 text-[0.65rem] uppercase tracking-[0.2em] text-ink-700">
        {badge}
      </span>
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
      <main className="relative z-10">
        <section className="mx-auto flex w-full max-w-6xl flex-col gap-12 px-6 pb-16 pt-6 lg:flex-row lg:items-center">
          <div className="flex-1 space-y-6 fade-up">
            <span className="ink-label">Open-source PDF workbench</span>
            <h1 className="text-4xl leading-tight md:text-5xl">
              Calm, precise PDF control with clear limits and a paper-first feel.
            </h1>
            <p className="max-w-xl text-lg text-ink-700">
              ZenPDF keeps your PDF workflows grounded with strict capacity
              controls, friendly error messages, and a serene dossier-inspired
              interface. Heavy lifting happens in a separate worker so the web
              app stays light.
            </p>
            <div className="flex flex-wrap gap-3">
              <button className="paper-button" type="button">
                Start with a file
              </button>
              <Link className="paper-button--ghost" href="/usage-capacity">
                See usage & capacity
              </Link>
            </div>
          </div>
          <div
            className="relative flex-1 fade-up"
            style={{ animationDelay: "0.12s" }}
          >
            <div className="absolute -left-6 top-6 hidden h-full w-full rounded-[34px] bg-paper-200/70 shadow-paper-lift lg:block" />
            <div className="relative paper-stack p-6">
              <div className="space-y-5">
                <div className="paper-card p-5">
                  <div className="flex items-center justify-between text-sm text-ink-700">
                    <span className="ink-label">Status</span>
                    <span className="rounded-full border border-forest-600/30 bg-sage-200 px-3 py-1 text-xs uppercase tracking-[0.2em] text-forest-700">
                      Available
                    </span>
                  </div>
                  <p className="mt-3 text-sm text-ink-700">
                    Capacity is monitored hourly to protect free-tier budgets.
                  </p>
                </div>
                <div className="paper-card p-5">
                  <div className="flex items-center justify-between text-sm text-ink-700">
                    <span className="ink-label">Active tools</span>
                    <span className="text-xs uppercase tracking-[0.2em] text-ink-500">
                      14 standard
                    </span>
                  </div>
                  <p className="mt-3 text-sm text-ink-700">
                    OCR and PDF/A toggle based on monthly budget health.
                  </p>
                </div>
                <div className="paper-card p-5">
                  <div className="flex items-center justify-between text-sm text-ink-700">
                    <span className="ink-label">Storage</span>
                    <span className="text-xs uppercase tracking-[0.2em] text-ink-500">
                      Convex Files
                    </span>
                  </div>
                  <p className="mt-3 text-sm text-ink-700">
                    Files expire automatically to keep storage lean.
                  </p>
                </div>
              </div>
            </div>
          </div>
        </section>

        <section className="mx-auto w-full max-w-6xl px-6 pb-16">
          <div className="flex flex-col gap-4">
            <div className="flex items-center justify-between">
              <div>
                <span className="ink-label">Tool shelf</span>
                <h2 className="text-3xl">Everyday PDF work, layered and calm.</h2>
              </div>
              <Link className="paper-button--ghost" href="/usage-capacity">
                Tool availability
              </Link>
            </div>
            <div className="ink-divider" />
          </div>
          <div className="mt-8 grid gap-6 md:grid-cols-2">
            {toolShelves.map((tool) => (
              <ToolCard key={tool.title} {...tool} />
            ))}
          </div>
        </section>

        <section className="mx-auto w-full max-w-6xl px-6 pb-16">
          <div className="grid gap-6 lg:grid-cols-[1.2fr_0.8fr]">
            <div className="paper-card p-8">
              <span className="ink-label">Plans</span>
              <h2 className="mt-3 text-3xl">Limits you can understand.</h2>
              <p className="mt-3 text-base text-ink-700">
                ZenPDF shows every cap up front and enforces it server-side.
                When capacity tightens, heavy tools pause first.
              </p>
              <div className="mt-6 grid gap-4 md:grid-cols-3">
                {planSnapshots.map((plan) => (
                  <div key={plan.tier} className="rounded-[22px] border border-ink-900/10 bg-paper-100 p-4">
                    <h3 className="text-lg font-display text-ink-900">{plan.tier}</h3>
                    <p className="mt-2 text-sm text-ink-700">{plan.description}</p>
                    <ul className="mt-3 space-y-2 text-xs text-ink-500">
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
            <div className="paper-card flex flex-col gap-4 p-8">
              <span className="ink-label">Workflow</span>
              <h2 className="text-2xl">From upload to delivery.</h2>
              <div className="space-y-4">
                {steps.map((step, index) => (
                  <div key={step.title} className="flex items-start gap-4">
                    <div className="flex h-10 w-10 items-center justify-center rounded-[16px] border border-ink-900/10 bg-paper-100 text-sm font-display text-ink-900">
                      0{index + 1}
                    </div>
                    <div>
                      <h3 className="text-lg font-display text-ink-900">
                        {step.title}
                      </h3>
                      <p className="text-sm text-ink-700">{step.copy}</p>
                    </div>
                  </div>
                ))}
              </div>
              <div className="mt-auto rounded-[20px] border border-forest-600/30 bg-sage-200/70 p-4 text-sm text-forest-700">
                Supporter mode hides the banner and expands batch limits.
              </div>
            </div>
          </div>
        </section>
      </main>

      <footer className="relative z-10 mx-auto w-full max-w-6xl px-6 pb-12">
        <div className="paper-card flex flex-col gap-4 p-6 md:flex-row md:items-center md:justify-between">
          <div>
            <span className="ink-label">Open source</span>
            <p className="mt-2 text-sm text-ink-700">
              ZenPDF is built to be run locally when you need unlimited
              capacity.
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
