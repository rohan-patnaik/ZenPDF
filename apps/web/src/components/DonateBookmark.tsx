"use client";

/* eslint-disable @next/next/no-img-element */
import { useMemo, useState } from "react";

import { useThemeMode } from "@/components/ThemeModeProvider";

export default function DonateBookmark() {
  const [open, setOpen] = useState(false);
  const [showQr, setShowQr] = useState(false);
  const { theme } = useThemeMode();

  const defaultUpiName = "Rohan Patnaik";
  const defaultUpiId = "rohanpatnaik1997-1@okhdfcbank";

  const upiId = (process.env.NEXT_PUBLIC_DONATE_UPI_ID ?? defaultUpiId).trim();
  const upiName = (process.env.NEXT_PUBLIC_DONATE_UPI_NAME ?? defaultUpiName).trim();
  const upiNote = (process.env.NEXT_PUBLIC_DONATE_UPI_NOTE ?? "Support ZenPDF").trim();
  const qrUrl = (process.env.NEXT_PUBLIC_DONATE_UPI_QR_URL ?? "").trim();
  const lightIcon = (process.env.NEXT_PUBLIC_DONATE_ICON_LIGHT ?? "").trim();
  const darkIcon = (process.env.NEXT_PUBLIC_DONATE_ICON_DARK ?? "").trim();

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

  const iconUrl = useMemo(() => {
    if (theme === "dark") {
      return darkIcon || lightIcon;
    }
    return lightIcon || darkIcon;
  }, [darkIcon, lightIcon, theme]);

  const resolvedQrUrl = useMemo(() => {
    if (qrUrl) {
      return qrUrl;
    }
    if (!upiUri) {
      return "";
    }
    const encoded = encodeURIComponent(upiUri);
    return `https://quickchart.io/qr?size=360&text=${encoded}`;
  }, [qrUrl, upiUri]);

  const handleOpen = () => {
    setShowQr(false);
    setOpen(true);
  };

  const handleClose = () => {
    setShowQr(false);
    setOpen(false);
  };

  return (
    <>
      <button
        type="button"
        className="donate-fab"
        onClick={handleOpen}
        aria-label="Support ZenPDF"
      >
        {iconUrl ? (
          <span
            className="donate-fab-icon-image"
            style={{ backgroundImage: `url("${iconUrl}")` }}
            aria-hidden="true"
          />
        ) : (
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
        )}
      </button>

      {open && (
        <div
          className="fixed inset-0 z-50 flex items-start justify-center bg-ink-900/45 px-4 pb-6 pt-20 pointer-events-none sm:pt-24"
          role="dialog"
          aria-modal="true"
          aria-labelledby="support-title"
        >
          <div className="paper-card pointer-events-auto w-full max-w-md p-6">
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
                onClick={handleClose}
              >
                Close
              </button>
            </div>

            <div className="surface-muted mt-4 p-4">
              <div className="ink-label">UPI ID</div>
              <div className="mt-1 break-all text-sm text-ink-900">
                {upiId || "Set NEXT_PUBLIC_DONATE_UPI_ID"}
              </div>
            </div>

            {showQr && resolvedQrUrl && (
              <div className="surface-muted mt-4 p-3">
                <img
                  src={resolvedQrUrl}
                  alt="UPI QR code"
                  width={260}
                  height={260}
                  className="mx-auto h-56 w-56 rounded-md object-contain"
                />
              </div>
            )}

            <div className="mt-4 flex flex-wrap gap-2">
              <button
                type="button"
                className="paper-button"
                onClick={() => setShowQr(true)}
                disabled={!resolvedQrUrl}
              >
                Show UPI QR
              </button>
              <a
                className="paper-button--ghost"
                href={upiUri || "#"}
                onClick={(event) => {
                  if (!upiUri) {
                    event.preventDefault();
                  }
                }}
              >
                Open any UPI
              </a>
            </div>
          </div>
        </div>
      )}
    </>
  );
}
