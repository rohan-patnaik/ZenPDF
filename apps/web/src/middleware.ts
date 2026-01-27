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

const clerkHandler = clerkMiddleware((auth, req) => {
  if (!isPublicRoute(req)) {
    return auth.protect();
  }
});

export function middleware(request: Request, event: NextFetchEvent) {
  if (shouldBypassAuth()) {
    return NextResponse.next();
  }
  const nextRequest =
    request instanceof NextRequest ? request : new NextRequest(request);
  return clerkHandler(nextRequest, event);
}

export default middleware;

export const config = {
  matcher: ["/((?!.*\\..*|_next).*)", "/", "/(api|trpc)(.*)"],
};
