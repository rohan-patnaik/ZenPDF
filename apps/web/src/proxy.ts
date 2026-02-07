import { clerkMiddleware, createRouteMatcher } from "@clerk/nextjs/server";
import { NextResponse, type NextFetchEvent, NextRequest } from "next/server";

const isPublicRoute = createRouteMatcher([
  "/",
  "/usage-capacity",
  "/api/download(.*)",
  "/sign-in(.*)",
  "/sign-up(.*)",
]);

const shouldBypassAuth = () => process.env.ZENPDF_DISABLE_AUTH === "1";

const getMissingAuthEnvVars = () => {
  const missing: string[] = [];
  if (!process.env.NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY) {
    missing.push("NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY");
  }
  if (!process.env.CLERK_SECRET_KEY) {
    missing.push("CLERK_SECRET_KEY");
  }
  return missing;
};

const clerkHandler = clerkMiddleware(async (auth, req) => {
  if (!isPublicRoute(req)) {
    await auth.protect();
  }
});

export function proxy(request: Request, event: NextFetchEvent) {
  const nextRequest =
    request instanceof NextRequest ? request : new NextRequest(request);

  if (shouldBypassAuth()) {
    const requestHeaders = new Headers(nextRequest.headers);
    requestHeaders.set("x-zenpdf-auth-bypassed", "1");
    return NextResponse.next({
      request: {
        headers: requestHeaders,
      },
    });
  }

  const missingAuthEnvVars = getMissingAuthEnvVars();
  if (missingAuthEnvVars.length > 0) {
    console.error(
      `Authentication configuration error: missing ${missingAuthEnvVars.join(", ")}`,
    );
    return new NextResponse("Authentication configuration error", { status: 500 });
  }

  const sanitizedHeaders = new Headers(nextRequest.headers);
  sanitizedHeaders.delete("x-zenpdf-auth-bypassed");
  const sanitizedRequest = new NextRequest(nextRequest, {
    headers: sanitizedHeaders,
  });

  return clerkHandler(sanitizedRequest, event);
}

export default proxy;

export const config = {
  matcher: ["/((?!.*\\..*|_next).*)", "/", "/(api|trpc)(.*)"],
};
