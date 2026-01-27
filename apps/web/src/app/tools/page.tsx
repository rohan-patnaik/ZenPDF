"use client";

import { useMemo, useState } from "react";
import Link from "next/link";
import { useConvex, useMutation, useQuery } from "convex/react";

import SiteHeader from "@/components/SiteHeader";
import { ANON_STORAGE_KEY, getOrCreateAnonId } from "@/lib/anon-id";
import { api } from "@/lib/convex";
import { uploadFile } from "@/lib/uploads";

type ToolId =
  | "merge"
  | "split"
  | "compress"
  | "rotate"
  | "remove-pages"
  | "reorder-pages"
  | "watermark"
  | "page-numbers"
  | "crop"
  | "unlock"
  | "protect"
  | "repair"
  | "redact"
  | "compare"
  | "image-to-pdf"
  | "pdf-to-jpg"
  | "web-to-pdf"
  | "office-to-pdf"
  | "pdfa"
  | "pdf-to-word"
  | "pdf-to-excel"
  | "pdf-to-word-ocr"
  | "pdf-to-excel-ocr";

type ToolField = {
  key: string;
  label: string;
  placeholder: string;
  helper?: string;
  type?: "text" | "number";
  required?: boolean;
};

type ToolDefinition = {
  id: ToolId;
  label: string;
  description: string;
  accept: string;
  multiple: boolean;
  tier?: "Standard" | "Premium";
  requiresFiles?: boolean;
  fields?: ToolField[];
};

const TOOLS: ToolDefinition[] = [
  {
    id: "merge",
    label: "Merge PDFs",
    description: "Combine multiple PDFs into a single dossier.",
    accept: ".pdf",
    multiple: true,
  },
  {
    id: "split",
    label: "Split PDF",
    description: "Split by ranges (e.g. 1-3,4-6) or leave blank for each page.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "ranges",
        label: "Page ranges",
        placeholder: "1-3,4-6",
        helper: "Leave blank to split every page.",
      },
    ],
  },
  {
    id: "compress",
    label: "Compress PDF",
    description: "Reduce file size while preserving readability.",
    accept: ".pdf",
    multiple: false,
  },
  {
    id: "rotate",
    label: "Rotate pages",
    description: "Rotate pages by 90, 180, or 270 degrees.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "pages",
        label: "Pages",
        placeholder: "1,3-4",
        helper: "Leave blank to rotate all pages.",
      },
      {
        key: "angle",
        label: "Angle",
        placeholder: "90",
        helper: "Use 90, 180, or 270.",
        type: "number",
      },
    ],
  },
  {
    id: "remove-pages",
    label: "Remove pages",
    description: "Drop specific pages from a PDF.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "pages",
        label: "Pages to remove",
        placeholder: "2,5-6",
      },
    ],
  },
  {
    id: "reorder-pages",
    label: "Reorder pages",
    description: "Reorder by listing the new page order.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "order",
        label: "New order",
        placeholder: "3,1,2",
        helper: "Pages not listed are removed.",
      },
    ],
  },
  {
    id: "watermark",
    label: "Watermark",
    description: "Stamp a light text watermark onto each page.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "text",
        label: "Watermark text",
        placeholder: "CONFIDENTIAL",
        required: true,
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
        helper: "Leave blank to watermark every page.",
      },
    ],
  },
  {
    id: "page-numbers",
    label: "Page numbers",
    description: "Add page numbers to the footer of each page.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "start",
        label: "Start number",
        placeholder: "1",
        helper: "Leave blank to start at 1.",
        type: "number",
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
        helper: "Leave blank to number every page.",
      },
    ],
  },
  {
    id: "crop",
    label: "Crop pages",
    description: "Trim margins from each page.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "margins",
        label: "Margins (pt)",
        placeholder: "10,10,10,10",
        helper: "Top,right,bottom,left in points (72 = 1 inch).",
        required: true,
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
        helper: "Leave blank to crop every page.",
      },
    ],
  },
  {
    id: "redact",
    label: "Redact text",
    description: "Find and black out matching text in the PDF.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "text",
        label: "Text to redact",
        placeholder: "CONFIDENTIAL",
        helper: "Case-sensitive match.",
        required: true,
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
        helper: "Leave blank to scan every page.",
      },
    ],
  },
  {
    id: "compare",
    label: "Compare PDFs",
    description: "Generate a text report comparing two PDFs.",
    accept: ".pdf",
    multiple: true,
  },
  {
    id: "unlock",
    label: "Unlock PDF",
    description: "Remove a password so the file opens freely.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "password",
        label: "Current password",
        placeholder: "Enter current password",
        required: true,
      },
    ],
  },
  {
    id: "protect",
    label: "Protect PDF",
    description: "Encrypt a PDF with a new password.",
    accept: ".pdf",
    multiple: false,
    fields: [
      {
        key: "password",
        label: "New password",
        placeholder: "Create a password",
        required: true,
      },
    ],
  },
  {
    id: "repair",
    label: "Repair PDF",
    description: "Rebuild a PDF file to fix structural issues.",
    accept: ".pdf",
    multiple: false,
  },
  {
    id: "image-to-pdf",
    label: "Image → PDF",
    description: "Convert JPG or PNG images into a PDF.",
    accept: ".png,.jpg,.jpeg",
    multiple: true,
  },
  {
    id: "pdf-to-jpg",
    label: "PDF → JPG",
    description: "Export each page as a JPG (zipped when multiple).",
    accept: ".pdf",
    multiple: false,
  },
  {
    id: "web-to-pdf",
    label: "Web → PDF",
    description: "Capture a URL into a printable PDF snapshot.",
    accept: "",
    multiple: false,
    requiresFiles: false,
    fields: [
      {
        key: "url",
        label: "Web address",
        placeholder: "https://example.com",
        required: true,
      },
    ],
  },
  {
    id: "office-to-pdf",
    label: "Office → PDF",
    description: "Convert Word, Excel, or PowerPoint files to PDF.",
    accept: ".docx,.xlsx,.pptx",
    multiple: false,
  },
  {
    id: "pdfa",
    label: "PDF → PDF/A",
    description: "Convert PDFs into archival PDF/A-2b output.",
    accept: ".pdf",
    multiple: false,
    tier: "Premium",
  },
  {
    id: "pdf-to-word",
    label: "PDF → Word",
    description: "Extract text from digital PDFs into DOCX.",
    accept: ".pdf",
    multiple: false,
  },
  {
    id: "pdf-to-word-ocr",
    label: "PDF → Word (OCR)",
    description: "OCR scanned PDFs into a Word document.",
    accept: ".pdf",
    multiple: false,
    tier: "Premium",
  },
  {
    id: "pdf-to-excel",
    label: "PDF → Excel",
    description: "Extract text into a simple XLSX worksheet.",
    accept: ".pdf",
    multiple: false,
  },
  {
    id: "pdf-to-excel-ocr",
    label: "PDF → Excel (OCR)",
    description: "OCR scanned PDFs into a simple XLSX worksheet.",
    accept: ".pdf",
    multiple: false,
    tier: "Premium",
  },
];

