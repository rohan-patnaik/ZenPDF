"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { SignInButton, SignedIn, SignedOut, UserButton } from "@clerk/nextjs";

import RuntimeEnvironmentIndicator from "@/components/RuntimeEnvironmentIndicator";
import ThemeToggle from "@/components/ThemeToggle";

export default function SiteHeader() {
  const pathname = usePathname();
  const isTools = pathname?.startsWith("/tools") ?? false;
  const isUsage = pathname?.startsWith("/usage-capacity") ?? false;
  const navLinks = (
    <>
      <Link className={`nav-link ${isTools ? "nav-link--active" : ""}`} href="/tools">
        Tools
      </Link>
      <Link
        className={`nav-link ${isUsage ? "nav-link--active" : ""}`}
        href="/usage-capacity"
      >
        Usage & Capacity
      </Link>
    </>
  );

  return (
    <header className="sticky top-0 z-30 px-2 pt-2 sm:px-4 sm:pt-3 lg:px-6 lg:pt-4">
      <div className="wood-nav mx-auto w-full max-w-6xl px-3 py-2.5 sm:px-4 sm:py-2.5 lg:px-6 lg:py-3">
        <div className="flex min-h-10 w-full items-center gap-2.5">
          <Link href="/" className="flex min-w-0 flex-1 items-center gap-2 sm:gap-3">
            <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-xl border border-paper-300 bg-paper-100 text-sm font-display font-bold text-forest-700 sm:h-10 sm:w-10">
              Z
            </div>
            <div className="min-w-0">
              <div className="truncate text-sm font-display font-semibold text-ink-900 sm:text-base">
                ZenPDF
              </div>
              <div className="hidden text-[0.65rem] uppercase tracking-[0.16em] text-ink-500 lg:block">
                Open PDF Workbench
              </div>
            </div>
          </Link>

          <div className="flex shrink-0 items-center gap-1.5 pr-0.5 lg:hidden">
            <ThemeToggle />
            <SignedOut>
              <SignInButton mode="modal">
                <button className="paper-button--ghost w-auto px-3 py-2 text-xs font-semibold" type="button">
                  Sign in
                </button>
              </SignInButton>
            </SignedOut>
            <SignedIn>
              <UserButton afterSignOutUrl="/" />
            </SignedIn>
          </div>

          <nav
            className="hidden min-w-0 flex-1 items-center justify-end gap-2 text-sm lg:flex lg:flex-wrap"
            aria-label="Primary"
          >
            {navLinks}
            <RuntimeEnvironmentIndicator />
            <SignedOut>
              <SignInButton mode="modal">
                <button className="paper-button hidden lg:inline-flex" type="button">
                  Sign in with Google
                </button>
              </SignInButton>
            </SignedOut>
            <div className="ml-1 flex shrink-0 items-center gap-2">
              <ThemeToggle />
              <SignedIn>
                <UserButton afterSignOutUrl="/" />
              </SignedIn>
            </div>
          </nav>
        </div>

        <div className="mt-2 flex items-center gap-2 lg:hidden">
          <nav
            className="mobile-scroll-row flex min-w-0 flex-1 items-center gap-2 overflow-x-auto pb-1 text-sm"
            aria-label="Primary"
          >
            {navLinks}
          </nav>
          <RuntimeEnvironmentIndicator compact />
        </div>
      </div>
    </header>
  );
}
