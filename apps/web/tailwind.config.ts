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
        display: ["var(--font-display)", "serif"],
        body: ["var(--font-body)", "serif"],
      },
      borderRadius: {
        paper: "28px",
        "paper-lg": "34px",
      },
      boxShadow: {
        paper:
          "0 22px 45px -34px rgba(34, 26, 18, 0.65), 0 6px 14px -12px rgba(34, 26, 18, 0.35)",
        "paper-lift":
          "0 28px 60px -40px rgba(34, 26, 18, 0.6), 0 10px 20px -14px rgba(34, 26, 18, 0.35)",
      },
    },
  },
  plugins: [],
};

export default config;
