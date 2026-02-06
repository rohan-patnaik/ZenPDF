"use client";

import { useThemeMode } from "@/components/ThemeModeProvider";

export default function ThemeToggle() {
  const { theme, toggleTheme } = useThemeMode();
  const nextLabel = theme === "dark" ? "Switch to light mode" : "Switch to dark mode";

  return (
    <button
      type="button"
      onClick={toggleTheme}
      className="paper-button--ghost theme-toggle"
      aria-label={nextLabel}
      title={nextLabel}
      aria-pressed={theme === "dark"}
    >
      <span className="sr-only">{nextLabel}</span>
      {theme === "dark" ? (
        <svg
          aria-hidden="true"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="1.8"
          strokeLinecap="round"
          strokeLinejoin="round"
        >
          <circle cx="12" cy="12" r="4.5" />
          <path d="M12 2.5v2.2" />
          <path d="M12 19.3v2.2" />
          <path d="m4.9 4.9 1.6 1.6" />
          <path d="m17.5 17.5 1.6 1.6" />
          <path d="M2.5 12h2.2" />
          <path d="M19.3 12h2.2" />
          <path d="m4.9 19.1 1.6-1.6" />
          <path d="m17.5 6.5 1.6-1.6" />
        </svg>
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
          <path d="M21 14.5A8.5 8.5 0 1 1 9.5 3 6.6 6.6 0 0 0 21 14.5Z" />
        </svg>
      )}
    </button>
  );
}
