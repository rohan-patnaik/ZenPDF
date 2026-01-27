import { query } from "./_generated/server";

import { resolveUser } from "./lib/auth";

export const getViewer = query({
  args: {},
  handler: async (ctx) => {
    const { identity, tier, adsFree } = await resolveUser(ctx);
    return {
      tier,
      adsFree,
      signedIn: Boolean(identity),
    };
  },
});
