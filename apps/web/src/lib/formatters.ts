const BYTE_UNITS = ["B", "KB", "MB", "GB", "TB"];

export const formatBytes = (value: number, decimals = 1) => {
  if (!Number.isFinite(value) || value <= 0) {
    return "0 B";
  }

  const unitIndex = Math.min(
    Math.floor(Math.log(value) / Math.log(1024)),
    BYTE_UNITS.length - 1,
  );
  const scaledValue = value / 1024 ** unitIndex;
  const precision = unitIndex <= 1 ? 0 : decimals;
  const roundedValue = scaledValue.toFixed(precision);

  return `${roundedValue} ${BYTE_UNITS[unitIndex]}`;
};

export const formatPercent = (value: number, total: number) => {
  if (total <= 0 || !Number.isFinite(value) || !Number.isFinite(total)) {
    return "0%";
  }
  const percent = Math.min(Math.max((value / total) * 100, 0), 100);
  return `${Math.round(percent)}%`;
};
