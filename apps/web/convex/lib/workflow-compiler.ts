import { throwFriendlyError } from "./errors";
import {
  MAX_WORKFLOW_STEPS,
  WORKFLOW_TOOL_SPECS,
  type WorkflowAssetKind,
  type WorkflowStep,
  type WorkflowToolSpec,
} from "./workflow-spec";

export { MAX_WORKFLOW_STEPS, WORKFLOW_TOOL_SPECS };
export type { WorkflowAssetKind, WorkflowStep, WorkflowToolSpec };

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
    if (!step || typeof step !== "object" || Array.isArray(step)) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "invalid_step_shape", index });
    }

    const toolName = (step as { tool?: unknown }).tool;
    if (typeof toolName !== "string" || toolName.trim().length === 0) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "invalid_step_shape", index });
    }

    const configValue = (step as { config?: unknown }).config;
    if (configValue !== undefined && !isRecord(configValue)) {
      throwFriendlyError("USER_INPUT_INVALID", { reason: "invalid_step_shape", index });
    }

    const spec = WORKFLOW_TOOL_SPECS[toolName];
    if (!spec) {
      throwFriendlyError("USER_INPUT_INVALID", {
        reason: "unknown_tool",
        tool: toolName,
      });
    }

    const config = isRecord(configValue) ? configValue : undefined;

    if (spec.requiredConfig && spec.requiredConfig.length > 0) {
      const missing = spec.requiredConfig.filter((key) =>
        isMissingConfigValue(config?.[key]),
      );
      if (missing.length > 0) {
        throwFriendlyError("USER_INPUT_INVALID", {
          reason: "missing_config",
          tool: toolName,
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
          tool: toolName,
        });
      }
      if (outputKind && spec.input !== outputKind) {
        throwFriendlyError("USER_INPUT_INVALID", {
          reason: "incompatible_chain",
          tool: toolName,
          expected: outputKind,
          received: spec.input,
        });
      }
    }

    if (index < steps.length - 1 && spec.output !== "pdf") {
      throwFriendlyError("USER_INPUT_INVALID", {
        reason: "non_pdf_mid_chain",
        tool: toolName,
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
