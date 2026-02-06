import Link from "next/link";

import SiteHeader from "@/components/SiteHeader";

type SetupStep = {
  title: string;
  body: string;
  commands?: string[];
};

const setupSteps: SetupStep[] = [
  {
    title: "1. Install prerequisites",
    body: "Install Node.js 20+, Python 3.11+, and Docker Desktop. You also need a Clerk app and a Convex project.",
  },
  {
    title: "2. Configure environment files",
    body: "Copy env templates and set values. Keep worker token identical in web and worker env files.",
    commands: [
      "cp apps/web/.env.example apps/web/.env.local",
      "cp apps/worker/.env.example apps/worker/.env",
    ],
  },
  {
    title: "3. Install dependencies",
    body: "Install web and worker dependencies once before first run.",
    commands: [
      "cd apps/web && npm install",
      "cd ../worker && python3 -m pip install -r requirements.txt",
    ],
  },
  {
    title: "4. Start the full local stack (recommended)",
    body: "Run the helper script from repo root. It starts Convex, web, and worker together.",
    commands: ["./scripts/dev.sh"],
  },
  {
    title: "5. Manual start option (three terminals)",
    body: "Use this if you prefer explicit control over each service.",
    commands: [
      "cd apps/web && npx convex dev",
      "cd apps/web && npm run dev -- --webpack",
      "cd apps/worker && set -a && . .env && set +a && python3 main.py",
    ],
  },
  {
    title: "6. Verify the stack",
    body: "Open the app and run a simple tool to confirm queue and worker flow are healthy.",
    commands: [
      "Web app: http://localhost:3000",
      "Convex dashboard: https://dashboard.convex.dev",
    ],
  },
  {
    title: "7. Optional Docker run",
    body: "You can run web + worker in Docker with local env files.",
    commands: ["docker compose up --build"],
  },
];

export default function SelfHostPage() {
  return (
    <div className="relative">
      <SiteHeader />
      <main className="mx-auto w-full max-w-6xl px-4 pb-14 pt-5 sm:px-6">
        <section className="paper-card p-8">
          <span className="ink-label">Self-hosting Guide</span>
          <h1 className="mt-2 text-3xl">Run ZenPDF locally, step by step.</h1>
          <p className="mt-3 max-w-3xl text-sm text-ink-700">
            This setup runs Convex, the Next.js web app, and the Python worker on your own machine.
            Follow steps in order for the fastest path to a working local stack.
          </p>

          <div className="mt-6 space-y-4">
            {setupSteps.map((step) => (
              <section key={step.title} className="surface-muted p-4">
                <h2 className="text-base font-semibold text-ink-900">{step.title}</h2>
                <p className="mt-2 text-sm text-ink-700">{step.body}</p>
                {step.commands ? (
                  <pre className="mt-3 overflow-x-auto rounded-lg border border-paper-300 bg-paper-50 p-3 text-xs text-ink-900">
                    <code>{step.commands.join("\n")}</code>
                  </pre>
                ) : null}
              </section>
            ))}
          </div>

          <div className="mt-6 flex flex-wrap gap-3">
            <Link className="paper-button" href="/tools">
              Open tools
            </Link>
            <Link className="paper-button--ghost" href="/usage-capacity">
              Back to usage & capacity
            </Link>
          </div>
        </section>
      </main>
    </div>
  );
}
