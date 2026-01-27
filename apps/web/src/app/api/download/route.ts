import { auth } from "@clerk/nextjs/server";
import { ConvexHttpClient } from "convex/browser";
import type { NextRequest } from "next/server";

import { api } from "@/lib/convex";

export const dynamic = "force-dynamic";

/**
 * Sanitize filenames for safe Content-Disposition headers.
 */
const sanitizeFilename = (filename: string) => {
  const cleaned = filename
    .replace(/[\r\n]/g, " ")
    .replace(/[<>:"/\\|?*]+/g, "_")
    .replace(/\s+/g, " ")
    .trim();
  return cleaned || "zenpdf-output";
};

/**
 * Proxy download requests while enforcing access checks.
 */
export async function GET(request: NextRequest) {
  const { searchParams } = new URL(request.url);
  const jobId = searchParams.get("jobId");
  const storageId = searchParams.get("storageId");
  const filename = searchParams.get("filename") ?? "zenpdf-output";
  const anonId = searchParams.get("anonId") ?? undefined;

  if (!jobId || !storageId) {
    return new Response("Missing download parameters.", { status: 400 });
  }

  const convexUrl = process.env.NEXT_PUBLIC_CONVEX_URL ?? "http://localhost:3210";
  const convex = new ConvexHttpClient(convexUrl);
  const { getToken } = auth();
  const token = await getToken({ template: "convex" });
  if (token) {
    convex.setAuth(token);
  }

  let downloadUrl: string | null = null;
  try {
    downloadUrl = await convex.query(api.files.getOutputDownloadUrl, {
      jobId,
      storageId,
      anonId,
    });
  } catch (error) {
    console.error("Download URL query failed:", error);
    return new Response("Unable to stream the download.", { status: 502 });
  }

  if (!downloadUrl) {
    return new Response("Unable to locate the requested file.", { status: 404 });
  }

  let upstream: Response;
  try {
    upstream = await fetch(downloadUrl);
  } catch (error) {
    console.error("Download fetch failed:", error);
    return new Response("Unable to stream the download.", { status: 502 });
  }
  if (!upstream.ok || !upstream.body) {
    return new Response("Unable to stream the download.", { status: 502 });
  }

  const headers = new Headers(upstream.headers);
  headers.set("Content-Disposition", `attachment; filename="${sanitizeFilename(filename)}"`);
  headers.set("Cache-Control", "private, no-store");

  return new Response(upstream.body, {
    status: 200,
    headers,
  });
}
