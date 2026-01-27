export type UploadInput = {
  storageId: string;
  filename: string;
  sizeBytes: number;
};

export const uploadFile = async (
  file: File,
  generateUploadUrl: () => Promise<string>,
) => {
  const uploadUrl = await generateUploadUrl();
  const response = await fetch(uploadUrl, {
    method: "POST",
    headers: {
      "Content-Type": file.type || "application/octet-stream",
    },
    body: file,
  });

  if (!response.ok) {
    throw new Error("Upload failed.");
  }

  const payload = (await response.json()) as { storageId: string };

  return {
    storageId: payload.storageId,
    filename: file.name,
    sizeBytes: file.size,
  } satisfies UploadInput;
};
