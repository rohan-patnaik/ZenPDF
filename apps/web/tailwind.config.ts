import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./src/**/*.{ts,tsx}", "./tests/**/*.{ts,tsx}"],
  theme: {
    extend: {
      colors: {
        paper: {
          50: "rgb(var(--paper-50) / <alpha-value>)",
          100: "rgb(var(--paper-100) / <alpha-value>)",
          200: "rgb(var(--paper-200) / <alpha-value>)",
          300: "rgb(var(--paper-300) / <alpha-value>)",
        },
        ink: {
          900: "rgb(var(--ink-900) / <alpha-value>)",
          700: "rgb(var(--ink-700) / <alpha-value>)",
          500: "rgb(var(--ink-500) / <alpha-value>)",
        },
        forest: {
          700: "rgb(var(--forest-700) / <alpha-value>)",
          600: "rgb(var(--forest-600) / <alpha-value>)",
          500: "rgb(var(--forest-500) / <alpha-value>)",
        },
        sage: {
          200: "rgb(var(--sage-200) / <alpha-value>)",
        },
        rose: {
          100: "rgb(var(--rose-100) / <alpha-value>)",
        },
        gold: {
          200: "rgb(var(--gold-200) / <alpha-value>)",
        },
      },
      fontFamily: {
        display: ["var(--font-display)", "sans-serif"],
        body: ["var(--font-body)", "sans-serif"],
      },
      borderRadius: {
        paper: "16px",
        "paper-lg": "20px",
      },
      boxShadow: {
        paper:
          "0 1px 2px rgba(15, 23, 42, 0.06), 0 12px 24px -20px rgba(15, 23, 42, 0.25)",
        "paper-lift":
          "0 1px 2px rgba(15, 23, 42, 0.05), 0 18px 32px -22px rgba(15, 23, 42, 0.3)",
      },
    },
  },
  plugins: [],
};

export default config;
