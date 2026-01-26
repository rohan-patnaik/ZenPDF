export type CapacityState = "available" | "limited" | "at_capacity";

export type CapacitySignals = {
  monthlyBudgetUsage: number;
  heavyToolsEnabled: boolean;
};

export const resolveCapacityState = ({
  monthlyBudgetUsage,
  heavyToolsEnabled,
}: CapacitySignals): CapacityState => {
  if (monthlyBudgetUsage >= 1) {
    return "at_capacity";
  }

  if (monthlyBudgetUsage >= 0.8 || !heavyToolsEnabled) {
    return "limited";
  }

  return "available";
};
