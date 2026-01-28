"use client";

import { useEffect, useMemo, useState } from "react";
import Link from "next/link";
import { useMutation, useQuery } from "convex/react";

import SiteHeader from "@/components/SiteHeader";
import { ANON_STORAGE_KEY, getOrCreateAnonId } from "@/lib/anon-id";
import { api } from "@/lib/convex";
import { formatBytes } from "@/lib/formatters";
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
  | "highlight"
  | "compare"
  | "image-to-pdf"
  | "pdf-to-jpg"
  | "web-to-pdf"
  | "office-to-pdf"
  | "pdfa"
  | "pdf-to-text"
  | "pdf-to-word"
  | "pdf-to-excel"
  | "pdf-to-word-ocr"
  | "pdf-to-excel-ocr";

type ToolField = {
  key: string;
  label: string;
  placeholder: string;
  helper?: string;
  type?: "text" | "number" | "password";
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

const PDF_ACCEPT = "application/pdf,.pdf";
const IMAGE_ACCEPT = "image/*,.png,.jpg,.jpeg";
const OFFICE_ACCEPT =
  "application/vnd.openxmlformats-officedocument.wordprocessingml.document,.docx," +
  "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet,.xlsx," +
  "application/vnd.openxmlformats-officedocument.presentationml.presentation,.pptx";

const DEV_BYPASS_STORAGE_KEY = "zenpdf-dev-bypass";

const TOOLS: ToolDefinition[] = [
  {
    id: "merge",
    label: "Merge PDFs",
    description: "Combine multiple PDFs into a single dossier.",
    accept: PDF_ACCEPT,
    multiple: true,
  },
  {
    id: "split",
    label: "Split PDF",
    description: "Split by ranges (e.g. 1-3,4-6) or leave blank for each page.",
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "rotate",
    label: "Rotate pages",
    description: "Rotate pages by 90, 180, or 270 degrees.",
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
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
    accept: PDF_ACCEPT,
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
    id: "highlight",
    label: "Highlight text",
    description: "Highlight matching text across the PDF.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "text",
        label: "Text to highlight",
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
    accept: PDF_ACCEPT,
    multiple: true,
  },
  {
    id: "unlock",
    label: "Unlock PDF",
    description: "Remove a password so the file opens freely.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "password",
        label: "Current password",
        placeholder: "Enter current password",
        type: "password",
        required: true,
      },
    ],
  },
  {
    id: "protect",
    label: "Protect PDF",
    description: "Encrypt a PDF with a new password.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "password",
        label: "New password",
        placeholder: "Create a password",
        type: "password",
        required: true,
      },
    ],
  },
  {
    id: "repair",
    label: "Repair PDF",
    description: "Rebuild a PDF file to fix structural issues.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "image-to-pdf",
    label: "Image → PDF",
    description: "Convert JPG or PNG images into a PDF.",
    accept: IMAGE_ACCEPT,
    multiple: true,
  },
  {
    id: "pdf-to-jpg",
    label: "PDF → JPG",
    description: "Export each page as a JPG (zipped when multiple).",
    accept: PDF_ACCEPT,
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
    accept: OFFICE_ACCEPT,
    multiple: false,
  },
  {
    id: "pdfa",
    label: "PDF → PDF/A",
    description: "Convert PDFs into archival PDF/A-2b output.",
    accept: PDF_ACCEPT,
    multiple: false,
    tier: "Premium",
  },
  {
    id: "pdf-to-text",
    label: "PDF → Text",
    description: "Extract raw text into a plain TXT file.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "pdf-to-word",
    label: "PDF → Word",
    description: "Extract text from digital PDFs into DOCX.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "pdf-to-word-ocr",
    label: "PDF → Word (OCR)",
    description: "OCR scanned PDFs into a Word document.",
    accept: PDF_ACCEPT,
    multiple: false,
    tier: "Premium",
  },
  {
    id: "pdf-to-excel",
    label: "PDF → Excel",
    description: "Extract text into a simple XLSX worksheet.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "pdf-to-excel-ocr",
    label: "PDF → Excel (OCR)",
    description: "OCR scanned PDFs into a simple XLSX worksheet.",
    accept: PDF_ACCEPT,
    multiple: false,
    tier: "Premium",
  },
];

