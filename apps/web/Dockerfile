FROM node:20.19.4-bookworm-slim@sha256:6db5e436948af8f0244488a1f658c2c8e55a3ae51ca2e1686ed042be8f25f70a

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci

ARG NEXT_PUBLIC_CONVEX_URL
ARG NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY
ENV NEXT_PUBLIC_CONVEX_URL=${NEXT_PUBLIC_CONVEX_URL}
ENV NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY=${NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY}

COPY . ./

ENV NODE_ENV=production
RUN test -n "$NEXT_PUBLIC_CONVEX_URL" \
    || (echo "NEXT_PUBLIC_CONVEX_URL build argument is required" >&2; exit 1) \
    && test -n "$NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY" \
    || (echo "NEXT_PUBLIC_CLERK_PUBLISHABLE_KEY build argument is required" >&2; exit 1) \
    && npm run build

EXPOSE 3000

CMD ["npm", "run", "start"]
