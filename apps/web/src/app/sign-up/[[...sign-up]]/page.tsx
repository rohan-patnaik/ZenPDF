"use client";

import { SignUp } from "@clerk/nextjs";

export default function SignUpPage() {
  return (
    <div className="relative z-10 flex min-h-screen items-center justify-center px-6 py-12">
      <div className="paper-card w-full max-w-md p-6">
        <SignUp
          appearance={{
            variables: {
              colorPrimary: "#1f4338",
            },
          }}
        />
      </div>
    </div>
  );
}
