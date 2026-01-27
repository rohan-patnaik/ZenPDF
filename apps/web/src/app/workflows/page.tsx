"use client";

import { useMemo, useState } from "react";
import Link from "next/link";
import { useMutation, useQuery } from "convex/react";

import SiteHeader from "@/components/SiteHeader";
import { api } from "@/lib/convex";

type WorkflowAssetKind = "pdf" | "image" | "office" | "url" | "text" | "docx" | "xlsx";

type ToolField = {
  key: string;
  label: string;
  placeholder: string;
  helper?: string;
  type?: "text" | "number";
};

type WorkflowToolDefinition = {
  id: string;
  label: string;
  description: string;
  input: WorkflowAssetKind;
  output: WorkflowAssetKind;
  tier?: "Standard" | "Premium";
  multiInput?: boolean;
  fields?: ToolField[];
};

type WorkflowStepDraft = {
  tool: string;
  config: Record<string, string>;
};

type WorkflowSummary = {
  _id: string;
  name: string;
  description?: string;
  steps: Array<{ tool: string; config?: Record<string, unknown> }>;
  teamId?: string;
  teamName?: string;
  ownerName?: string;
  ownerEmail?: string;
  createdAt: number;
  updatedAt: number;
  inputKind: WorkflowAssetKind;
  outputKind: WorkflowAssetKind;
  canManage: boolean;
};

const MAX_WORKFLOW_STEPS = 6;

const WORKFLOW_TOOLS: WorkflowToolDefinition[] = [
  {
    id: "merge",
    label: "Merge PDFs",
    description: "Combine multiple PDFs into a single dossier.",
    input: "pdf",
    output: "pdf",
    multiInput: true,
  },
  {
    id: "split",
    label: "Split PDF",
    description: "Split by ranges or into individual pages.",
    input: "pdf",
    output: "pdf",
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
    input: "pdf",
    output: "pdf",
  },
  {
    id: "rotate",
    label: "Rotate pages",
    description: "Rotate pages by 90, 180, or 270 degrees.",
    input: "pdf",
    output: "pdf",
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
    input: "pdf",
    output: "pdf",
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
    input: "pdf",
    output: "pdf",
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
    input: "pdf",
    output: "pdf",
    fields: [
      {
        key: "text",
        label: "Watermark text",
        placeholder: "CONFIDENTIAL",
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
      },
    ],
  },
  {
    id: "page-numbers",
    label: "Page numbers",
    description: "Add page numbers to the footer of each page.",
    input: "pdf",
    output: "pdf",
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
      },
    ],
  },
  {
    id: "crop",
    label: "Crop pages",
    description: "Trim margins from each page.",
    input: "pdf",
    output: "pdf",
    fields: [
      {
        key: "margins",
        label: "Margins (pt)",
        placeholder: "10,10,10,10",
        helper: "Top,right,bottom,left in points (72 = 1 inch).",
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
      },
    ],
  },
  {
    id: "redact",
    label: "Redact text",
    description: "Find and black out matching text in the PDF.",
    input: "pdf",
    output: "pdf",
    fields: [
      {
        key: "text",
        label: "Text to redact",
        placeholder: "CONFIDENTIAL",
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
      },
    ],
  },
  {
    id: "highlight",
    label: "Highlight text",
    description: "Highlight matching text across the PDF.",
    input: "pdf",
    output: "pdf",
    fields: [
      {
        key: "text",
        label: "Text to highlight",
        placeholder: "CONFIDENTIAL",
      },
      {
        key: "pages",
        label: "Pages",
        placeholder: "1-3,6",
      },
    ],
  },
  {
    id: "compare",
    label: "Compare PDFs",
    description: "Generate a text report comparing two PDFs.",
    input: "pdf",
    output: "text",
    multiInput: true,
  },
  {
    id: "unlock",
    label: "Unlock PDF",
    description: "Remove a password so the file opens freely.",
    input: "pdf",
    output: "pdf",
    fields: [
      {
        key: "password",
        label: "Current password",
        placeholder: "Enter current password",
      },
    ],
  },
  {
    id: "protect",
    label: "Protect PDF",
    description: "Encrypt a PDF with a new password.",
    input: "pdf",
    output: "pdf",
    fields: [
      {
        key: "password",
        label: "New password",
        placeholder: "Create a password",
      },
    ],
  },
  {
    id: "repair",
    label: "Repair PDF",
    description: "Rebuild a PDF file to fix structural issues.",
    input: "pdf",
    output: "pdf",
  },
  {
    id: "image-to-pdf",
    label: "Image to PDF",
    description: "Convert JPG or PNG images into a PDF.",
    input: "image",
    output: "pdf",
    multiInput: true,
  },
  {
    id: "pdf-to-jpg",
    label: "PDF to JPG",
    description: "Export each page as a JPG.",
    input: "pdf",
    output: "image",
  },
  {
    id: "web-to-pdf",
    label: "Web to PDF",
    description: "Capture a URL into a printable PDF snapshot.",
    input: "url",
    output: "pdf",
    fields: [
      {
        key: "url",
        label: "Web address",
        placeholder: "https://example.com",
      },
    ],
  },
  {
    id: "office-to-pdf",
    label: "Office to PDF",
    description: "Convert Word, Excel, or PowerPoint files to PDF.",
    input: "office",
    output: "pdf",
  },
  {
    id: "pdfa",
    label: "PDF to PDF/A",
    description: "Convert PDFs into archival PDF/A-2b output.",
    input: "pdf",
    output: "pdf",
    tier: "Premium",
  },
  {
    id: "pdf-to-text",
    label: "PDF to Text",
    description: "Extract raw text into a plain TXT file.",
    input: "pdf",
    output: "text",
  },
  {
    id: "pdf-to-word",
    label: "PDF to Word",
    description: "Extract text from digital PDFs into DOCX.",
    input: "pdf",
    output: "docx",
  },
  {
    id: "pdf-to-excel",
    label: "PDF to Excel",
    description: "Extract text into a simple XLSX worksheet.",
    input: "pdf",
    output: "xlsx",
  },
  {
    id: "pdf-to-word-ocr",
    label: "PDF to Word (OCR)",
    description: "OCR scanned PDFs into a Word document.",
    input: "pdf",
    output: "docx",
    tier: "Premium",
  },
  {
    id: "pdf-to-excel-ocr",
    label: "PDF to Excel (OCR)",
    description: "OCR scanned PDFs into a simple XLSX worksheet.",
    input: "pdf",
    output: "xlsx",
    tier: "Premium",
  },
];

