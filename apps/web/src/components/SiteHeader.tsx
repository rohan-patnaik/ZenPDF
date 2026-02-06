"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { SignInButton, SignedIn, SignedOut, UserButton } from "@clerk/nextjs";

import ThemeToggle from "@/components/ThemeToggle";

export default function SiteHeader() {
  const pathname = usePathname();
  const isTools = pathname?.startsWith("/tools") ?? false;
  const isUsage = pathname?.startsWith("/usage-capacity") ?? false;

  return (
    <header className="sticky top-0 z-30 px-4 pt-4 sm:px-6">
      <div className="wood-nav mx-auto flex w-full max-w-6xl flex-wrap items-center justify-between gap-3 px-4 py-3 sm:px-6">
        <Link href="/" className="flex items-center gap-3">
          <div className="flex h-10 w-10 items-center justify-center rounded-xl border border-paper-300 bg-paper-100 text-sm font-display font-bold text-forest-700">
            Z
          </div>
          <div>
            <div className="text-base font-display font-semibold text-ink-900">ZenPDF</div>
            <div className="text-[0.65rem] uppercase tracking-[0.16em] text-ink-500">
              Open PDF Workbench
            </div>
          </div>
        </Link>

        <nav className="flex flex-wrap items-center gap-2 text-sm" aria-label="Primary">
          <Link className={`nav-link ${isTools ? "nav-link--active" : ""}`} href="/tools">
            Tools
          </Link>
          <Link
            className={`nav-link ${isUsage ? "nav-link--active" : ""}`}
            href="/usage-capacity"
          >
            Usage & Capacity
          </Link>
          <ThemeToggle />
          <SignedOut>
            <SignInButton mode="modal">
              <button className="paper-button" type="button">
                Sign in with Google
              </button>
            </SignInButton>
          </SignedOut>
          <SignedIn>
            <UserButton afterSignOutUrl="/" />
          </SignedIn>
        </nav>
      </div>
    </header>
  );
}
