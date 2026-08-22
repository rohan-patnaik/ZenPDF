import type { Doc, Id } from "../_generated/dataModel";

import { throwFriendlyError } from "./errors";

export const BROWSER_UPLOAD_RESERVATION_TTL_MS = 15 * 60 * 1000;
export const MAX_OUTSTANDING_BROWSER_UPLOADS = 12;

const ANON_ID_PATTERN = /^[A-Za-z0-9_-]{8,128}$/;
const CONTENT_TYPE_PATTERN = /^[A-Za-z0-9!#$&^_.+-]+\/[A-Za-z0-9!#$&^_.+-]+$/;

export const normalizeAnonId = (anonId: string | undefined) => {
  const normalized = anonId?.trim();
  return normalized && ANON_ID_PATTERN.test(normalized) ? normalized : undefined;
};

export const normalizeUploadIntent = (
  filename: string,
  sizeBytes: number,
  contentType: string,
  maxBytes: number,
) => {
  const normalizedFilename = filename.trim();
  const normalizedContentType = contentType.trim().toLowerCase();
  if (
    !normalizedFilename ||
    normalizedFilename.length > 255 ||
    normalizedFilename === "." ||
    normalizedFilename === ".." ||
    /[\\/\0]/.test(normalizedFilename) ||
    !Number.isSafeInteger(sizeBytes) ||
    sizeBytes <= 0 ||
    !Number.isSafeInteger(maxBytes) ||
    sizeBytes > maxBytes ||
    normalizedContentType.length > 128 ||
    !CONTENT_TYPE_PATTERN.test(normalizedContentType)
  ) {
    if (Number.isSafeInteger(sizeBytes) && sizeBytes > maxBytes) {
      throwFriendlyError("USER_LIMIT_FILE_TOO_LARGE", {
        limitMb: Math.floor(maxBytes / (1024 * 1024)),
      });
    }
    throwFriendlyError("USER_INPUT_INVALID");
  }
  return {
    filename: normalizedFilename,
    sizeBytes,
    contentType: normalizedContentType,
  };
};

export const reservationBelongsTo = (
  reservation: Doc<"browserUploadReservations">,
  userId: Id<"users"> | undefined,
  anonId: string | undefined,
) =>
  userId
    ? reservation.userId === userId && reservation.anonId === undefined
    : reservation.userId === undefined && reservation.anonId === anonId;
