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
  | "pdf-to-word"
  | "pdf-to-powerpoint"
  | "pdf-to-excel"
  | "word-to-pdf"
  | "powerpoint-to-pdf"
  | "excel-to-pdf"
  | "edit-pdf"
  | "pdf-to-jpg"
  | "jpg-to-pdf"
  | "sign-pdf"
  | "watermark"
  | "rotate"
  | "html-to-pdf"
  | "unlock"
  | "protect"
  | "organize-pdf"
  | "pdfa"
  | "repair"
  | "page-numbers"
  | "scan-to-pdf"
  | "ocr-pdf"
  | "compare"
  | "redact"
  | "crop";

type ToolField = {
  key: string;
  label: string;
  placeholder: string;
  helper?: string;
  type?: "text" | "number" | "password" | "textarea";
  required?: boolean;
};

type ToolDefinition = {
  id: ToolId;
  label: string;
  description: string;
  accept: string;
  multiple: boolean;
  requiresFiles?: boolean;
  fields?: ToolField[];
};

const PDF_ACCEPT = "application/pdf,.pdf";
const IMAGE_ACCEPT = "image/*,.png,.jpg,.jpeg";
const WORD_ACCEPT = "application/msword,application/vnd.openxmlformats-officedocument.wordprocessingml.document,.doc,.docx";
const POWERPOINT_ACCEPT = "application/vnd.ms-powerpoint,application/vnd.openxmlformats-officedocument.presentationml.presentation,.ppt,.pptx";
const EXCEL_ACCEPT = "application/vnd.ms-excel,application/vnd.openxmlformats-officedocument.spreadsheetml.sheet,.xls,.xlsx";

const TOOLS: ToolDefinition[] = [
  {
    id: "merge",
    label: "Merge PDF",
    description: "Combine multiple PDF files into one.",
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
    id: "pdf-to-word",
    label: "PDF to Word",
    description: "Convert PDF content into DOCX.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "pdf-to-powerpoint",
    label: "PDF to PowerPoint",
    description: "Convert PDF pages into PPTX slides.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "pdf-to-excel",
    label: "PDF to Excel",
    description: "Extract PDF content into XLSX.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "word-to-pdf",
    label: "Word to PDF",
    description: "Convert DOC or DOCX into PDF.",
    accept: WORD_ACCEPT,
    multiple: false,
  },
  {
    id: "powerpoint-to-pdf",
    label: "PowerPoint to PDF",
    description: "Convert PPT or PPTX into PDF.",
    accept: POWERPOINT_ACCEPT,
    multiple: false,
  },
  {
    id: "excel-to-pdf",
    label: "Excel to PDF",
    description: "Convert XLS or XLSX into PDF.",
    accept: EXCEL_ACCEPT,
    multiple: false,
  },
  {
    id: "edit-pdf",
    label: "Edit PDF",
    description: "Apply structured edit operations to a PDF.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "operations",
        label: "Operations JSON",
        placeholder:
          '[{\"op\":\"add_text\",\"page\":1,\"x\":72,\"y\":72,\"text\":\"Approved\"}]',
        helper:
          "Supported ops: add_text, draw_rect, draw_line, whiteout, delete_pages, insert_blank_page.",
        required: true,
        type: "textarea",
      },
    ],
  },
  {
    id: "pdf-to-jpg",
    label: "PDF to JPG",
    description: "Export each page as JPG images in a ZIP.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "jpg-to-pdf",
    label: "JPG to PDF",
    description: "Convert JPG or PNG images into PDF.",
    accept: IMAGE_ACCEPT,
    multiple: true,
  },
  {
    id: "sign-pdf",
    label: "Sign PDF",
    description: "Place a visible electronic signature stamp.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "text",
        label: "Signature text",
        placeholder: "Jane Doe",
        required: true,
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1,3-4",
        helper: "Leave blank to sign all pages.",
      },
      {
        key: "x",
        label: "X position (pt)",
        placeholder: "36",
        type: "number",
      },
      {
        key: "y",
        label: "Y position (pt)",
        placeholder: "36",
        type: "number",
      },
    ],
  },
  {
    id: "watermark",
    label: "Watermark",
    description: "Stamp diagonal watermark text over pages.",
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
    id: "rotate",
    label: "Rotate PDF",
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
    id: "html-to-pdf",
    label: "HTML to PDF",
    description: "Capture an HTML page URL into PDF.",
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
    id: "unlock",
    label: "Unlock PDF",
    description: "Remove encryption from a PDF.",
    accept: PDF_ACCEPT,
    multiple: false,
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
    id: "organize-pdf",
    label: "Organize PDF",
    description: "Reorder, delete, and rotate pages in one step.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "order",
        label: "Final order",
        placeholder: "3,1,2",
        helper: "Leave blank to keep original order.",
      },
      {
        key: "delete",
        label: "Delete pages",
        placeholder: "4,7-8",
      },
      {
        key: "rotate",
        label: "Rotate map",
        placeholder: "1:90,2:180",
        helper: "Format page:angle with 90/180/270.",
      },
    ],
  },
  {
    id: "pdfa",
    label: "PDF to PDF/A",
    description: "Convert PDFs into archival PDF/A output.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "repair",
    label: "Repair PDF",
    description: "Rebuild a PDF to fix structural issues.",
    accept: PDF_ACCEPT,
    multiple: false,
  },
  {
    id: "page-numbers",
    label: "Page numbers",
    description: "Add centered footer page numbers.",
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
    id: "scan-to-pdf",
    label: "Scan to PDF",
    description: "Convert camera captures or images into a PDF scan file.",
    accept: IMAGE_ACCEPT,
    multiple: true,
  },
  {
    id: "ocr-pdf",
    label: "OCR PDF",
    description: "Create a searchable PDF with OCR text.",
    accept: PDF_ACCEPT,
    multiple: false,
    fields: [
      {
        key: "lang",
        label: "OCR language",
        placeholder: "eng",
        helper: "Use Tesseract language code, e.g. eng.",
      },
    ],
  },
  {
    id: "compare",
    label: "Compare PDF",
    description: "Generate a comparison report for two PDFs.",
    accept: PDF_ACCEPT,
    multiple: true,
  },
  {
    id: "redact",
    label: "Redact PDF",
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
    id: "crop",
    label: "Crop PDF",
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
  toolResult?: {
    status: "success" | "no_change" | "failed";
    method: string;
    original_bytes: number;
    output_bytes: number;
    savings_bytes: number;
    savings_percent: number;
    steps?: Array<{ name: string; ok: boolean; ms: number; notes?: string }>;
    warnings?: string[];
  } | null;
  createdAt: number;
  startedAt?: number;
};

