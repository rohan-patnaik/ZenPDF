"use client";

import Link from "next/link";
import { useMutation } from "convex/react";
import { FormEvent, useCallback, useEffect, useRef, useState } from "react";

import { getOrCreateAnonId } from "@/lib/anon-id";
import { api } from "@/lib/convex";

const HEADING_MIN_CHARS = 4;
const HEADING_MAX_CHARS = 120;
const MESSAGE_MIN_CHARS = 12;
const MESSAGE_MAX_CHARS = 2000;

const normalizeHeading = (value: string) => value.replace(/\s+/g, " ").trim();

const getErrorMessage = (error: unknown) => {
  if (error && typeof error === "object") {
    const maybeError = error as {
      data?: { message?: string };
      message?: string;
    };

    if (maybeError.data?.message) {
      return maybeError.data.message;
    }

    if (maybeError.message) {
      return maybeError.message;
    }
  }

  return "Could not submit feedback right now. Please try again.";
};

type FeedbackWidgetProps = {
  compact?: boolean;
  className?: string;
};

export default function FeedbackWidget({ compact = false, className = "" }: FeedbackWidgetProps) {
  const createFeedback = useMutation(api.feedback.createFeedback);

  const [open, setOpen] = useState(false);
  const [heading, setHeading] = useState("");
  const [message, setMessage] = useState("");
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [submitError, setSubmitError] = useState("");
  const [submitSuccess, setSubmitSuccess] = useState("");

  const headingRef = useRef<HTMLInputElement | null>(null);
  const triggerClassName = `feedback-trigger${compact ? " feedback-trigger--compact" : ""}${className ? ` ${className}` : ""}`;

  const handleClose = useCallback(() => {
    setOpen(false);
    setSubmitError("");
  }, []);

  useEffect(() => {
    if (!open) {
      return;
    }

    const frame = window.requestAnimationFrame(() => {
      headingRef.current?.focus();
    });

    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        event.preventDefault();
        handleClose();
      }
    };

    document.addEventListener("keydown", onKeyDown);
    return () => {
      window.cancelAnimationFrame(frame);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [handleClose, open]);

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();

    const safeHeading = normalizeHeading(heading);
    const safeMessage = message.trim();

    if (
      safeHeading.length < HEADING_MIN_CHARS ||
      safeHeading.length > HEADING_MAX_CHARS
    ) {
      setSubmitError(
        `Heading must be ${HEADING_MIN_CHARS}-${HEADING_MAX_CHARS} characters.`,
      );
      return;
    }

    if (
      safeMessage.length < MESSAGE_MIN_CHARS ||
      safeMessage.length > MESSAGE_MAX_CHARS
    ) {
      setSubmitError(
        `Message must be ${MESSAGE_MIN_CHARS}-${MESSAGE_MAX_CHARS} characters.`,
      );
      return;
    }

    setIsSubmitting(true);
    setSubmitError("");
    setSubmitSuccess("");

    try {
      const anonId = getOrCreateAnonId() ?? undefined;
      await createFeedback({
        heading: safeHeading,
        message: safeMessage,
        anonId,
      });

      setHeading("");
      setMessage("");
      setSubmitSuccess("Thanks. Your feedback is now in the board.");
    } catch (error) {
      setSubmitError(getErrorMessage(error));
    } finally {
      setIsSubmitting(false);
    }
  };

  return (
    <>
      <button
        type="button"
        className={triggerClassName}
        onClick={() => {
          setOpen(true);
          setSubmitError("");
          setSubmitSuccess("");
        }}
        aria-label="Open feedback form"
        aria-expanded={open}
      >
        <span>Feedback</span>
      </button>

      {open ? (
        <div
          className="feedback-overlay"
          role="dialog"
          aria-modal="true"
          aria-labelledby="feedback-modal-title"
          onClick={(event) => {
            if (event.currentTarget === event.target) {
              handleClose();
            }
          }}
        >
          <div className="paper-card feedback-modal">
            <button
              type="button"
              className="paper-button--ghost feedback-modal-close"
              onClick={handleClose}
            >
              Close
            </button>

            <div className="pr-24">
              <span className="ink-label">Share feedback</span>
              <h2 id="feedback-modal-title" className="mt-2 text-2xl">
                Help improve ZenPDF
              </h2>
              <p className="mt-2 text-sm text-ink-700">
                Report issues or product ideas. Open items stay visible to everyone.
              </p>
            </div>

            <form className="mt-5 space-y-4" onSubmit={submit}>
              <label className="field-label" htmlFor="feedback-heading">
                Heading
              </label>
              <input
                ref={headingRef}
                id="feedback-heading"
                className="field-input"
                value={heading}
                onChange={(event) => setHeading(event.target.value)}
                placeholder="Short summary"
                required
                minLength={HEADING_MIN_CHARS}
                maxLength={HEADING_MAX_CHARS}
              />

              <label className="field-label" htmlFor="feedback-message">
                Message
              </label>
              <textarea
                id="feedback-message"
                className="field-input min-h-32 resize-y"
                value={message}
                onChange={(event) => setMessage(event.target.value)}
                placeholder="Describe the issue, expected behavior, and context."
                required
                minLength={MESSAGE_MIN_CHARS}
                maxLength={MESSAGE_MAX_CHARS}
              />

              <div className="flex flex-wrap items-center gap-2">
                <button type="submit" className="paper-button w-auto" disabled={isSubmitting}>
                  {isSubmitting ? "Submitting..." : "Submit feedback"}
                </button>
                <Link href="/feedback" className="paper-button--ghost w-auto" onClick={handleClose}>
                  View feedback board
                </Link>
              </div>
            </form>

            {submitSuccess ? <p className="alert alert--success mt-3">{submitSuccess}</p> : null}
            {submitError ? <p className="alert alert--error mt-3">{submitError}</p> : null}
          </div>
        </div>
      ) : null}
    </>
  );
}
