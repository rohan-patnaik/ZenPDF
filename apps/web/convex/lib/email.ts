export const normalizeEmail = (value: string) => value.trim().toLowerCase();

export const normalizeOptionalEmail = (value?: string | null) =>
  value ? normalizeEmail(value) : undefined;
