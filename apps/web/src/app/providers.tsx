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
            colorNeutral: "rgb(var(--ink-500))",
          },
          elements: {
            userButtonTrigger:
              "h-9 w-9 rounded-full ring-1 ring-paper-300/80 transition hover:ring-forest-500/55",
            userButtonAvatarBox: "h-9 w-9 rounded-full",
            userButtonPopoverRootBox: "zen-user-popover-root mt-2",
            userButtonPopoverCard:
              "zen-user-popover-card rounded-[16px] overflow-hidden border border-paper-200 bg-paper-50 text-ink-900 shadow-paper-lift",
            userButtonPopoverMain: "zen-user-popover-main bg-paper-50",
            userButtonPopoverActions:
              "zen-user-popover-actions border-t border-paper-200 bg-paper-50",
            userButtonPopoverActionButton:
              "zen-user-popover-action min-h-14 px-4 !text-ink-900 !opacity-100 hover:bg-paper-100 hover:!text-ink-900 focus:bg-paper-100 focus:!text-ink-900",
            userButtonPopoverActionButtonIconBox:
              "zen-user-popover-action-iconbox !text-ink-700 !opacity-100",
            userButtonPopoverActionButtonIcon:
              "zen-user-popover-action-icon !text-ink-700 !opacity-100",
            userButtonPopoverFooter:
              "zen-user-popover-footer border-t border-paper-200 bg-paper-100 !text-ink-700",
            userButtonPopoverFooterPagesLink:
              "zen-user-popover-footer-link !text-ink-700 hover:!text-ink-900",
            userPreview: "zen-user-preview px-4 py-4",
            userPreviewAvatarContainer: "zen-user-preview-avatar-wrap",
            userPreviewAvatarBox: "zen-user-preview-avatar-box",
            userPreviewTextContainer: "zen-user-preview-text gap-1",
            userPreviewMainIdentifierText:
              "zen-user-preview-name !text-ink-900 !opacity-100",
            userPreviewSecondaryIdentifier:
              "zen-user-preview-email !text-ink-700 !opacity-100",
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
