"use client";

import { useMemo, useState } from "react";
import Image from "next/image";

type CopyState = "idle" | "done" | "failed";

export default function DonateBookmark() {
  const [open, setOpen] = useState(false);
  const [copyState, setCopyState] = useState<CopyState>("idle");

  const defaultUpiName = "Rohan Patnaik";
  const defaultUpiId = "rohanpatnaik1997-1@okhdfcbank";

  const upiId = (process.env.NEXT_PUBLIC_DONATE_UPI_ID ?? defaultUpiId).trim();
  const upiName = (process.env.NEXT_PUBLIC_DONATE_UPI_NAME ?? defaultUpiName).trim();
  const upiNote = (process.env.NEXT_PUBLIC_DONATE_UPI_NOTE ?? "Support ZenPDF").trim();
  const qrUrl = (process.env.NEXT_PUBLIC_DONATE_UPI_QR_URL ?? "").trim();
  const onlyChaiEnvUrl = (process.env.NEXT_PUBLIC_DONATE_ONLYCHAI_URL ?? "").trim();

  const upiUri = useMemo(() => {
    if (!upiId) {
      return "";
    }
    const params = new URLSearchParams({
      pa: upiId,
      pn: upiName,
      tn: upiNote,
      cu: "INR",
    });
    return `upi://pay?${params.toString()}`;
  }, [upiId, upiName, upiNote]);

  const onlyChaiUrl = useMemo(() => {
    if (onlyChaiEnvUrl) {
      return onlyChaiEnvUrl;
    }
    if (!upiId || !upiName) {
      return "https://onlychai.neocities.org/";
    }
    const params = new URLSearchParams({
      name: upiName,
      upi: upiId,
    });
    return `https://onlychai.neocities.org/support.html?${params.toString()}`;
  }, [onlyChaiEnvUrl, upiId, upiName]);

  const copyUpiId = async () => {
    if (!upiId) {
      setCopyState("failed");
      return;
    }
    try {
      await navigator.clipboard.writeText(upiId);
      setCopyState("done");
      window.setTimeout(() => setCopyState("idle"), 2000);
    } catch {
      setCopyState("failed");
      window.setTimeout(() => setCopyState("idle"), 2000);
    }
  };

  return (
    <>
      <button
        type="button"
        className="donate-fab"
        onClick={() => setOpen(true)}
        aria-label="Support ZenPDF"
      >
        <span className="chai-steam" aria-hidden="true">
          <span className="chai-steam-line chai-steam-line--1"></span>
          <span className="chai-steam-line chai-steam-line--2"></span>
        </span>
        <svg
          aria-hidden="true"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="1.8"
          strokeLinecap="round"
          strokeLinejoin="round"
        >
          <path d="M5.5 11.5h9.5v3.6a3.2 3.2 0 0 1-3.2 3.2H8.8a3.3 3.3 0 0 1-3.3-3.3v-3.5Z" />
          <path d="M15 12.5h1.7a1.8 1.8 0 0 1 0 3.6H15" />
          <path d="M4.6 19h11.8" />
        </svg>
      </button>

      {open && (
        <div
          className="fixed inset-0 z-50 flex items-center justify-center bg-ink-900/45 px-4"
          role="dialog"
          aria-modal="true"
          aria-labelledby="support-title"
        >
          <div className="paper-card w-full max-w-md p-6">
            <div className="flex items-start justify-between gap-3">
              <div>
                <span className="ink-label">Support ZenPDF</span>
                <h2 id="support-title" className="mt-2 text-2xl">
                  Buy me a chai
                </h2>
                <p className="mt-2 text-sm text-ink-700">
                  ZenPDF is free and open source. Support goes directly via UPI with no platform
                  cut through OnlyChai.
                </p>
              </div>
              <button
                type="button"
                className="paper-button--ghost"
                onClick={() => setOpen(false)}
              >
                Close
              </button>
            </div>

            {qrUrl ? (
              <div className="surface-muted mt-4 p-3">
                <Image
                  src={qrUrl}
                  alt="UPI QR code"
                  width={224}
                  height={224}
                  className="mx-auto h-56 w-56 rounded-md object-contain"
                />
              </div>
            ) : (
              <div className="alert mt-4">
                Set `NEXT_PUBLIC_DONATE_UPI_QR_URL` to show a scannable QR image.
              </div>
            )}

            <div className="surface-muted mt-4 p-4">
              <div className="ink-label">OnlyChai page</div>
              <a
                href={onlyChaiUrl}
                target="_blank"
                rel="noreferrer"
                className="mt-1 block break-all text-sm text-forest-700 hover:underline"
              >
                {onlyChaiUrl}
              </a>
            </div>

            <div className="surface-muted mt-4 p-4">
              <div className="ink-label">UPI ID</div>
              <div className="mt-1 break-all text-sm text-ink-900">
                {upiId || "Set NEXT_PUBLIC_DONATE_UPI_ID"}
              </div>
            </div>

            <div className="mt-4 flex flex-wrap gap-2">
              <a className="paper-button" href={onlyChaiUrl} target="_blank" rel="noreferrer">
                Open OnlyChai
              </a>
              <button type="button" className="paper-button--ghost" onClick={copyUpiId}>
                {copyState === "done"
                  ? "Copied"
                  : copyState === "failed"
                    ? "Copy failed"
                    : "Copy UPI ID"}
              </button>
              {upiUri && (
                <a className="paper-button--ghost" href={upiUri}>
                  Open UPI app
                </a>
              )}
            </div>
          </div>
        </div>
      )}
    </>
  );
}
