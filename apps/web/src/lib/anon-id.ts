export const ANON_STORAGE_KEY = "zenpdf_anon_id";

export const getOrCreateAnonId = () => {
  if (typeof window === "undefined") {
    return null;
  }
  const createAnonId = () =>
    typeof crypto !== "undefined" && "randomUUID" in crypto
      ? crypto.randomUUID()
      : `anon-${Math.random().toString(36).slice(2, 10)}`;
  try {
    const existing = window.localStorage.getItem(ANON_STORAGE_KEY);
    if (existing) {
      return existing;
    }
    const fallback = createAnonId();
    window.localStorage.setItem(ANON_STORAGE_KEY, fallback);
    return fallback;
  } catch {
    return createAnonId();
  }
};
