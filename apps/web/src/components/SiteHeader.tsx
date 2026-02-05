"use client";

import Link from "next/link";
import { SignInButton, SignedIn, SignedOut, UserButton } from "@clerk/nextjs";
import { useQuery } from "convex/react";

import { api } from "@/lib/convex";

export default function SiteHeader() {
  const viewer = useQuery(api.users.getViewer, {});
  const showSupporter = viewer ? !viewer.adsFree : false;

  return (
    <header className="relative z-20">
      <div className="wood-nav mx-auto flex w-full max-w-6xl items-center justify-between px-6 py-4">
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
      {showSupporter && (
        <div className="mx-auto w-full max-w-6xl px-6 pb-4">
          <div className="flex flex-wrap items-center justify-between gap-4 rounded-[20px] border border-forest-600/30 bg-sage-200/70 px-5 py-4 text-forest-700">
            <div>
              <span className="ink-label">Supporter mode</span>
              <p className="mt-1 max-w-2xl text-sm">
                Unlock larger batches and remove this banner when supporter
                mode is enabled.
              </p>
            </div>
            <Link className="paper-button--ghost text-xs" href="/usage-capacity">
              Review premium limits
            </Link>
          </div>
        </div>
      )}
    </header>
  );
}
