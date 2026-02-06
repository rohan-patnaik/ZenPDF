"use client";

import { useMemo, useState } from "react";
import Image from "next/image";

type CopyState = "idle" | "done" | "failed";

export default function DonateBookmark() {
  const [open, setOpen] = useState(false);
  const [copyState, setCopyState] = useState<CopyState>("idle");

  const upiId = (process.env.NEXT_PUBLIC_DONATE_UPI_ID ?? "").trim();
  const upiName = (process.env.NEXT_PUBLIC_DONATE_UPI_NAME ?? "ZenPDF").trim();
  const upiNote = (process.env.NEXT_PUBLIC_DONATE_UPI_NOTE ?? "Support ZenPDF").trim();
  const qrUrl = (process.env.NEXT_PUBLIC_DONATE_UPI_QR_URL ?? "").trim();

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
        className="fixed right-0 top-1/2 z-40 -translate-y-1/2 rounded-l-lg border border-r-0 border-forest-700 bg-forest-600 px-2.5 py-3 text-[0.62rem] font-semibold uppercase tracking-[0.16em] text-white shadow-paper"
        onClick={() => setOpen(true)}
        aria-label="Support ZenPDF"
      >
        Support
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
                  Buy me a coffee
                </h2>
                <p className="mt-2 text-sm text-ink-700">
                  ZenPDF is free and open source. Donations help cover infrastructure costs.
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
              <div className="ink-label">UPI ID</div>
              <div className="mt-1 break-all text-sm text-ink-900">
                {upiId || "Set NEXT_PUBLIC_DONATE_UPI_ID"}
              </div>
            </div>

            <div className="mt-4 flex flex-wrap gap-2">
              <button type="button" className="paper-button" onClick={copyUpiId}>
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