const parseEnvFloat = (value: string | undefined, fallback: number) => {
  const parsed = Number.parseFloat(value ?? "");
  return Number.isFinite(parsed) ? parsed : fallback;
};

const estimateCompressTimeoutSeconds = (sizeBytes?: number) => {
  const override = parseEnvFloat(
    process.env.NEXT_PUBLIC_COMPRESS_TIMEOUT_SECONDS,
    0,
  );
  if (override > 0) {
    return override;
  }
  const base = parseEnvFloat(
    process.env.NEXT_PUBLIC_COMPRESS_TIMEOUT_BASE_SECONDS,
    120,
  );
  const perMb = parseEnvFloat(
    process.env.NEXT_PUBLIC_COMPRESS_TIMEOUT_PER_MB_SECONDS,
    3,
  );
  const maxTimeout = parseEnvFloat(
    process.env.NEXT_PUBLIC_COMPRESS_TIMEOUT_MAX_SECONDS,
    900,
  );
  const sizeMb =
    sizeBytes === undefined
      ? 1
      : Math.max(1, Math.ceil(sizeBytes / (1024 * 1024)));
  return Math.min(maxTimeout, base + sizeMb * perMb);
};

const formatMinutesSeconds = (seconds: number) => {
  const total = Math.max(0, Math.floor(seconds));
  const minutes = Math.floor(total / 60);
  const secs = total % 60;
  return `${minutes}:${secs.toString().padStart(2, "0")}`;
};

const formatSavingsPercent = (value: number) => {
  if (!Number.isFinite(value)) {
    return "0%";
  }
  return `${value.toFixed(1)}%`;
};

const formatJobTimestamp = (value: number) =>
  new Date(value).toLocaleString(undefined, {
    month: "short",
    day: "2-digit",
    hour: "2-digit",
    minute: "2-digit",
  });

