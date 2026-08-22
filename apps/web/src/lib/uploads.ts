export type UploadInput = {
  reservationId: string;
  storageId: string;
  filename: string;
  sizeBytes: number;
};

type BrowserUploadReservation = {
  reservationId: string;
  uploadUrl: string;
  expiresAt: number;
};

export const uploadFile = async (
  file: File,
  beginUpload: (intent: {
    filename: string;
    sizeBytes: number;
    contentType: string;
  }) => Promise<BrowserUploadReservation>,
  bindUpload: (binding: {
    reservationId: string;
    storageId: string;
  }) => Promise<unknown>,
) => {
  const contentType = file.type || "application/octet-stream";
  const reservation = await beginUpload({
    filename: file.name,
    sizeBytes: file.size,
    contentType,
  });
  const response = await fetch(reservation.uploadUrl, {
    method: "POST",
    headers: {
      "Content-Type": contentType,
    },
    body: file,
  });

  if (!response.ok) {
    throw new Error("Upload failed.");
  }

  const payload = (await response.json()) as { storageId: string };
  await bindUpload({
    reservationId: reservation.reservationId,
    storageId: payload.storageId,
  });

  return {
    reservationId: reservation.reservationId,
    storageId: payload.storageId,
    filename: file.name,
    sizeBytes: file.size,
  } satisfies UploadInput;
};
