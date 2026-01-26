"use client";

import { ClerkProvider, useAuth } from "@clerk/nextjs";
import { ConvexProviderWithClerk } from "convex/react-clerk";
import { ConvexReactClient } from "convex/react";

const convexUrl = process.env.NEXT_PUBLIC_CONVEX_URL;

if (!convexUrl && process.env.NODE_ENV === "production") {
  throw new Error("NEXT_PUBLIC_CONVEX_URL is required in production.");
}

const convex = new ConvexReactClient(convexUrl ?? "http://localhost:3210");

export default function Providers({ children }: { children: React.ReactNode }) {
  return (
    <ClerkProvider
      signInUrl="/sign-in"
      signUpUrl="/sign-up"
      appearance={{
        variables: {
          colorPrimary: "#1f4338",
          colorText: "#2b2723",
          colorBackground: "#f7f0e5",
        },
      }}
    >
      <ConvexProviderWithClerk client={convex} useAuth={useAuth}>
        {children}
      </ConvexProviderWithClerk>
    </ClerkProvider>
  );
}
