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