const JobCard = ({
  job,
  onDownload,
  now,
}: {
  job: JobRecord;
  onDownload: (jobId: string, storageId: string, filename: string) => void;
  now: number;
}) => {
  const inputSize =
    job.inputs && job.inputs.length === 1 ? job.inputs[0]?.sizeBytes : undefined;
  const compressionResult =
    job.tool === "compress" ? job.toolResult ?? undefined : undefined;
  const noChange = compressionResult?.status === "no_change";
  const hasSavings =
    compressionResult?.status === "success" &&
    (compressionResult.savings_bytes ?? 0) > 0;
  const showCompressTimer =
    job.tool === "compress" &&
    (job.status === "queued" || job.status === "running");
  const timeoutSeconds = showCompressTimer
    ? estimateCompressTimeoutSeconds(inputSize)
    : 0;
  const elapsedSeconds =
    job.status === "running" && job.startedAt
      ? Math.floor((now - job.startedAt) / 1000)
      : 0;
  const remainingSeconds = Math.max(timeoutSeconds - elapsedSeconds, 0);

  return (
    <div className="paper-card p-5">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <div className="text-lg font-display text-ink-900">{job.tool}</div>
          <div className="text-xs text-ink-500">
            Status: {job.status}
            {job.progress !== undefined && ` (${job.progress}%)`}
            <span className="ml-2 text-[0.65rem]" title={new Date(job.createdAt).toISOString()}>
              {formatJobTimestamp(job.createdAt)}
            </span>
            {noChange && (
              <span className="ml-2 rounded-full bg-ink-900/10 px-2 py-0.5 text-[0.6rem] uppercase tracking-[0.2em] text-ink-600">
                No change
              </span>
            )}
          </div>
          {showCompressTimer && (
            <div className="text-xs text-ink-500">
              {job.status === "running" && job.startedAt
                ? `Timeout in ${formatMinutesSeconds(remainingSeconds)}`
                : `Timeout budget ${formatMinutesSeconds(timeoutSeconds)}`}
              {" · Based on file size"}
            </div>
          )}
          {noChange && (
            <div className="text-xs text-ink-500">
              No meaningful size reduction possible (already optimized or image-heavy).
            </div>
          )}
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
                {hasSavings && compressionResult && (
                  <div className="text-[0.65rem] text-ink-500">
                    Saved {formatSavingsPercent(compressionResult.savings_percent)} (
                    {formatBytes(compressionResult.savings_bytes)})
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
  const [now, setNow] = useState(() => Date.now());
  const devModeAvailable = process.env.NODE_ENV === "development";

  const generateUploadUrl = useMutation(api.files.generateUploadUrl);
  const createJob = useMutation(api.jobs.createJob);
  const jobsArgs = useMemo(
    () => (anonId ? { anonId } : {}),
    [anonId],
  );
  const jobs = useQuery(api.jobs.listJobs, jobsArgs) as JobRecord[] | undefined;
  const unlockNeedsPassword = useMemo(() => {
    if (!jobs) {
      return false;
    }
    const latestUnlock = [...jobs]
      .filter((job) => job.tool === "unlock")
      .sort((a, b) => b.createdAt - a.createdAt)[0];
    if (!latestUnlock) {
      return false;
    }
    const message = (latestUnlock.errorMessage ?? "").toLowerCase();
    return (
      latestUnlock.status === "failed" &&
      latestUnlock.errorCode === "USER_INPUT_INVALID" &&
      message.includes("password required")
    );
  }, [jobs]);

  useEffect(() => {
    const timer = window.setInterval(() => {
      setNow(Date.now());
    }, 1000);
    return () => {
      window.clearInterval(timer);
    };
  }, []);

  const tool = useMemo(() => {
    const base = TOOLS.find((item) => item.id === activeTool);
    if (!base) {
      return undefined;
    }
    if (base.id !== "unlock") {
      return base;
    }
    if (!unlockNeedsPassword) {
      return base;
    }
    return {
      ...base,
      description:
        "This PDF requires a password. Enter it to remove encryption.",
      fields: [
        {
          key: "password",
          label: "Password",
          placeholder: "Enter password",
          type: "password",
          required: true,
        },
      ] as ToolField[],
    };
  }, [activeTool, unlockNeedsPassword]);
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
    if (!needsFiles && tool.id === "html-to-pdf") {
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
              return (
                <button
                  key={item.id}
                  type="button"
                  onClick={() => selectTool(item.id)}
                  aria-pressed={item.id === activeTool}
                  className={`paper-card tool-card w-full p-4 text-left transition duration-200 ease-out focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-forest-500/40 hover:shadow-paper-lift active:translate-y-0.5 active:shadow-paper ${
                    item.id === activeTool
                      ? "border-forest-600/60 ring-2 ring-forest-600/20"
                      : "hover:border-forest-600/30 hover:ring-1 hover:ring-forest-600/10"
                  }`}
                >
                  <div>
                    <h2 className="text-lg font-display">{item.label}</h2>
                    <p className="text-xs text-ink-500">{item.description}</p>
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
              <div className="mt-4 rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-xs text-ink-600">
                <div>
                  <div className="font-semibold text-ink-700">Dev mode</div>
                  <div className="text-ink-500">
                    Local development bypass is always enabled (set
                    `ZENPDF_DEV_MODE=0` to enforce limits locally).
                  </div>
                </div>
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
                  {field.type === "textarea" ? (
                    <textarea
                      value={configValues[field.key] ?? ""}
                      onChange={(event) =>
                        updateConfig(field.key, event.target.value)
                      }
                      placeholder={field.placeholder}
                      rows={4}
                      className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                    />
                  ) : (
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
                  )}
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
                {activeJob.tool === "compress" && (
                  <div className="mt-2 text-xs text-ink-500">
                    {activeJob.status === "running" && activeJob.startedAt
                      ? `Timeout in ${formatMinutesSeconds(
                          Math.max(
                            estimateCompressTimeoutSeconds(
                              activeJob.inputs?.[0]?.sizeBytes,
                            ) -
                              Math.floor((now - activeJob.startedAt) / 1000),
                            0,
                          ),
                        )}`
                      : `Timeout budget ${formatMinutesSeconds(
                          estimateCompressTimeoutSeconds(
                            activeJob.inputs?.[0]?.sizeBytes,
                          ),
                        )}`}
                    {" · Based on file size"}
                  </div>
                )}
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
              <JobCard
                key={job._id}
                job={job}
                onDownload={handleDownload}
                now={now}
              />
            ))}
          </div>
        </section>
      </main>
    </div>
  );
}
