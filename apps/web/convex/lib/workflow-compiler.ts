import { throwFriendlyError } from "./errors";

export type WorkflowAssetKind =
  | "pdf"
  | "image"
  | "office"
  | "url"
  | "text"
  | "docx"
  | "xlsx";

export type WorkflowStep = {
  tool: string;
  config?: Record<string, unknown>;
};

export type WorkflowToolSpec = {
  id: string;
  label: string;
  input: WorkflowAssetKind;
  output: WorkflowAssetKind;
  multiInput?: boolean;
  premium?: boolean;
  requiredConfig?: string[];
};

export const MAX_WORKFLOW_STEPS = 6;

export const WORKFLOW_TOOL_SPECS: Record<string, WorkflowToolSpec> = {
  merge: {
    id: "merge",
    label: "Merge PDFs",
    input: "pdf",
    output: "pdf",
    multiInput: true,
  },
  split: {
    id: "split",
    label: "Split PDF",
    input: "pdf",
    output: "pdf",
  },
  compress: {
    id: "compress",
    label: "Compress PDF",
    input: "pdf",
    output: "pdf",
  },
  rotate: {
    id: "rotate",
    label: "Rotate pages",
    input: "pdf",
    output: "pdf",
  },
  "remove-pages": {
    id: "remove-pages",
    label: "Remove pages",
    input: "pdf",
    output: "pdf",
  },
  "reorder-pages": {
    id: "reorder-pages",
    label: "Reorder pages",
    input: "pdf",
    output: "pdf",
  },
  watermark: {
    id: "watermark",
    label: "Watermark",
    input: "pdf",
    output: "pdf",
    requiredConfig: ["text"],
  },
  "page-numbers": {
    id: "page-numbers",
    label: "Page numbers",
    input: "pdf",
    output: "pdf",
  },
  crop: {
    id: "crop",
    label: "Crop pages",
    input: "pdf",
    output: "pdf",
    requiredConfig: ["margins"],
  },
  unlock: {
    id: "unlock",
    label: "Unlock PDF",
    input: "pdf",
    output: "pdf",
    requiredConfig: ["password"],
  },
  protect: {
    id: "protect",
    label: "Protect PDF",
    input: "pdf",
    output: "pdf",
    requiredConfig: ["password"],
  },
  repair: {
    id: "repair",
    label: "Repair PDF",
    input: "pdf",
    output: "pdf",
  },
  redact: {
    id: "redact",
    label: "Redact text",
    input: "pdf",
    output: "pdf",
    requiredConfig: ["text"],
  },
  highlight: {
    id: "highlight",
    label: "Highlight text",
    input: "pdf",
    output: "pdf",
    requiredConfig: ["text"],
  },
  compare: {
    id: "compare",
    label: "Compare PDFs",
    input: "pdf",
    output: "text",
    multiInput: true,
  },
  "image-to-pdf": {
    id: "image-to-pdf",
    label: "Image to PDF",
    input: "image",
    output: "pdf",
    multiInput: true,
  },
  "pdf-to-jpg": {
    id: "pdf-to-jpg",
    label: "PDF to JPG",
    input: "pdf",
    output: "image",
  },
  "web-to-pdf": {
    id: "web-to-pdf",
    label: "Web to PDF",
    input: "url",
    output: "pdf",
    requiredConfig: ["url"],
  },
  "office-to-pdf": {
    id: "office-to-pdf",
    label: "Office to PDF",
    input: "office",
    output: "pdf",
  },
  pdfa: {
    id: "pdfa",
    label: "PDF to PDF/A",
    input: "pdf",
    output: "pdf",
    premium: true,
  },
  "pdf-to-text": {
    id: "pdf-to-text",
    label: "PDF to Text",
    input: "pdf",
    output: "text",
  },
  "pdf-to-word": {
    id: "pdf-to-word",
    label: "PDF to Word",
    input: "pdf",
    output: "docx",
  },
  "pdf-to-excel": {
    id: "pdf-to-excel",
    label: "PDF to Excel",
    input: "pdf",
    output: "xlsx",
  },
  "pdf-to-word-ocr": {
    id: "pdf-to-word-ocr",
    label: "PDF to Word (OCR)",
    input: "pdf",
    output: "docx",
    premium: true,
  },
  "pdf-to-excel-ocr": {
    id: "pdf-to-excel-ocr",
    label: "PDF to Excel (OCR)",
    input: "pdf",
    output: "xlsx",
    premium: true,
  },
};

const isRecord = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);

const isMissingConfigValue = (value: unknown) => {
  if (value === undefined || value === null) {
    return true;
  }
  if (typeof value === "string") {
    return value.trim().length === 0;
  }
  return false;
};

export const compileWorkflow = (steps: WorkflowStep[]) => {
  if (!Array.isArray(steps) || steps.length === 0) {
    throwFriendlyError("USER_INPUT_INVALID", { reason: "missing_steps" });
  }

  if (steps.length > MAX_WORKFLOW_STEPS) {
    throwFriendlyError("USER_INPUT_INVALID", {
      reason: "too_many_steps",
      maxSteps: MAX_WORKFLOW_STEPS,
    });
  }

  let inputKind: WorkflowAssetKind | null = null;
  let outputKind: WorkflowAssetKind | null = null;
  let hasPremiumTools = false;

  steps.forEach((step, index) => {
    const spec = WORKFLOW_TOOL_SPECS[step.tool];
    if (!spec) {
      throwFriendlyError("USER_INPUT_INVALID", {
        reason: "unknown_tool",
        tool: step.tool,
      });
    }

    if (step.config !== undefined && !isRecord(step.config)) {
      throwFriendlyError("USER_INPUT_INVALID", {
        reason: "invalid_config",
        tool: step.tool,
      });
    }

    const config = isRecord(step.config) ? step.config : undefined;

    if (spec.requiredConfig && spec.requiredConfig.length > 0) {
      const missing = spec.requiredConfig.filter((key) =>
        isMissingConfigValue(config?.[key]),
      );
      if (missing.length > 0) {
        throwFriendlyError("USER_INPUT_INVALID", {
          reason: "missing_config",
          tool: step.tool,
          fields: missing.join(","),
        });
      }
    }

    if (index === 0) {
      inputKind = spec.input;
    } else {
      if (spec.multiInput) {
        throwFriendlyError("USER_INPUT_INVALID", {
          reason: "multi_input_step",
          tool: step.tool,
        });
      }
      if (outputKind && spec.input !== outputKind) {
        throwFriendlyError("USER_INPUT_INVALID", {
          reason: "incompatible_chain",
          tool: step.tool,
          expected: outputKind,
          received: spec.input,
        });
      }
    }

    if (index < steps.length - 1 && spec.output !== "pdf") {
      throwFriendlyError("USER_INPUT_INVALID", {
        reason: "non_pdf_mid_chain",
        tool: step.tool,
      });
    }

    outputKind = spec.output;
    if (spec.premium) {
      hasPremiumTools = true;
    }
  });

  if (!inputKind || !outputKind) {
    throwFriendlyError("USER_INPUT_INVALID", { reason: "invalid_steps" });
  }

  return {
    inputKind,
    outputKind,
    hasPremiumTools,
  };
};
