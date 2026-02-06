import { query } from "./_generated/server";

import { resolveUser } from "./lib/auth";

export const getViewer = query({
  args: {},
  handler: async (ctx) => {
    const { identity, tier } = await resolveUser(ctx);
    return {
      tier,
      signedIn: Boolean(identity),
    };
  },
});