const WORKFLOW_TOOL_MAP = new Map(WORKFLOW_TOOLS.map((tool) => [tool.id, tool]));

const KIND_LABELS: Record<WorkflowAssetKind, string> = {
  pdf: "PDF",
  image: "Images",
  office: "Office",
  url: "URL",
  text: "Text",
  docx: "Word",
  xlsx: "Excel",
};

const buildStepsPayload = (steps: WorkflowStepDraft[]) =>
  steps.map((step) => {
    const tool = WORKFLOW_TOOL_MAP.get(step.tool);
    const numericKeys = new Set(
      tool?.fields?.filter((field) => field.type === "number").map((field) => field.key) ?? [],
    );
    const config: Record<string, string | number> = {};
    Object.entries(step.config).forEach(([key, value]) => {
      const trimmed = value.trim();
      if (!trimmed) {
        return;
      }
      if (numericKeys.has(key)) {
        const numericValue = Number(trimmed);
        config[key] = Number.isNaN(numericValue) ? trimmed : numericValue;
        return;
      }
      config[key] = trimmed;
    });
    return {
      tool: step.tool,
      config: Object.keys(config).length > 0 ? config : undefined,
    };
  });

const validateDraft = (steps: WorkflowStepDraft[]) => {
  if (steps.length === 0) {
    return { ok: false, message: "Add at least one step." };
  }

  if (steps.length > MAX_WORKFLOW_STEPS) {
    return { ok: false, message: `Keep workflows to ${MAX_WORKFLOW_STEPS} steps.` };
  }

  let inputKind: WorkflowAssetKind | null = null;
  let outputKind: WorkflowAssetKind | null = null;

  for (let index = 0; index < steps.length; index += 1) {
    const step = steps[index];
    const tool = WORKFLOW_TOOL_MAP.get(step.tool);
    if (!tool) {
      return { ok: false, message: "Select a valid tool for each step." };
    }

    if (index === 0) {
      inputKind = tool.input;
    } else {
      if (tool.multiInput) {
        return { ok: false, message: "Multi-file tools can only be the first step." };
      }
      if (outputKind && tool.input !== outputKind) {
        return { ok: false, message: "Each step must accept the previous output." };
      }
    }

    if (index < steps.length - 1 && tool.output !== "pdf") {
      return { ok: false, message: "Non-PDF outputs must be the final step." };
    }

    outputKind = tool.output;
  }

  if (!inputKind || !outputKind) {
    return { ok: false, message: "Workflow steps are incomplete." };
  }

  return { ok: true, inputKind, outputKind };
};

