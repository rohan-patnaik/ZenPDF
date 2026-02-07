"use client";

import { useSyncExternalStore } from "react";

type RuntimeMode = "local" | "hosted";

const PRIVATE_IPV4_PATTERN =
  /^(127\.\d{1,3}\.\d{1,3}\.\d{1,3}|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(1[6-9]|2\d|3[0-1])\.\d{1,3}\.\d{1,3}|0\.0\.0\.0)$/;

function resolveRuntimeMode(hostname: string | null): RuntimeMode {
  if (!hostname) {
    return "hosted";
  }

  const normalized = hostname.toLowerCase();
  if (
    normalized === "localhost" ||
    normalized === "::1" ||
    normalized.endsWith(".local") ||
    normalized.endsWith(".localhost") ||
    PRIVATE_IPV4_PATTERN.test(normalized)
  ) {
    return "local";
  }

  return "hosted";
}

export default function RuntimeEnvironmentIndicator({ compact = false }: { compact?: boolean }) {
  const hostname = useSyncExternalStore(
    () => () => {},
    () => window.location.hostname || null,
    () => null,
  );
  const accessMode = resolveRuntimeMode(hostname);
  const isLocal = accessMode === "local";

  return (
    <div
      className={`runtime-indicator ${compact ? "runtime-indicator--compact" : ""}`}
      role="status"
      aria-live="polite"
      title={
        isLocal
          ? "You are using the offline app."
          : "You are using the website."
      }
      aria-label={`Website mode: ${isLocal ? "Local (offline app)" : "Hosted (website)"}`}
    >
      {!compact ? <span className="runtime-indicator__label">Website</span> : null}
      <span
        className={`runtime-indicator__switch ${
          isLocal ? "runtime-indicator__switch--local" : "runtime-indicator__switch--hosted"
        }`}
        aria-hidden="true"
      >
        <span className="runtime-indicator__thumb" />
      </span>
      <span className="runtime-indicator__value">{isLocal ? "Local" : "Hosted"}</span>
    </div>
  );
}
