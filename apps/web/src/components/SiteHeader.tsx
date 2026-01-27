"use client";

import Link from "next/link";
import { SignInButton, SignedIn, SignedOut, UserButton } from "@clerk/nextjs";

export default function SiteHeader() {
  return (
    <header className="relative z-20">
      <div className="mx-auto flex w-full max-w-6xl items-center justify-between px-6 py-6">
        <Link href="/" className="flex items-center gap-3">
          <div className="flex h-11 w-11 items-center justify-center rounded-[18px] border border-ink-900/15 bg-paper-100 text-lg font-display text-ink-900 shadow-paper">
            Z
          </div>
          <div>
            <div className="text-lg font-display text-ink-900">ZenPDF</div>
            <div className="text-[0.6rem] uppercase tracking-[0.32em] text-ink-500">
              Dossier Toolkit
            </div>
          </div>
        </Link>
        <div className="flex items-center gap-3 text-sm">
          <Link className="paper-button--ghost" href="/tools">
            Tools
          </Link>
          <Link className="paper-button--ghost" href="/usage-capacity">
            Usage & Capacity
          </Link>
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
        </div>
      </div>
    </header>
  );
}