type JobRecord = {
  _id: string;
  tool: string;
  status: string;
  progress?: number;
  outputs?: Array<{ storageId: string; filename: string }> | null;
  errorCode?: string;
  errorMessage?: string;
  createdAt: number;
};

/**
 * Render the tools page UI for selecting a tool, uploading files or inputs,
 * configuring options, queuing background jobs, and viewing recent jobs and outputs.
 *
 * The page manages tool selection, file inputs, tool-specific configuration fields,
 * anonymous user identification, upload and job-creation flows, and download links
 * for job outputs.
 *
 * @returns The React element representing the tools page UI.
 */
export default function ToolsPage() {
  const [activeTool, setActiveTool] = useState<ToolId>("merge");
  const [files, setFiles] = useState<File[]>([]);
  const [fileInputKey, setFileInputKey] = useState(0);
  const [configValues, setConfigValues] = useState<Record<string, string>>({});
  const [status, setStatus] = useState<string | null>(null);
  const [lastJobId, setLastJobId] = useState<string | null>(null);
  const [anonId, setAnonId] = useState<string | null>(() => getOrCreateAnonId());
  const [isSubmitting, setIsSubmitting] = useState(false);

  const convex = useConvex();
  const generateUploadUrl = useMutation(api.files.generateUploadUrl);
  const createJob = useMutation(api.jobs.createJob);
  const jobsArgs = useMemo(
    () => (anonId ? { anonId } : {}),
    [anonId],
  );
  const jobs = useQuery(api.jobs.listJobs, jobsArgs) as JobRecord[] | undefined;

  const tool = useMemo(
    () => TOOLS.find((item) => item.id === activeTool),
    [activeTool],
  );

  const selectTool = (toolId: ToolId) => {
    setActiveTool(toolId);
    setFiles([]);
    setConfigValues({});
    setStatus(null);
  };

  const updateConfig = (key: string, value: string) => {
    setConfigValues((prev) => ({ ...prev, [key]: value }));
  };

  const handleFilesChange = (event: React.ChangeEvent<HTMLInputElement>) => {
    const selected = Array.from(event.target.files ?? []);
    setFiles(selected);
  };

  const buildConfig = () => {
    if (!tool) {
      return undefined;
    }
    const config: Record<string, string | number> = {};
    const numberKeys = new Set(
      tool.fields
        ?.filter((field) => field.type === "number")
        .map((field) => field.key) ?? [],
    );
    for (const [key, value] of Object.entries(configValues)) {
      if (!value.trim()) {
        continue;
      }
      if (numberKeys.has(key)) {
        const numericValue = Number(value);
        config[key] = Number.isNaN(numericValue) ? value.trim() : numericValue;
      } else {
        config[key] = value.trim();
      }
    }
    return Object.keys(config).length ? config : undefined;
  };

  const handleSubmit = async () => {
    if (isSubmitting) {
      return;
    }
    if (!tool) {
      return;
    }
    const needsFiles = tool.requiresFiles ?? true;
    if (tool.id === "compare" && files.length < 2) {
      setStatus("Upload two PDFs to compare.");
      return;
    }
    if (needsFiles && files.length === 0) {
      setStatus("Add at least one file to continue.");
      return;
    }
    if (!needsFiles && tool.id === "web-to-pdf") {
      const urlValue = configValues.url?.trim() ?? "";
      if (!urlValue) {
        setStatus("Enter a valid URL.");
        return;
      }
      try {
        const parsed = new URL(urlValue);
        if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
          throw new Error("Invalid protocol");
        }
      } catch {
        setStatus("Enter a valid URL.");
        return;
      }
    }

    const missingField = tool.fields?.find(
      (field) => field.required && !configValues[field.key]?.trim(),
    );
    if (missingField) {
      setStatus(`Enter ${missingField.label.toLowerCase()}.`);
      return;
    }

    setIsSubmitting(true);
    setStatus(needsFiles ? "Uploading files..." : "Preparing job...");
    try {
      const uploads = [] as Array<{
        storageId: string;
        filename: string;
        sizeBytes: number;
      }>;
      if (needsFiles) {
        for (const file of files) {
          uploads.push(
            await uploadFile(file, () =>
              generateUploadUrl({ anonId: anonId ?? undefined }),
            ),
          );
        }
      }

      setStatus("Creating job...");
      const response = await createJob({
        tool: tool.id,
        inputs: uploads,
        config: buildConfig(),
        anonId: anonId ?? undefined,
      });

      if (response.anonId && response.anonId !== anonId) {
        window.localStorage.setItem(ANON_STORAGE_KEY, response.anonId as string);
        setAnonId(response.anonId as string);
      }

      setLastJobId(response.jobId as string);
      setStatus("Job queued. Results will appear below.");
      setFiles([]);
      setFileInputKey((value) => value + 1);
      setConfigValues({});
    } catch (error) {
      console.error("Job creation failed:", error);
      setStatus("Unable to create job. Check limits or try again.");
    } finally {
      setIsSubmitting(false);
    }
  };

  const handleDownload = async (jobId: string, storageId: string) => {
    const url = (await convex.query(api.files.getOutputDownloadUrl, {
      jobId,
      storageId,
      anonId: anonId ?? undefined,
    })) as string | null;
    if (url) {
      window.open(url, "_blank", "noopener");
    }
  };

  return (
    <div className="relative">
      <SiteHeader />
      <main className="relative z-10 mx-auto w-full max-w-6xl px-6 pb-16">
        <section className="paper-card mt-4 p-8">
          <span className="ink-label">Tool desk</span>
          <h1 className="mt-3 text-4xl">Run a tool in a calm, guided flow.</h1>
          <p className="mt-3 max-w-2xl text-base text-ink-700">
            Each tool runs in the background worker, with limits enforced before
            processing begins. Upload, configure, and queue a job in seconds.
          </p>
        </section>

        <section className="mt-8 grid gap-6 lg:grid-cols-[0.9fr_1.1fr]">
          <div className="space-y-4">
            {TOOLS.map((item) => {
              const tierLabel = item.tier ?? "Standard";
              return (
                <button
                  key={item.id}
                  type="button"
                  onClick={() => selectTool(item.id)}
                  className={`paper-card w-full p-4 text-left transition ${
                    item.id === activeTool
                      ? "border-forest-600/60 shadow-paper-lift"
                      : "hover:border-forest-600/30"
                  }`}
                >
                  <div className="flex items-center justify-between">
                    <div>
                      <h2 className="text-lg font-display">{item.label}</h2>
                      <p className="text-xs text-ink-500">{item.description}</p>
                    </div>
                    <span
                      className={`rounded-full px-3 py-1 text-[0.6rem] uppercase tracking-[0.2em] ${
                        tierLabel === "Premium"
                          ? "border border-ink-900/15 bg-rose-100 text-ink-700"
                          : "border border-ink-900/10 bg-paper-100 text-ink-500"
                      }`}
                    >
                      {tierLabel}
                    </span>
                  </div>
                </button>
              );
            })}
          </div>

          <div className="paper-card p-6">
            <div className="flex items-center justify-between">
              <div>
                <span className="ink-label">Active tool</span>
                <h2 className="text-2xl">{tool?.label}</h2>
              </div>
              <Link className="paper-button--ghost" href="/usage-capacity">
                View limits
              </Link>
            </div>
            <div className="mt-4 text-sm text-ink-700">{tool?.description}</div>

            <div className="mt-6 space-y-4">
              {(tool?.requiresFiles ?? true) && (
                <div>
                  <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                    Files
                  </label>
                  <input
                    key={fileInputKey}
                    type="file"
                    className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                    accept={tool?.accept}
                    multiple={tool?.multiple}
                    onChange={handleFilesChange}
                  />
                  {files.length > 0 && (
                    <p className="mt-2 text-xs text-ink-500">
                      {files.length} file(s) selected.
                    </p>
                  )}
                </div>
              )}

              {tool?.fields?.map((field) => (
                <div key={field.key}>
                  <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                    {field.label}
                  </label>
                  <input
                    type={field.type ?? "text"}
                    inputMode={field.type === "number" ? "numeric" : undefined}
                    min={field.key === "angle" ? 90 : undefined}
                    max={field.key === "angle" ? 270 : undefined}
                    step={field.key === "angle" ? 90 : undefined}
                    value={configValues[field.key] ?? ""}
                    onChange={(event) =>
                      updateConfig(field.key, event.target.value)
                    }
                    placeholder={field.placeholder}
                    className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                  />
                  {field.helper && (
                    <p className="mt-2 text-xs text-ink-500">{field.helper}</p>
                  )}
                </div>
              ))}
            </div>

            <div className="mt-6 flex flex-wrap items-center gap-3">
              <button
                className="paper-button"
                type="button"
                onClick={handleSubmit}
                disabled={isSubmitting}
              >
                Queue job
              </button>
              {status && <p className="text-sm text-ink-700">{status}</p>}
            </div>
            {lastJobId && (
              <p className="mt-3 text-xs text-ink-500">
                Latest job: {lastJobId}
              </p>
            )}
          </div>
        </section>

        <section className="mt-10">
          <div className="flex items-center justify-between">
            <h2 className="text-2xl">Recent jobs</h2>
            <span className="text-xs uppercase tracking-[0.2em] text-ink-500">
              Auto-refresh
            </span>
          </div>
          <div className="mt-4 grid gap-4">
            {(jobs ?? []).length === 0 && (
              <div className="paper-card p-6 text-sm text-ink-700">
                No jobs yet. Queue a tool above to get started.
              </div>
            )}
            {(jobs ?? []).map((job) => (
              <div key={job._id} className="paper-card p-5">
                <div className="flex flex-wrap items-center justify-between gap-3">
                  <div>
                    <div className="text-lg font-display text-ink-900">
                      {job.tool}
                    </div>
                    <div className="text-xs text-ink-500">
                      Status: {job.status}
                      {job.progress !== undefined && ` (${job.progress}%)`}
                    </div>
                    {job.errorCode && (
                      <div className="text-xs text-ink-500">
                        {job.errorCode} {job.errorMessage ? `— ${job.errorMessage}` : ""}
                      </div>
                    )}
                  </div>
                  <div className="flex flex-wrap gap-2">
                    {(job.outputs ?? []).map((output) => (
                      <button
                        key={output.storageId}
                        type="button"
                        className="paper-button--ghost text-xs"
                        onClick={() => handleDownload(job._id, output.storageId)}
                      >
                        {output.filename}
                      </button>
                    ))}
                  </div>
                </div>
              </div>
            ))}
          </div>
        </section>
      </main>
    </div>
  );
}