export default function WorkflowsPage() {
  const [workflowName, setWorkflowName] = useState("");
  const [workflowDescription, setWorkflowDescription] = useState("");
  const [workflowSteps, setWorkflowSteps] = useState<WorkflowStepDraft[]>([
    { tool: "merge", config: {} },
  ]);
  const [saveScope, setSaveScope] = useState<string>("personal");
  const [status, setStatus] = useState<string | null>(null);
  const [teamStatus, setTeamStatus] = useState<string | null>(null);
  const [teamName, setTeamName] = useState("");
  const [memberEmails, setMemberEmails] = useState<Record<string, string>>({});
  const [isSaving, setIsSaving] = useState(false);
  const [isTeamAction, setIsTeamAction] = useState(false);

  const viewer = useQuery(api.users.getViewer, {});
  const isPremium = viewer?.tier === "PREMIUM";
  const shouldFetch = Boolean(isPremium);
  const workflows = useQuery(api.workflows.listWorkflows, shouldFetch ? {} : undefined);
  const teams = useQuery(api.teams.listTeams, shouldFetch ? {} : undefined);
  const createWorkflow = useMutation(api.workflows.createWorkflow);
  const deleteWorkflow = useMutation(api.workflows.deleteWorkflow);
  const createTeam = useMutation(api.teams.createTeam);
  const addTeamMember = useMutation(api.teams.addTeamMember);
  const removeTeamMember = useMutation(api.teams.removeTeamMember);

  const workflowValidation = useMemo(
    () => validateDraft(workflowSteps),
    [workflowSteps],
  );
  const teamsById = useMemo(
    () => new Map((teams ?? []).map((team) => [team._id, team])),
    [teams],
  );

  const personalWorkflows = useMemo(
    () => (workflows ?? []).filter((workflow) => !workflow.teamId),
    [workflows],
  );
  const teamWorkflows = useMemo(
    () => (workflows ?? []).filter((workflow) => workflow.teamId),
    [workflows],
  );
  const teamWorkflowGroups = useMemo(() => {
    const grouped = new Map<string, WorkflowSummary[]>();
    teamWorkflows.forEach((workflow) => {
      if (!workflow.teamId) {
        return;
      }
      const list = grouped.get(workflow.teamId) ?? [];
      list.push(workflow);
      grouped.set(workflow.teamId, list);
    });
    return grouped;
  }, [teamWorkflows]);

  const updateStepTool = (index: number, toolId: string) => {
    setWorkflowSteps((prev) =>
      prev.map((step, stepIndex) =>
        stepIndex === index ? { tool: toolId, config: {} } : step,
      ),
    );
  };

  const updateStepConfig = (index: number, key: string, value: string) => {
    setWorkflowSteps((prev) =>
      prev.map((step, stepIndex) =>
        stepIndex === index
          ? { ...step, config: { ...step.config, [key]: value } }
          : step,
      ),
    );
  };

  const addStep = () => {
    setWorkflowSteps((prev) => {
      if (prev.length >= MAX_WORKFLOW_STEPS) {
        return prev;
      }
      return [...prev, { tool: "compress", config: {} }];
    });
  };

  const removeStep = (index: number) => {
    setWorkflowSteps((prev) => prev.filter((_, stepIndex) => stepIndex !== index));
  };

  const handleSaveWorkflow = async () => {
    if (isSaving) {
      return;
    }
    if (!workflowName.trim()) {
      setStatus("Name the workflow before saving.");
      return;
    }
    if (!workflowValidation.ok) {
      setStatus(workflowValidation.message);
      return;
    }
    if (!isPremium) {
      setStatus("Workflow presets require Premium access.");
      return;
    }

    setIsSaving(true);
    setStatus("Saving workflow...");
    try {
      const payloadSteps = buildStepsPayload(workflowSteps);
      const teamId = saveScope !== "personal" ? saveScope : undefined;
      await createWorkflow({
        name: workflowName.trim(),
        description: workflowDescription.trim() || undefined,
        steps: payloadSteps,
        teamId,
      });
      setStatus("Workflow saved.");
      setWorkflowName("");
      setWorkflowDescription("");
      setWorkflowSteps([{ tool: "merge", config: {} }]);
      setSaveScope("personal");
    } catch (error) {
      console.error("Failed to save workflow:", error);
      setStatus("Unable to save workflow. Check the steps and try again.");
    } finally {
      setIsSaving(false);
    }
  };

  const handleDeleteWorkflow = async (workflowId: string) => {
    setStatus("Removing workflow...");
    try {
      await deleteWorkflow({ workflowId });
      setStatus("Workflow removed.");
    } catch (error) {
      console.error("Failed to remove workflow:", error);
      setStatus("Unable to remove workflow.");
    }
  };

  const handleLoadWorkflow = (workflow: WorkflowSummary) => {
    setWorkflowName(`Copy of ${workflow.name}`);
    setWorkflowDescription(workflow.description ?? "");
    setWorkflowSteps(
      workflow.steps.map((step) => ({
        tool: step.tool,
        config: Object.entries(step.config ?? {}).reduce<Record<string, string>>(
          (acc, [key, value]) => {
            acc[key] = String(value);
            return acc;
          },
          {},
        ),
      })),
    );
    if (workflow.teamId && teamsById.has(workflow.teamId)) {
      setSaveScope(workflow.teamId);
    } else {
      setSaveScope("personal");
    }
    setStatus("Preset loaded into the builder.");
  };

  const handleCreateTeam = async () => {
    if (isTeamAction) {
      return;
    }
    if (!teamName.trim()) {
      setTeamStatus("Enter a team name.");
      return;
    }
    setIsTeamAction(true);
    setTeamStatus("Creating team...");
    try {
      await createTeam({ name: teamName.trim() });
      setTeamName("");
      setTeamStatus("Team created.");
    } catch (error) {
      console.error("Failed to create team:", error);
      setTeamStatus("Unable to create team.");
    } finally {
      setIsTeamAction(false);
    }
  };

  const handleAddMember = async (teamId: string) => {
    if (isTeamAction) {
      return;
    }
    const email = memberEmails[teamId] ?? "";
    if (!email.trim()) {
      setTeamStatus("Enter a member email.");
      return;
    }
    setIsTeamAction(true);
    setTeamStatus("Adding member...");
    try {
      await addTeamMember({ teamId, email });
      setMemberEmails((prev) => ({ ...prev, [teamId]: "" }));
      setTeamStatus("Member added.");
    } catch (error) {
      console.error("Failed to add member:", error);
      setTeamStatus("Unable to add member. They must have an account.");
    } finally {
      setIsTeamAction(false);
    }
  };

  const handleRemoveMember = async (teamId: string, memberId: string) => {
    if (isTeamAction) {
      return;
    }
    setIsTeamAction(true);
    setTeamStatus("Removing member...");
    try {
      await removeTeamMember({ teamId, memberId });
      setTeamStatus("Member removed.");
    } catch (error) {
      console.error("Failed to remove member:", error);
      setTeamStatus("Unable to remove member.");
    } finally {
      setIsTeamAction(false);
    }
  };

  const renderWorkflowCard = (workflow: WorkflowSummary) => {
    const stepLabels = workflow.steps.map((step) => {
      const tool = WORKFLOW_TOOL_MAP.get(step.tool);
      return tool?.label ?? step.tool;
    });
    return (
      <div key={workflow._id} className="rounded-[22px] border border-ink-900/10 bg-paper-100 p-4">
        <div className="flex flex-wrap items-start justify-between gap-3">
          <div>
            <h3 className="text-lg font-display text-ink-900">{workflow.name}</h3>
            {workflow.description && (
              <p className="mt-1 text-xs text-ink-500">{workflow.description}</p>
            )}
          </div>
          {workflow.canManage && (
            <button
              type="button"
              className="paper-button--ghost text-xs"
              onClick={() => handleDeleteWorkflow(workflow._id)}
            >
              Delete
            </button>
          )}
        </div>
        <p className="mt-2 text-xs text-ink-500">
          Input {KIND_LABELS[workflow.inputKind]} | Output {KIND_LABELS[workflow.outputKind]} |{" "}
          {workflow.steps.length} steps
        </p>
        {workflow.teamName && (
          <p className="mt-1 text-xs text-ink-500">Team: {workflow.teamName}</p>
        )}
        {(workflow.ownerName || workflow.ownerEmail) && (
          <p className="mt-1 text-xs text-ink-500">
            Owner: {workflow.ownerName ?? workflow.ownerEmail}
          </p>
        )}
        <div className="mt-3 flex flex-wrap gap-2">
          {stepLabels.map((label, index) => (
            <span
              key={`${workflow._id}-${index}`}
              className="rounded-full border border-ink-900/10 bg-paper-50 px-3 py-1 text-[0.65rem] uppercase tracking-[0.18em] text-ink-600"
            >
              {label}
            </span>
          ))}
        </div>
        <div className="mt-3">
          <button
            type="button"
            className="paper-button--ghost text-xs"
            onClick={() => handleLoadWorkflow(workflow)}
          >
            Load preset
          </button>
        </div>
      </div>
    );
  };

  return (
    <div className="relative">
      <SiteHeader />
      <main className="relative z-10 mx-auto w-full max-w-6xl px-6 pb-16">
        <section className="paper-card mt-4 p-8">
          <span className="ink-label">Workflow studio</span>
          <h1 className="mt-3 text-4xl">Build reusable PDF workflows for teams.</h1>
          <p className="mt-3 max-w-2xl text-base text-ink-700">
            Draft multi-step presets, capture common configurations, and share templates
            with your team. Workflows are premium-only while the team space evolves.
          </p>
        </section>

        {!viewer && (
          <section className="mt-6 rounded-[24px] border border-ink-900/10 bg-paper-100 p-6 text-sm text-ink-700">
            Loading your workflow studio...
          </section>
        )}

        {viewer && !isPremium && (
          <section className="mt-6 paper-card p-6">
            <span className="ink-label">Premium required</span>
            <h2 className="mt-3 text-2xl">Workflow presets are unlocked for supporters.</h2>
            <p className="mt-2 text-sm text-ink-700">
              Sign in with a Premium account or run ZenPDF locally to enable workflows and
              team templates.
            </p>
            <div className="mt-4 flex flex-wrap gap-3">
              <Link className="paper-button" href="/usage-capacity">
                Review premium limits
              </Link>
              <Link className="paper-button--ghost" href="/tools">
                Run a one-off tool
              </Link>
            </div>
          </section>
        )}

        {viewer && isPremium && (
          <section className="mt-8 grid gap-6 lg:grid-cols-[1.15fr_0.85fr]">
            <div className="space-y-6">
              <div className="paper-card p-6">
                <div className="flex items-center justify-between">
                  <div>
                    <span className="ink-label">Preset builder</span>
                    <h2 className="mt-2 text-2xl">Compose a multi-step workflow.</h2>
                  </div>
                  <span className="text-xs uppercase tracking-[0.2em] text-ink-500">
                    Max {MAX_WORKFLOW_STEPS} steps
                  </span>
                </div>
                <div className="mt-4 grid gap-4 md:grid-cols-2">
                  <div>
                    <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                      Workflow name
                    </label>
                    <input
                      type="text"
                      value={workflowName}
                      onChange={(event) => setWorkflowName(event.target.value)}
                      placeholder="Quarterly report prep"
                      className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                    />
                  </div>
                  <div>
                    <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                      Save scope
                    </label>
                    <select
                      value={saveScope}
                      onChange={(event) => setSaveScope(event.target.value)}
                      className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                    >
                      <option value="personal">Personal library</option>
                      {(teams ?? []).map((team) => (
                        <option key={team._id} value={team._id}>
                          Team: {team.name}
                        </option>
                      ))}
                    </select>
                  </div>
                  <div className="md:col-span-2">
                    <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                      Description
                    </label>
                    <input
                      type="text"
                      value={workflowDescription}
                      onChange={(event) => setWorkflowDescription(event.target.value)}
                      placeholder="Watermark, compress, and lock client packets."
                      className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                    />
                  </div>
                </div>

                <div className="mt-6 space-y-4">
                  {workflowSteps.map((step, index) => {
                    const tool = WORKFLOW_TOOL_MAP.get(step.tool);
                    return (
                      <div
                        key={`${step.tool}-${index}`}
                        className="rounded-[24px] border border-ink-900/10 bg-paper-100 p-4"
                      >
                        <div className="flex items-center justify-between">
                          <span className="text-xs uppercase tracking-[0.2em] text-ink-500">
                            Step {index + 1}
                          </span>
                          {workflowSteps.length > 1 && (
                            <button
                              type="button"
                              className="text-xs uppercase tracking-[0.2em] text-ink-500"
                              onClick={() => removeStep(index)}
                            >
                              Remove
                            </button>
                          )}
                        </div>
                        <div className="mt-3">
                          <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                            Tool
                          </label>
                          <select
                            value={step.tool}
                            onChange={(event) => updateStepTool(index, event.target.value)}
                            className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-50 px-4 py-3 text-sm"
                          >
                            {WORKFLOW_TOOLS.map((item) => (
                              <option key={item.id} value={item.id}>
                                {item.label}
                              </option>
                            ))}
                          </select>
                          {tool?.description && (
                            <p className="mt-2 text-xs text-ink-500">{tool.description}</p>
                          )}
                        </div>

                        {tool?.fields?.map((field) => (
                          <div key={`${step.tool}-${field.key}`} className="mt-4">
                            <label className="text-xs uppercase tracking-[0.2em] text-ink-500">
                              {field.label}
                            </label>
                            <input
                              type={field.type ?? "text"}
                              inputMode={field.type === "number" ? "numeric" : undefined}
                              value={step.config[field.key] ?? ""}
                              onChange={(event) =>
                                updateStepConfig(index, field.key, event.target.value)
                              }
                              placeholder={field.placeholder}
                              className="mt-2 w-full rounded-[18px] border border-ink-900/10 bg-paper-50 px-4 py-3 text-sm"
                            />
                            {field.helper && (
                              <p className="mt-2 text-xs text-ink-500">{field.helper}</p>
                            )}
                          </div>
                        ))}
                      </div>
                    );
                  })}
                </div>

                <div className="mt-5 flex flex-wrap items-center gap-3">
                  <button
                    type="button"
                    className="paper-button--ghost"
                    onClick={addStep}
                    disabled={workflowSteps.length >= MAX_WORKFLOW_STEPS}
                  >
                    Add step
                  </button>
                  <button
                    type="button"
                    className="paper-button"
                    onClick={handleSaveWorkflow}
                    disabled={isSaving}
                  >
                    Save workflow
                  </button>
                  {status && <p className="text-sm text-ink-700">{status}</p>}
                </div>
                {workflowValidation.ok && (
                  <p className="mt-2 text-xs text-ink-500">
                    Input {KIND_LABELS[workflowValidation.inputKind]} | Output{" "}
                    {KIND_LABELS[workflowValidation.outputKind]}
                  </p>
                )}
              </div>

              <div className="paper-card p-6">
                <span className="ink-label">Teams</span>
                <h2 className="mt-2 text-2xl">Share presets with collaborators.</h2>
                <p className="mt-2 text-sm text-ink-700">
                  Create a team space, add members by email, and save shared templates
                  to reuse across projects.
                </p>
                <div className="mt-4 flex flex-wrap items-center gap-3">
                  <input
                    type="text"
                    value={teamName}
                    onChange={(event) => setTeamName(event.target.value)}
                    placeholder="Team name"
                    className="w-full flex-1 rounded-[18px] border border-ink-900/10 bg-paper-100 px-4 py-3 text-sm"
                  />
                  <button
                    type="button"
                    className="paper-button"
                    onClick={handleCreateTeam}
                    disabled={isTeamAction}
                  >
                    Create team
                  </button>
                </div>
                {teamStatus && <p className="mt-3 text-sm text-ink-700">{teamStatus}</p>}

                <div className="mt-6 space-y-4">
                  {(teams ?? []).length === 0 && (
                    <div className="rounded-[20px] border border-ink-900/10 bg-paper-100 p-4 text-sm text-ink-600">
                      No teams yet. Create one to start sharing workflows.
                    </div>
                  )}
                  {(teams ?? []).map((team) => (
                    <div
                      key={team._id}
                      className="rounded-[22px] border border-ink-900/10 bg-paper-100 p-4"
                    >
                      <div className="flex items-center justify-between">
                        <div>
                          <h3 className="text-lg font-display text-ink-900">{team.name}</h3>
                          <p className="text-xs text-ink-500">
                            {team.members.length} member{team.members.length === 1 ? "" : "s"}
                          </p>
                        </div>
                        {team.isOwner && (
                          <span className="rounded-full border border-ink-900/10 bg-rose-100 px-3 py-1 text-[0.6rem] uppercase tracking-[0.2em] text-ink-700">
                            Owner
                          </span>
                        )}
                      </div>
                      <div className="mt-3 space-y-2">
                        {team.members.map((member) => (
                          <div
                            key={member._id}
                            className="flex flex-wrap items-center justify-between gap-3 rounded-[16px] border border-ink-900/10 bg-paper-50 px-3 py-2 text-xs text-ink-600"
                          >
                            <span>
                              {member.name ?? member.email ?? member.userId} 1 {member.role}
                            </span>
                            {team.isOwner && member.role !== "owner" && (
                              <button
                                type="button"
                                className="text-[0.6rem] uppercase tracking-[0.2em] text-ink-500"
                                onClick={() => handleRemoveMember(team._id, member._id)}
                              >
                                Remove
                              </button>
                            )}
                          </div>
                        ))}
                      </div>
                      {team.isOwner && (
                        <div className="mt-4 flex flex-wrap items-center gap-3">
                          <input
                            type="email"
                            value={memberEmails[team._id] ?? ""}
                            onChange={(event) =>
                              setMemberEmails((prev) => ({
                                ...prev,
                                [team._id]: event.target.value,
                              }))
                            }
                            placeholder="member@company.com"
                            className="w-full flex-1 rounded-[18px] border border-ink-900/10 bg-paper-50 px-4 py-3 text-sm"
                          />
                          <button
                            type="button"
                            className="paper-button--ghost"
                            onClick={() => handleAddMember(team._id)}
                            disabled={isTeamAction}
                          >
                            Add member
                          </button>
                        </div>
                      )}
                    </div>
                  ))}
                </div>
              </div>
            </div>

            <div className="space-y-6">
              <div className="paper-card p-6">
                <div className="flex items-center justify-between">
                  <div>
                    <span className="ink-label">Personal presets</span>
                    <h2 className="mt-2 text-2xl">Your saved workflows.</h2>
                  </div>
                </div>
                <div className="mt-4 space-y-4">
                  {personalWorkflows.length === 0 && (
                    <div className="rounded-[20px] border border-ink-900/10 bg-paper-100 p-4 text-sm text-ink-600">
                      No presets yet. Save a workflow to see it here.
                    </div>
                  )}
                  {personalWorkflows.map((workflow) => renderWorkflowCard(workflow))}
                </div>
              </div>

              <div className="paper-card p-6">
                <div className="flex items-center justify-between">
                  <div>
                    <span className="ink-label">Team templates</span>
                    <h2 className="mt-2 text-2xl">Shared across teams.</h2>
                  </div>
                </div>
                <div className="mt-4 space-y-4">
                  {teamWorkflows.length === 0 && (
                    <div className="rounded-[20px] border border-ink-900/10 bg-paper-100 p-4 text-sm text-ink-600">
                      Team templates will appear here once shared.
                    </div>
                  )}
                  {Array.from(teamWorkflowGroups.entries()).map(([teamId, grouped]) => {
                    const team = teamsById.get(teamId);
                    return (
                      <div key={teamId} className="space-y-3">
                        <div className="flex items-center justify-between">
                          <h3 className="text-lg font-display text-ink-900">
                            {team?.name ?? "Team workflows"}
                          </h3>
                          <span className="text-xs uppercase tracking-[0.2em] text-ink-500">
                            {grouped.length} template{grouped.length === 1 ? "" : "s"}
                          </span>
                        </div>
                        <div className="space-y-4">
                          {grouped.map((workflow) => renderWorkflowCard(workflow))}
                        </div>
                      </div>
                    );
                  })}
                </div>
              </div>
            </div>
          </section>
        )}
      </main>
    </div>
  );
}
