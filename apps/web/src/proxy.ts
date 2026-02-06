import { clerkMiddleware, createRouteMatcher } from "@clerk/nextjs/server";
import { NextResponse, type NextFetchEvent, NextRequest } from "next/server";

const isPublicRoute = createRouteMatcher([
  "/",
  "/usage-capacity",
  "/api/download(.*)",
  "/sign-in(.*)",
  "/sign-up(.*)",
]);

const shouldBypassAuth = () =>
  process.env.ZENPDF_DISABLE_AUTH === "1" &&
  process.env.NODE_ENV !== "production";

const clerkHandler = clerkMiddleware(async (auth, req) => {
  if (!isPublicRoute(req)) {
    await auth.protect();
  }
});

export function proxy(request: Request, event: NextFetchEvent) {
  if (shouldBypassAuth()) {
    const requestHeaders = new Headers(request.headers);
    requestHeaders.set("x-zenpdf-auth-bypassed", "1");
    return NextResponse.next({
      request: {
        headers: requestHeaders,
      },
    });
  }
  const nextRequest =
    request instanceof NextRequest ? request : new NextRequest(request);
  return clerkHandler(nextRequest, event);
}

export default proxy;

export const config = {
  matcher: ["/((?!.*\\..*|_next).*)", "/", "/(api|trpc)(.*)"],
};
