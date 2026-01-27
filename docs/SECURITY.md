# ZenPDF Security

## Scope
- Covers the web app, Convex backend, and worker.
- Focuses on data handling, access control, and operational safety.

## Principles
- Least privilege and default-deny for sensitive actions.
- Minimize data retention; expire artifacts via TTL.
- Validate inputs server-side for every tool and workflow.

## Identity and Access
- Clerk handles auth; only Google sign-in enabled.
- Convex functions enforce tier and team membership.
- Premium access allowlisted via environment variables.

## Secrets and Config
- Store secrets in Vercel, Convex, and Cloud Run secret managers.
- Never commit secrets; rotate immediately if exposed.
- Scope worker tokens to the required Convex functions.

## Data Handling
- Uploads use signed URLs; outputs stored in Convex file storage or R2.
- Artifacts expire via TTL cleanup jobs.
- Avoid logging PII or file contents; log identifiers only.

## Worker Hardening
- Run the container as non-root.
- Restrict outbound network access to required services.
- Keep LibreOffice and tool binaries pinned and updated.
- Isolate temp files and clean after each job.

## Dependency and Vulnerability Hygiene
- Use Dependabot or Renovate for dependency updates.
- Patch high-severity CVEs within 7 days.
- Keep base images updated.

## Incident Response
- Suspend heavy tools if budget or abuse spikes.
- Rotate secrets and invalidate tokens after breaches.
- Document postmortems in internal incident notes.