type JobRecord = {
  _id: string;
  tool: string;
  status: string;
  progress?: number;
  inputs?: Array<{ sizeBytes?: number }> | null;
  outputs?: Array<{ storageId: string; filename: string; sizeBytes?: number }> | null;
  errorCode?: string;
  errorMessage?: string;
  createdAt: number;
};

const JobCard = ({
  job,
  onDownload,
}: {
  job: JobRecord;
  onDownload: (jobId: string, storageId: string, filename: string) => void;
}) => {
  const inputSize =
    job.inputs && job.inputs.length === 1 ? job.inputs[0]?.sizeBytes : undefined;

  return (
    <div className="paper-card p-5">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <div className="text-lg font-display text-ink-900">{job.tool}</div>
          <div className="text-xs text-ink-500">
            Status: {job.status}
            {job.progress !== undefined && ` (${job.progress}%)`}
          </div>
          {job.errorCode && (
            <div className="text-xs text-ink-500">
              {job.errorCode} {job.errorMessage ? `— ${job.errorMessage}` : ""}
            </div>
          )}
          {(job.status === "queued" || job.status === "running") && (
            <div className="mt-2 flex items-center gap-2 text-xs text-ink-500">
              <span className="h-2 w-2 animate-pulse rounded-full bg-forest-500" />
              <span>{job.status === "queued" ? "Queued" : "Processing"}</span>
            </div>
          )}
          {(job.status === "queued" || job.status === "running") && (
            <div className="mt-2 h-1 w-full rounded-full bg-ink-900/10">
              <div
                className="h-full rounded-full bg-forest-500"
                style={{
                  width: `${Math.min(
                    Math.max(
                      job.progress ?? (job.status === "queued" ? 5 : 35),
                      0,
                    ),
                    100,
                  )}%`,
                }}
              />
            </div>
          )}
        </div>
        <div className="flex flex-col gap-2">
          {(job.outputs ?? []).map((output) => (
            <div
              key={output.storageId}
              className="flex items-center justify-between gap-3"
            >
              <div className="min-w-0">
                <div className="truncate text-xs text-ink-700">
                  {output.filename}
                </div>
                {output.sizeBytes !== undefined && (
                  <div className="text-[0.65rem] text-ink-500">
                    {formatBytes(output.sizeBytes)}
                    {inputSize !== undefined && ` · from ${formatBytes(inputSize)}`}
                  </div>
                )}
              </div>
              <button
                type="button"
                className="paper-button--ghost text-[0.6rem] uppercase tracking-[0.2em]"
                onClick={() =>
                  onDownload(job._id, output.storageId, output.filename)
                }
              >
                Download
              </button>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
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
  const [devBypass, setDevBypass] = useState(false);
  const devModeAvailable = process.env.NODE_ENV === "development";

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
  const fileLabel = tool?.multiple ? "Choose files" : "Choose file";

  const totalSize = useMemo(
    () => files.reduce((sum, file) => sum + file.size, 0),
    [files],
  );

  const activeJob = useMemo(() => {
    if (!jobs) {
      return undefined;
    }
    if (lastJobId) {
      const match = jobs.find((job) => job._id === lastJobId);
      if (match) {
        return match;
      }
    }
    return jobs.find((job) => job.status === "queued" || job.status === "running");
  }, [jobs, lastJobId]);

  const selectTool = (toolId: ToolId) => {
    setActiveTool(toolId);
    setFiles([]);
    setFileInputKey((value) => value + 1);
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

  const clearFiles = () => {
    setFiles([]);
    setFileInputKey((value) => value + 1);
  };

  useEffect(() => {
    if (!devModeAvailable) {
      return;
    }
    const stored = window.localStorage.getItem(DEV_BYPASS_STORAGE_KEY);
    if (stored) {
      setDevBypass(stored === "1");
    }
  }, [devModeAvailable]);

  const toggleDevBypass = () => {
    setDevBypass((prev) => {
      const next = !prev;
      window.localStorage.setItem(DEV_BYPASS_STORAGE_KEY, next ? "1" : "0");
      return next;
    });
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
    if (tool.id === "compare" && files.length !== 2) {
      setStatus("Upload exactly two PDFs to compare.");
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
        devBypass: devModeAvailable && devBypass ? true : undefined,
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

  function handleDownload(
    jobId: string,
    storageId: string,
    filename: string,
  ): void {
    const params = new URLSearchParams({
      jobId,
      storageId,
      filename,
    });
    if (anonId) {
      params.set("anonId", anonId);
    }
    window.open(`/api/download?${params.toString()}`, "_blank", "noopener");
  }

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
                  aria-pressed={item.id === activeTool}
                  className={`paper-card w-full p-4 text-left transition duration-200 ease-out focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-forest-500/40 hover:shadow-paper-lift active:translate-y-0.5 active:shadow-paper ${
                    item.id === activeTool
                      ? "border-forest-600/60 ring-2 ring-forest-600/20"
                      : "hover:border-forest-600/30 hover:ring-1 hover:ring-forest-600/10"
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

            {devModeAvailable && (
              <div className="mt-4 flex flex-wrap items-center justify-between gap-3 rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-xs text-ink-600">
                <div>
                  <div className="font-semibold text-ink-700">Dev mode</div>
                  <div className="text-ink-500">
                    Bypass limits locally (development builds; requires `ZENPDF_DEV_MODE=1`).
                  </div>
                </div>
                <button
                  type="button"
                  onClick={toggleDevBypass}
                  className={`rounded-full px-3 py-1 text-[0.65rem] uppercase tracking-[0.2em] transition ${
                    devBypass
                      ? "bg-forest-600 text-paper-50"
                      : "border border-ink-900/10 bg-paper-50 text-ink-600"
                  }`}
                >
                  {devBypass ? "Bypass on" : "Bypass off"}
                </button>
              </div>
            )}

            <div className="mt-6 space-y-4">
              {(tool?.requiresFiles ?? true) && (
                <div>
                  <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                    Files
                  </label>
                  <div className="mt-2 space-y-2">
                    <label className="paper-button--ghost inline-flex cursor-pointer items-center gap-2 text-xs uppercase tracking-[0.2em]">
                      <input
                        key={fileInputKey}
                        type="file"
                        className="sr-only"
                        accept={tool?.accept}
                        multiple={tool?.multiple}
                        onChange={handleFilesChange}
                      />
                      {fileLabel}
                    </label>
                    <div className="text-xs text-ink-500">
                      {files.length > 0
                        ? `${files.length} file(s) ready • ${formatBytes(totalSize)}`
                        : "No files selected yet."}
                    </div>
                    {files.length > 0 && (
                      <div className="space-y-1 text-xs text-ink-600">
                        {files.map((file) => (
                          <div
                            key={`${file.name}-${file.size}-${file.lastModified}`}
                            className="flex items-center justify-between gap-3"
                          >
                            <span className="truncate">{file.name}</span>
                            <span className="text-ink-500">
                              {formatBytes(file.size)}
                            </span>
                          </div>
                        ))}
                      </div>
                    )}
                    {files.length > 0 && (
                      <button
                        type="button"
                        onClick={clearFiles}
                        className="paper-button--ghost w-fit text-[0.65rem] uppercase tracking-[0.2em]"
                      >
                        Clear files
                      </button>
                    )}
                  </div>
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
                className="paper-button flex items-center gap-2"
                type="button"
                onClick={handleSubmit}
                disabled={isSubmitting}
              >
                {isSubmitting && (
                  <span className="h-4 w-4 animate-spin rounded-full border-2 border-paper-50/60 border-t-paper-50" />
                )}
                {isSubmitting ? "Working..." : "Queue job"}
              </button>
              {status && <p className="text-sm text-ink-700">{status}</p>}
            </div>
            {activeJob && (activeJob.status === "queued" || activeJob.status === "running") && (
              <div className="mt-4 rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3">
                <div className="flex items-center gap-2 text-xs text-ink-500">
                  <span className="h-2 w-2 animate-pulse rounded-full bg-forest-500" />
                  <span className="uppercase tracking-[0.2em]">Processing</span>
                </div>
                <div className="mt-2 text-sm text-ink-700">
                  {activeJob.status === "queued" ? "Queued" : "Running"}
                  {activeJob.progress !== undefined && ` • ${activeJob.progress}%`}
                </div>
                <div className="mt-2 h-2 w-full rounded-full bg-ink-900/10">
                  <div
                    className="h-full rounded-full bg-forest-500"
                    style={{
                      width: `${Math.min(
                        Math.max(
                          activeJob.progress ?? (activeJob.status === "queued" ? 5 : 35),
                          0,
                        ),
                        100,
                      )}%`,
                    }}
                  />
                </div>
              </div>
            )}
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
              <JobCard key={job._id} job={job} onDownload={handleDownload} />
            ))}
          </div>
        </section>
      </main>
    </div>
  );
}
