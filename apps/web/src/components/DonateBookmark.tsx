"use client";

/* eslint-disable @next/next/no-img-element */
import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import { useThemeMode } from "@/components/ThemeModeProvider";

export default function DonateBookmark() {
  const [open, setOpen] = useState(false);
  const [showQr, setShowQr] = useState(true);
  const [showCardCheckout, setShowCardCheckout] = useState(false);
  const [generatedQrUrl, setGeneratedQrUrl] = useState("");
  const [isGeneratingQr, setIsGeneratingQr] = useState(false);
  const [qrGenerationError, setQrGenerationError] = useState("");
  const { theme } = useThemeMode();
  const triggerRef = useRef<HTMLButtonElement | null>(null);
  const modalRef = useRef<HTMLDivElement | null>(null);
  const lastFocusedElementRef = useRef<HTMLElement | null>(null);

  const defaultUpiName = "Rohan Patnaik";
  const defaultUpiId = "rohanpatnaik1997-1@okhdfcbank";
  // const defaultLightIcon = "/icons/chai-fab-light.png";
  // const defaultDarkIcon = "/icons/chai-fab-dark.png";
  const defaultLightIcon = "/icons/chai.png";
  const defaultDarkIcon = "/icons/chai.png";

  const upiId = (process.env.NEXT_PUBLIC_DONATE_UPI_ID ?? defaultUpiId).trim();
  const upiName = (process.env.NEXT_PUBLIC_DONATE_UPI_NAME ?? defaultUpiName).trim();
  const upiNote = (process.env.NEXT_PUBLIC_DONATE_UPI_NOTE ?? "Support ZenPDF").trim();
  const qrUrl = (process.env.NEXT_PUBLIC_DONATE_UPI_QR_URL ?? "").trim();
  const cardEmbedUrl = (process.env.NEXT_PUBLIC_DONATE_CARD_EMBED_URL ?? "").trim();
  const lightIcon = (process.env.NEXT_PUBLIC_DONATE_ICON_LIGHT ?? defaultLightIcon).trim();
  const darkIcon = (process.env.NEXT_PUBLIC_DONATE_ICON_DARK ?? defaultDarkIcon).trim();

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

  const resolvedQrUrl = qrUrl || generatedQrUrl;

  const resolvedCardEmbedUrl = useMemo(() => {
    if (!cardEmbedUrl) {
      return "";
    }

    if (cardEmbedUrl.startsWith("/")) {
      return cardEmbedUrl;
    }

    try {
      const parsed = new URL(cardEmbedUrl);
      const isLocalHttp = parsed.protocol === "http:" && parsed.hostname === "localhost";
      if (parsed.protocol !== "https:" && !isLocalHttp) {
        return "";
      }
      return parsed.toString();
    } catch {
      return "";
    }
  }, [cardEmbedUrl]);

  const hasExternalQrUrl = Boolean(qrUrl);
  const canShowQr = Boolean(qrUrl || upiUri);

  useEffect(() => {
    setGeneratedQrUrl("");
    setQrGenerationError("");
    setIsGeneratingQr(false);
  }, [qrUrl, upiUri]);

  useEffect(() => {
    if (!open || !showQr || hasExternalQrUrl || !upiUri || generatedQrUrl || isGeneratingQr) {
      return;
    }

    let cancelled = false;
    setIsGeneratingQr(true);
    setQrGenerationError("");

    void import("qrcode")
      .then((qrModule) =>
        qrModule.toDataURL(upiUri, {
          width: 360,
          margin: 1,
          errorCorrectionLevel: "M",
        }),
      )
      .then((dataUrl) => {
        if (!cancelled) {
          setGeneratedQrUrl(dataUrl);
        }
      })
      .catch(() => {
        if (!cancelled) {
          setQrGenerationError("Could not generate a QR code right now.");
        }
      })
      .finally(() => {
        if (!cancelled) {
          setIsGeneratingQr(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [generatedQrUrl, hasExternalQrUrl, isGeneratingQr, open, showQr, upiUri]);

  const handleOpen = useCallback(() => {
    if (typeof document !== "undefined" && document.activeElement instanceof HTMLElement) {
      lastFocusedElementRef.current = document.activeElement;
    }
    setShowQr(true);
    setShowCardCheckout(false);
    setOpen(true);
  }, []);

  const handleClose = useCallback(() => {
    setShowQr(true);
    setShowCardCheckout(false);
    setOpen(false);
  }, []);

  useEffect(() => {
    if (!open) {
      return;
    }

    const modalElement = modalRef.current;
    const triggerElement = triggerRef.current;
    if (!modalElement) {
      return;
    }

    const focusableSelector = [
      "a[href]",
      "button:not([disabled])",
      "input:not([disabled])",
      "select:not([disabled])",
      "textarea:not([disabled])",
      '[tabindex]:not([tabindex="-1"])',
    ].join(",");

    const frameId = window.requestAnimationFrame(() => {
      const focusableElements = modalElement.querySelectorAll<HTMLElement>(focusableSelector);
      focusableElements[0]?.focus();
    });

    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        event.preventDefault();
        handleClose();
        return;
      }

      if (event.key !== "Tab") {
        return;
      }

      const focusableElements = Array.from(
        modalElement.querySelectorAll<HTMLElement>(focusableSelector),
      ).filter((element) => !element.hasAttribute("disabled"));

      if (focusableElements.length === 0) {
        event.preventDefault();
        modalElement.focus();
        return;
      }

      const firstElement = focusableElements[0];
      const lastElement = focusableElements[focusableElements.length - 1];
      const activeElement = document.activeElement as HTMLElement | null;

      if (event.shiftKey && (activeElement === firstElement || !modalElement.contains(activeElement))) {
        event.preventDefault();
        lastElement.focus();
        return;
      }

      if (!event.shiftKey && activeElement === lastElement) {
        event.preventDefault();
        firstElement.focus();
      }
    };

    document.addEventListener("keydown", handleKeyDown);

    return () => {
      window.cancelAnimationFrame(frameId);
      document.removeEventListener("keydown", handleKeyDown);

      const restoreTarget = lastFocusedElementRef.current ?? triggerElement;
      restoreTarget?.focus();
      lastFocusedElementRef.current = null;
    };
  }, [handleClose, open]);

  const handleToggle = () => {
    if (open) {
      handleClose();
      return;
    }
    handleOpen();
  };

  return (
    <>
      <button
        ref={triggerRef}
        type="button"
        className={`donate-fab ${iconUrl ? "donate-fab--with-image" : ""}`}
        onClick={handleToggle}
        aria-label="Support ZenPDF"
        aria-expanded={open}
      >
        <span className="donate-fab-hint" aria-hidden="true">
          Wanna support me?
        </span>
        {iconUrl ? (
          <img
            src={iconUrl}
            alt=""
            className="donate-fab-icon-image"
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
          className="fixed inset-0 z-50 flex items-center justify-center bg-ink-900/45 backdrop-blur-sm px-4 py-6 sm:py-8"
          role="dialog"
          aria-modal="true"
          aria-labelledby="support-title"
          onClick={(event) => {
            if (event.target === event.currentTarget) {
              handleClose();
            }
          }}
        >
          <div
            ref={modalRef}
            tabIndex={-1}
            className="paper-card relative w-full max-w-md p-6 text-center"
          >
            <button
              type="button"
              className="paper-button--ghost donate-close-button absolute right-6 top-6 w-auto px-4 py-2"
              onClick={handleClose}
            >
              Close
            </button>

            <div className="mx-auto max-w-[30rem]">
              <span className="ink-label block">Support ZenPDF</span>
              <h2 id="support-title" className="mt-2 text-2xl">
                Buy me a chai
              </h2>
              <p className="mt-2 text-sm text-ink-700">
                <span className="block">ZenPDF is free and open source.</span>
                <span className="mt-0.5 block">
                  Support goes directly to the creator with no fees to OnlyChai.
                </span>
              </p>
            </div>

            <div className="surface-muted mt-4 p-4 text-center">
              <div className="ink-label">UPI ID</div>
              <div className="mt-1 break-all text-sm text-ink-900">
                {upiId || "Set NEXT_PUBLIC_DONATE_UPI_ID"}
              </div>
            </div>

            {showQr && (resolvedQrUrl || isGeneratingQr || qrGenerationError) && (
              <div className="surface-muted mt-4 p-3 text-center">
                {resolvedQrUrl ? (
                  <img
                    src={resolvedQrUrl}
                    alt="UPI QR code"
                    width={260}
                    height={260}
                    className="mx-auto h-56 w-56 rounded-md object-contain"
                  />
                ) : null}
                {isGeneratingQr ? (
                  <p className="mx-auto my-4 w-fit rounded-md bg-paper-50 px-3 py-2 text-sm text-ink-700">
                    Generating QR code...
                  </p>
                ) : null}
                {qrGenerationError ? <p className="text-sm text-ink-700">{qrGenerationError}</p> : null}
              </div>
            )}

            <div className="mt-4 flex flex-wrap justify-center gap-2">
              <button
                type="button"
                className="paper-button"
                onClick={() => setShowQr(true)}
                disabled={!canShowQr}
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
                Open UPI App
              </a>
              <button
                type="button"
                className="paper-button--ghost"
                aria-pressed={showCardCheckout}
                onClick={() => setShowCardCheckout((current) => !current)}
              >
                Pay by card
              </button>
            </div>
            <p className="mt-2 text-xs text-ink-500">
              {hasExternalQrUrl
                ? "QR is loaded from your configured image URL."
                : "QR is generated locally on your device."}
            </p>

            {showCardCheckout && (
              <div className="surface-muted mt-4 p-3 text-center">
                {resolvedCardEmbedUrl ? (
                  <iframe
                    src={resolvedCardEmbedUrl}
                    title="Donate with card"
                    className="donate-card-frame"
                    loading="lazy"
                    allow="payment *; clipboard-write"
                  />
                ) : (
                  <p className="text-sm text-ink-700">Card checkout is unavailable right now.</p>
                )}
              </div>
            )}
          </div>
        </div>
      )}
    </>
  );
}
