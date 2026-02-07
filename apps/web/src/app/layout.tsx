import type { Metadata } from "next";
import { Manrope, Public_Sans } from "next/font/google";

import DonateBookmark from "@/components/DonateBookmark";

import Providers from "./providers";
import "./globals.css";

const displayFont = Manrope({
  variable: "--font-display",
  subsets: ["latin"],
  weight: ["600", "700", "800"],
});

const bodyFont = Public_Sans({
  variable: "--font-body",
  subsets: ["latin"],
  weight: ["400", "500", "600"],
});

export const metadata: Metadata = {
  title: "ZenPDF",
  description:
    "ZenPDF is an open-source PDF workbench with clear usage limits and a clean, readable interface.",
  icons: {
    icon: [{ url: "/icon.svg?v=3", type: "image/svg+xml" }],
    shortcut: [{ url: "/icon.svg?v=3", type: "image/svg+xml" }],
    apple: [{ url: "/apple-touch-icon.png?v=1", type: "image/png", sizes: "180x180" }],
  },
};

const themeInitScript = `
(() => {
  try {
    const key = "zenpdf-theme";
    const saved = window.localStorage.getItem(key);
    const theme = saved === "light" || saved === "dark"
      ? saved
      : "light";
    document.documentElement.dataset.theme = theme;
  } catch (_) {
    document.documentElement.dataset.theme = "light";
  }
})();
`;

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html
      lang="en"
      className={`${displayFont.variable} ${bodyFont.variable}`}
      suppressHydrationWarning
    >
      <head>
        <meta name="color-scheme" content="light dark" />
        <script dangerouslySetInnerHTML={{ __html: themeInitScript }} />
      </head>
      <body className="min-h-screen text-ink-900 antialiased">
        <Providers>
          {children}
          <DonateBookmark />
        </Providers>
      </body>
    </html>
  );
}
