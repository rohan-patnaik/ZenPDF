import type { Metadata } from "next";
import { Playfair_Display, Source_Serif_4 } from "next/font/google";

import Providers from "./providers";
import "./globals.css";

const displayFont = Playfair_Display({
  variable: "--font-display",
  subsets: ["latin"],
  weight: ["600", "700"],
});

const bodyFont = Source_Serif_4({
  variable: "--font-body",
  subsets: ["latin"],
  weight: ["400", "500", "600"],
});

export const metadata: Metadata = {
  title: "ZenPDF",
  description:
    "ZenPDF is an open-source PDF workbench with clear usage limits and a tactile dossier-style interface.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className={`${displayFont.variable} ${bodyFont.variable}`}>
      <body className="min-h-screen bg-paper-50 text-ink-900 antialiased">
        <Providers>{children}</Providers>
      </body>
    </html>
  );
}
