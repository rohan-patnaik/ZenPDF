import { afterEach, describe, expect, it, vi } from "vitest";

import { uploadFile } from "./uploads";

const testFile = () => {
  const blob = new Blob(["test"], { type: "application/pdf" });
  Object.defineProperty(blob, "name", { value: "input.pdf" });
  return blob as File;
};

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("uploadFile", () => {
  it("reserves, uploads, binds, and returns the consumed job input", async () => {
    const calls: string[] = [];
    const beginUpload = vi.fn(async (intent) => {
      calls.push("begin");
      expect(intent).toEqual({
        filename: "input.pdf",
        sizeBytes: 4,
        contentType: "application/pdf",
      });
      return {
        reservationId: "reservation-1",
        uploadUrl: "https://upload.invalid/one-time",
        expiresAt: Date.now() + 60_000,
      };
    });
    const bindUpload = vi.fn(async (binding) => {
      calls.push("bind");
      expect(binding).toEqual({
        reservationId: "reservation-1",
        storageId: "storage-1",
      });
    });
    vi.stubGlobal(
      "fetch",
      vi.fn(async (_url: string, init: RequestInit) => {
        calls.push("post");
        expect(init.method).toBe("POST");
        expect(init.headers).toEqual({ "Content-Type": "application/pdf" });
        return {
          ok: true,
          json: async () => ({ storageId: "storage-1" }),
        } as Response;
      }),
    );

    await expect(uploadFile(testFile(), beginUpload, bindUpload)).resolves.toEqual({
      reservationId: "reservation-1",
      storageId: "storage-1",
      filename: "input.pdf",
      sizeBytes: 4,
    });
    expect(calls).toEqual(["begin", "post", "bind"]);
  });

  it("never binds when the direct upload fails", async () => {
    const beginUpload = vi.fn(async () => ({
      reservationId: "reservation-1",
      uploadUrl: "https://upload.invalid/one-time",
      expiresAt: Date.now() + 60_000,
    }));
    const bindUpload = vi.fn();
    vi.stubGlobal(
      "fetch",
      vi.fn(async () => ({ ok: false }) as Response),
    );

    await expect(
      uploadFile(testFile(), beginUpload, bindUpload),
    ).rejects.toThrow("Upload failed.");
    expect(bindUpload).not.toHaveBeenCalled();
  });
});
