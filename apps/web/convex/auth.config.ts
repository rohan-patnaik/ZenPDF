import type { AuthConfig } from "convex/server";

const domain = process.env.CLERK_JWT_ISSUER_DOMAIN;
const applicationID = process.env.CLERK_JWT_AUDIENCE ?? "convex";

if (!domain) {
  const message =
    "CLERK_JWT_ISSUER_DOMAIN is not set; AuthConfig providers will be empty.";
  if (process.env.NODE_ENV === "production") {
    throw new Error(message);
  }
  console.warn(message);
}

const config: AuthConfig = domain
  ? {
      providers: [
        {
          domain,
          applicationID,
        },
      ],
    }
  : {
      providers: [],
    };

export default config;
