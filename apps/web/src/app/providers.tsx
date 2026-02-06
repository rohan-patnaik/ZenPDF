"use client";

import { ClerkProvider, useAuth } from "@clerk/nextjs";
import { ConvexProviderWithClerk } from "convex/react-clerk";
import { ConvexReactClient } from "convex/react";

import { ThemeModeProvider } from "@/components/ThemeModeProvider";

const convexUrl = process.env.NEXT_PUBLIC_CONVEX_URL;

if (!convexUrl && process.env.NODE_ENV === "production") {
  throw new Error("NEXT_PUBLIC_CONVEX_URL is required in production.");
}

const convex = new ConvexReactClient(convexUrl ?? "http://localhost:3210");

export default function Providers({ children }: { children: React.ReactNode }) {
  return (
    <ThemeModeProvider>
      <ClerkProvider
        signInUrl="/sign-in"
        signUpUrl="/sign-up"
        appearance={{
          variables: {
            colorPrimary: "rgb(var(--forest-600))",
            colorText: "rgb(var(--ink-900))",
            colorTextOnPrimaryBackground: "#ffffff",
            colorBackground: "rgb(var(--paper-50))",
            colorInputBackground: "rgb(var(--paper-50))",
            colorInputText: "rgb(var(--ink-900))",
            colorNeutral: "rgb(var(--paper-300))",
          },
          elements: {
            userButtonTrigger: "h-9 w-9 rounded-full",
            userButtonAvatarBox: "h-9 w-9 rounded-full",
            userButtonPopoverCard:
              "border border-paper-200 bg-paper-50 text-ink-900 shadow-paper",
            userButtonPopoverMain: "bg-paper-50",
            userButtonPopoverActions: "bg-paper-50",
            userButtonPopoverActionButton:
              "!text-ink-900 !opacity-100 hover:bg-paper-100 hover:!text-ink-900",
            userButtonPopoverActionButtonIconBox: "!text-ink-700 !opacity-100",
            userButtonPopoverActionButtonIcon: "!text-ink-700 !opacity-100",
            userButtonPopoverFooter: "bg-paper-100 !text-ink-700",
            userButtonPopoverFooterPagesLink:
              "!text-ink-700 hover:!text-ink-900",
            userPreviewMainIdentifierText: "!text-ink-900 !opacity-100",
            userPreviewSecondaryIdentifier: "!text-ink-700 !opacity-100",
          },
        }}
      >
        <ConvexProviderWithClerk client={convex} useAuth={useAuth}>
          {children}
        </ConvexProviderWithClerk>
      </ClerkProvider>
    </ThemeModeProvider>
  );
}
