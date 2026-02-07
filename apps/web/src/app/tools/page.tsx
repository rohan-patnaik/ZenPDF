"use client";

import { useEffect, useMemo, useRef, useState } from "react";
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
  min?: number;
  max?: number;
  step?: number;
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
        min: 90,
        max: 270,
        step: 90,
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

const TOOL_GROUPS: Array<{
  id: string;
  title: string;
  description: string;
  toolIds: ToolId[];
}> = [
  {
    id: "organize",
    title: "Organize & Prepare",
    description: "Restructure pages before sharing or conversion.",
    toolIds: ["merge", "split", "organize-pdf", "rotate", "crop", "page-numbers", "compress"],
  },
  {
    id: "convert",
    title: "Convert",
    description: "Move between PDF, Office, image, and web formats.",
    toolIds: [
      "pdf-to-word",
      "pdf-to-powerpoint",
      "pdf-to-excel",
      "word-to-pdf",
      "powerpoint-to-pdf",
      "excel-to-pdf",
      "pdf-to-jpg",
      "jpg-to-pdf",
      "html-to-pdf",
    ],
  },
  {
    id: "edit",
    title: "Edit & Annotate",
    description: "Apply visual edits, signatures, and redactions.",
    toolIds: ["edit-pdf", "sign-pdf", "watermark", "redact"],
  },
  {
    id: "protect",
    title: "Protect & Validate",
    description: "Secure or fix files before final delivery.",
    toolIds: ["unlock", "protect", "repair", "compare"],
  },
  {
    id: "capture",
    title: "Capture & Archive",
    description: "Create searchable or archival-ready output.",
    toolIds: ["scan-to-pdf", "ocr-pdf", "pdfa"],
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
  finishedAt?: number;
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
  const statusTone =
    job.status === "failed"
      ? "status-pill status-pill--error"
      : job.status === "done" || job.status === "succeeded"
        ? "status-pill status-pill--success"
        : "status-pill";

  return (
    <div className="paper-card p-5">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-start lg:justify-between">
        <div className="min-w-0 flex-1">
          <div className="flex flex-wrap items-center gap-2">
            <div className="text-base font-semibold text-ink-900">{job.tool}</div>
            <span className={statusTone}>
              {job.status}
              {job.progress !== undefined && ` (${job.progress}%)`}
            </span>
            {noChange && <span className="status-pill">No change</span>}
          </div>
          <div className="mt-1 text-xs text-ink-500" title={new Date(job.createdAt).toISOString()}>
            {formatJobTimestamp(job.createdAt)}
          </div>

          {showCompressTimer && (
            <div className="mt-1 text-xs text-ink-500">
              {job.status === "running" && job.startedAt
                ? `Timeout in ${formatMinutesSeconds(remainingSeconds)}`
                : `Timeout budget ${formatMinutesSeconds(timeoutSeconds)}`}
              {" · Based on file size"}
            </div>
          )}
          {noChange && (
            <div className="mt-1 text-xs text-ink-500">
              No meaningful size reduction possible (already optimized or image-heavy).
            </div>
          )}
          {job.errorCode && (
            <div className="mt-1 text-xs text-red-700">
              {job.errorCode} {job.errorMessage ? `- ${job.errorMessage}` : ""}
            </div>
          )}
          {(job.status === "queued" || job.status === "running") && (
            <div className="mt-3 h-1.5 w-full rounded-full bg-paper-200">
              <div
                className="h-full rounded-full bg-forest-600"
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
        <div className="flex w-full flex-col gap-2 lg:w-auto lg:min-w-[250px]">
          {(job.outputs ?? []).map((output) => (
            <div
              key={output.storageId}
              className="surface-muted flex items-center justify-between gap-3 p-3"
            >
              <div className="min-w-0">
                <div className="truncate text-xs font-medium text-ink-700">
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
                className="paper-button--ghost"
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
  const [contextJobId, setContextJobId] = useState<string | null>(null);
  const [contextToolId, setContextToolId] = useState<ToolId | null>(null);
  const [anonId, setAnonId] = useState<string | null>(() => getOrCreateAnonId());
  const [isSubmitting, setIsSubmitting] = useState(false);
  const [now, setNow] = useState(() => Date.now());
  const activeToolPanelRef = useRef<HTMLDivElement | null>(null);
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

  const toolGroups = useMemo(
    () =>
      TOOL_GROUPS.map((group) => ({
        ...group,
        tools: group.toolIds
          .map((toolId) => TOOLS.find((item) => item.id === toolId))
          .filter((item): item is ToolDefinition => Boolean(item)),
      })),
    [],
  );

  const activeGroup = useMemo(
    () => TOOL_GROUPS.find((group) => group.toolIds.includes(activeTool)),
    [activeTool],
  );
  const toolCatalog = (
    <div>
      {toolGroups.map((group) => (
        <section key={group.id} aria-label={group.title}>
          <div className="tool-group-label">{group.title}</div>
          <p className="mb-2 text-xs text-ink-500">{group.description}</p>
          <div className="space-y-2">
            {group.tools.map((item) => (
              <button
                key={item.id}
                type="button"
                onClick={() => selectTool(item.id)}
                aria-pressed={item.id === activeTool}
                className={`tool-card w-full rounded-xl border p-3 text-left transition focus-visible:outline-none ${
                  item.id === activeTool
                    ? "border-forest-600 bg-sage-200/45"
                    : "border-paper-200 bg-paper-50 hover:border-forest-500/50"
                }`}
              >
                <h3 className="text-sm font-semibold text-ink-900">{item.label}</h3>
                <p className="mt-1 text-xs text-ink-500">{item.description}</p>
              </button>
            ))}
          </div>
        </section>
      ))}
    </div>
  );

  const fileLabel = tool?.multiple ? "Choose files" : "Choose file";
  const requiredFields = tool?.fields?.filter((field) => field.required) ?? [];
  const optionalFields = tool?.fields?.filter((field) => !field.required) ?? [];

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

  const contextResult = useMemo(() => {
    if (!jobs || !contextJobId || !contextToolId || contextToolId !== activeTool) {
      return undefined;
    }
    const matchingJob = jobs.find((job) => job._id === contextJobId);
    const output = matchingJob?.outputs?.[0];
    const normalizedStatus =
      matchingJob?.status === "succeeded" || matchingJob?.status === "done"
        ? "succeeded"
        : matchingJob?.status === "failed"
          ? "failed"
          : "running";
    if (!matchingJob || !output || normalizedStatus !== "succeeded") {
      return undefined;
    }
    const inputSize =
      matchingJob.inputs && matchingJob.inputs.length === 1
        ? matchingJob.inputs[0]?.sizeBytes
        : undefined;
    const compressionResult =
      matchingJob.tool === "compress" ? matchingJob.toolResult ?? undefined : undefined;
    const noChange = compressionResult?.status === "no_change";
    const hasSavings =
      compressionResult?.status === "success" &&
      (compressionResult.savings_bytes ?? 0) > 0;
    return {
      jobId: matchingJob._id,
      storageId: output.storageId,
      filename: output.filename,
      status: normalizedStatus,
      outputSizeBytes: output.sizeBytes,
      inputSizeBytes: inputSize,
      completedAt: matchingJob.finishedAt ?? matchingJob.createdAt,
      noChange,
      hasSavings,
      savingsBytes: compressionResult?.savings_bytes,
      savingsPercent: compressionResult?.savings_percent,
    };
  }, [activeTool, contextJobId, contextToolId, jobs]);

  const contextResultSummary = useMemo(() => {
    if (!contextResult) {
      return "";
    }

    const outputSize =
      contextResult.outputSizeBytes !== undefined
        ? formatBytes(contextResult.outputSizeBytes)
        : undefined;
    const inputSize =
      contextResult.inputSizeBytes !== undefined
        ? formatBytes(contextResult.inputSizeBytes)
        : undefined;

    if (contextResult.hasSavings && outputSize && inputSize && contextResult.savingsBytes !== undefined) {
      return `Output ${outputSize} from ${inputSize} · Saved ${formatBytes(
        contextResult.savingsBytes,
      )} (${formatSavingsPercent(contextResult.savingsPercent ?? 0)})`;
    }

    if (contextResult.noChange && outputSize) {
      return `Output ${outputSize} · No size reduction`;
    }

    if (outputSize && inputSize) {
      return `Output ${outputSize} from ${inputSize}`;
    }

    if (outputSize) {
      return `Output ${outputSize}`;
    }

    return "Output ready for download";
  }, [contextResult]);

  const selectTool = (toolId: ToolId) => {
    setActiveTool(toolId);
    setFiles([]);
    setFileInputKey((value) => value + 1);
    setConfigValues({});
    setStatus(null);
    setContextJobId(null);
    setContextToolId(null);
    window.requestAnimationFrame(() => {
      activeToolPanelRef.current?.scrollIntoView({
        behavior: "smooth",
        block: "start",
      });
    });
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
      setContextJobId(response.jobId as string);
      setContextToolId(tool.id);
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

  const statusClassName =
    status === null
      ? ""
      : status.startsWith("Unable") ||
          status.startsWith("Enter") ||
          status.startsWith("Upload exactly") ||
          status.startsWith("Add at least")
        ? "alert alert--error"
        : status.startsWith("Job queued")
          ? "alert alert--success"
          : "alert";

  const renderField = (field: ToolField) => (
    <div key={field.key}>
      <label className="field-label" htmlFor={field.key}>
        {field.label}
      </label>
      {field.type === "textarea" ? (
        <textarea
          id={field.key}
          value={configValues[field.key] ?? ""}
          onChange={(event) => updateConfig(field.key, event.target.value)}
          placeholder={field.placeholder}
          rows={4}
          className="field-input"
        />
      ) : (
        <input
          id={field.key}
          type={field.type ?? "text"}
          inputMode={field.type === "number" ? "numeric" : undefined}
          min={field.min}
          max={field.max}
          step={field.step}
          value={configValues[field.key] ?? ""}
          onChange={(event) => updateConfig(field.key, event.target.value)}
          placeholder={field.placeholder}
          className="field-input"
        />
      )}
      {field.helper && <p className="field-helper">{field.helper}</p>}
    </div>
  );

  return (
    <div className="relative">
      <SiteHeader />
      <main className="mx-auto w-full max-w-6xl px-4 pb-14 pt-5 sm:px-6">
        <section className="paper-card p-5 sm:p-8">
          <span className="ink-label">Tool desk</span>
          <h1 className="mt-2 text-2xl sm:text-3xl">Run a tool in a clear, guided flow.</h1>
          <p className="mt-3 max-w-2xl text-sm text-ink-700">
            Each tool runs in the background worker, with limits enforced before
            processing begins. Choose a tool, upload files, then queue a job.
          </p>
          <div className="mt-5 grid gap-3 sm:grid-cols-3">
            <div className="task-step">
              <span className="task-step-index">1</span>
              <p className="mt-2 text-sm font-semibold text-ink-900">Select a tool</p>
              <p className="mt-1 text-xs text-ink-500">Grouped by intent so placement matches task flow.</p>
            </div>
            <div className="task-step">
              <span className="task-step-index">2</span>
              <p className="mt-2 text-sm font-semibold text-ink-900">Configure required fields</p>
              <p className="mt-1 text-xs text-ink-500">Required inputs come first. Optional settings are collapsed.</p>
            </div>
            <div className="task-step">
              <span className="task-step-index">3</span>
              <p className="mt-2 text-sm font-semibold text-ink-900">Queue and monitor</p>
              <p className="mt-1 text-xs text-ink-500">Progress, output files, and errors stay near the action area.</p>
            </div>
          </div>
        </section>

        <section className="mt-8 grid gap-6 lg:grid-cols-[0.9fr_1.1fr]">
          <aside className="paper-card order-2 p-4 sm:p-5 lg:order-1">
            <div className="mb-3 flex items-center justify-between">
              <h2 className="text-lg font-semibold">Tools</h2>
              <span className="status-pill">{TOOLS.length} available</span>
            </div>
            <p className="text-xs text-ink-500">
              Grouped by workflow to make feature placement practical and predictable.
            </p>
            <details className="mt-3 surface-muted p-3 lg:hidden">
              <summary className="cursor-pointer text-sm font-semibold text-ink-900">
                Browse full tool catalog
              </summary>
              <div className="mt-3">{toolCatalog}</div>
            </details>
            <div className="mt-3 hidden lg:block">{toolCatalog}</div>
          </aside>

          <div ref={activeToolPanelRef} className="paper-card order-1 scroll-mt-28 p-4 sm:p-6 lg:order-2">
            <div className="flex flex-wrap items-start justify-between gap-3">
              <div>
                <span className="ink-label">Active tool</span>
                <h2 className="mt-1 text-2xl">{tool?.label}</h2>
                {activeGroup && (
                  <p className="mt-1 text-xs text-ink-500">
                    Group: <span className="font-semibold text-ink-700">{activeGroup.title}</span>
                  </p>
                )}
              </div>
              <Link className="paper-button--ghost" href="/usage-capacity">
                View limits
              </Link>
            </div>
            <p className="mt-3 text-sm text-ink-700">{tool?.description}</p>
            <div className="mt-4 lg:hidden">
              <p className="field-label">Quick switch</p>
              <div className="mobile-scroll-row mt-2 flex gap-2 overflow-x-auto pb-1">
                {TOOLS.map((item) => (
                  <button
                    key={item.id}
                    type="button"
                    onClick={() => selectTool(item.id)}
                    aria-pressed={item.id === activeTool}
                    className={`whitespace-nowrap rounded-full border px-3 py-1.5 text-xs font-semibold ${
                      item.id === activeTool
                        ? "border-forest-700 bg-forest-600 text-white"
                        : "border-paper-300 bg-paper-50 text-ink-700"
                    }`}
                  >
                    {item.label}
                  </button>
                ))}
              </div>
            </div>
            <div className="mt-4 grid gap-2 sm:grid-cols-3">
              <div className="surface-muted p-3">
                <p className="ink-label">Input mode</p>
                <p className="mt-1 text-sm font-semibold text-ink-900">
                  {(tool?.requiresFiles ?? true) ? "File upload" : "URL input"}
                </p>
              </div>
              <div className="surface-muted p-3">
                <p className="ink-label">Files required</p>
                <p className="mt-1 text-sm font-semibold text-ink-900">
                  {(tool?.requiresFiles ?? true)
                    ? tool?.multiple
                      ? "One or more"
                      : "Single file"
                    : "No files"}
                </p>
              </div>
              <div className="surface-muted p-3">
                <p className="ink-label">Field scope</p>
                <p className="mt-1 text-sm font-semibold text-ink-900">
                  {requiredFields.length} required / {optionalFields.length} optional
                </p>
              </div>
            </div>

            {devModeAvailable && (
              <div className="alert mt-4">
                <strong className="font-semibold text-ink-700">Dev mode:</strong>{" "}
                Local development bypass is enabled (set `ZENPDF_DEV_MODE=0` to enforce
                limits locally).
              </div>
            )}

            <div className="mt-6 space-y-4">
              {(tool?.requiresFiles ?? true) && (
                <div>
                  <label className="field-label">Files</label>
                  <div className="mt-2 space-y-2">
                    <label className="paper-button--ghost inline-flex cursor-pointer items-center gap-2">
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
                    <p className="field-helper">
                      {files.length > 0
                        ? `${files.length} file(s) ready - ${formatBytes(totalSize)}`
                        : "No files selected yet."}
                    </p>
                    {files.length > 0 && (
                      <div className="surface-muted space-y-1 p-3 text-xs text-ink-700">
                        {files.map((file) => (
                          <div
                            key={`${file.name}-${file.size}-${file.lastModified}`}
                            className="flex items-center justify-between gap-3"
                          >
                            <span className="truncate">{file.name}</span>
                            <span className="text-ink-500">{formatBytes(file.size)}</span>
                          </div>
                        ))}
                      </div>
                    )}
                    {files.length > 0 && (
                      <button
                        type="button"
                        onClick={clearFiles}
                        className="paper-button--ghost w-fit"
                      >
                        Clear files
                      </button>
                    )}
                  </div>
                </div>
              )}

              {requiredFields.length > 0 && (
                <div>
                  <p className="field-label">Required settings</p>
                  <div className="mt-3 space-y-4">{requiredFields.map(renderField)}</div>
                </div>
              )}
              {requiredFields.length === 0 && (
                <div className="surface-muted p-3 text-sm text-ink-600">
                  No required settings for this tool.
                </div>
              )}

              {optionalFields.length > 0 && (
                <details className="surface-muted p-4">
                  <summary className="cursor-pointer text-sm font-semibold text-ink-700">
                    Advanced options
                  </summary>
                  <div className="mt-3 space-y-4">{optionalFields.map(renderField)}</div>
                </details>
              )}
            </div>

            <div className="mt-6 flex flex-wrap items-start gap-3">
              <button
                className="paper-button flex w-full items-center justify-center gap-2 sm:w-auto"
                type="button"
                onClick={handleSubmit}
                disabled={isSubmitting}
              >
                {isSubmitting && (
                  <span className="h-4 w-4 animate-spin rounded-full border-2 border-white/60 border-t-white" />
                )}
                {isSubmitting ? "Working..." : "Queue job"}
              </button>
              {contextResult && (
                <button
                  type="button"
                  className="paper-button--ghost w-full sm:w-auto"
                  onClick={() =>
                    handleDownload(
                      contextResult.jobId,
                      contextResult.storageId,
                      contextResult.filename,
                    )
                  }
                >
                  Download
                </button>
              )}
              {contextResult && (
                <div className="surface-muted flex w-full items-center gap-3 p-3 sm:min-w-[19rem] sm:flex-1">
                  <div className="min-w-0 flex-1">
                    <div className="flex flex-wrap items-center gap-2">
                      <p
                        className="truncate text-sm font-semibold text-ink-900"
                        title={contextResult.filename}
                      >
                        {contextResult.filename}
                      </p>
                      <span
                        className={
                          contextResult.status === "succeeded"
                            ? "status-pill status-pill--success"
                            : contextResult.status === "failed"
                              ? "status-pill status-pill--error"
                              : "status-pill"
                        }
                      >
                        {contextResult.status === "succeeded"
                          ? "Succeeded"
                          : contextResult.status === "failed"
                            ? "Failed"
                            : "Running"}
                      </span>
                    </div>
                    <p className="mt-1 text-xs text-ink-500">
                      {formatJobTimestamp(contextResult.completedAt)} · {contextResultSummary}
                    </p>
                  </div>
                </div>
              )}
            </div>

            {status && (
              <p role="status" aria-live="polite" className={`${statusClassName} mt-3`}>
                {status}
              </p>
            )}

            {activeJob && (activeJob.status === "queued" || activeJob.status === "running") && (
              <div className="surface-muted mt-4 p-4">
                <div className="flex items-center gap-2 text-xs text-ink-500">
                  <span className="h-2 w-2 animate-pulse rounded-full bg-forest-500" />
                  <span className="uppercase tracking-[0.14em]">Processing</span>
                </div>
                <div className="mt-2 text-sm text-ink-700">
                  {activeJob.status === "queued" ? "Queued" : "Running"}
                  {activeJob.progress !== undefined && ` - ${activeJob.progress}%`}
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
                    {" - Based on file size"}
                  </div>
                )}
                <div className="mt-2 h-2 w-full rounded-full bg-paper-200">
                  <div
                    className="h-full rounded-full bg-forest-600"
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

            {lastJobId && <p className="mt-3 text-xs text-ink-500">Latest job: {lastJobId}</p>}
          </div>
        </section>

        <section className="mt-10">
          <div className="flex items-center justify-between gap-3">
            <h2 className="text-2xl">Recent jobs</h2>
            <span className="status-pill">Auto-refresh</span>
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
