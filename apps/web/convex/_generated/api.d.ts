/* eslint-disable */
/**
 * Generated `api` utility.
 *
 * THIS CODE IS AUTOMATICALLY GENERATED.
 *
 * To regenerate, run `npx convex dev`.
 * @module
 */

import type * as capacity from "../capacity.js";
import type * as cleanup from "../cleanup.js";
import type * as crons from "../crons.js";
import type * as files from "../files.js";
import type * as jobs from "../jobs.js";
import type * as lib_auth from "../lib/auth.js";
import type * as lib_budget from "../lib/budget.js";
import type * as lib_email from "../lib/email.js";
import type * as lib_errors from "../lib/errors.js";
import type * as lib_job_state from "../lib/job_state.js";
import type * as lib_limit_checks from "../lib/limit_checks.js";
import type * as lib_limits from "../lib/limits.js";
import type * as lib_time from "../lib/time.js";
import type * as lib_usage from "../lib/usage.js";
import type * as lib_worker_auth from "../lib/worker_auth.js";
import type * as users from "../users.js";

import type {
  ApiFromModules,
  FilterApi,
  FunctionReference,
} from "convex/server";

declare const fullApi: ApiFromModules<{
  capacity: typeof capacity;
  cleanup: typeof cleanup;
  crons: typeof crons;
  files: typeof files;
  jobs: typeof jobs;
  "lib/auth": typeof lib_auth;
  "lib/budget": typeof lib_budget;
  "lib/email": typeof lib_email;
  "lib/errors": typeof lib_errors;
  "lib/job_state": typeof lib_job_state;
  "lib/limit_checks": typeof lib_limit_checks;
  "lib/limits": typeof lib_limits;
  "lib/time": typeof lib_time;
  "lib/usage": typeof lib_usage;
  "lib/worker_auth": typeof lib_worker_auth;
  users: typeof users;
}>;

/**
 * A utility for referencing Convex functions in your app's public API.
 *
 * Usage:
 * ```js
 * const myFunctionReference = api.myModule.myFunction;
 * ```
 */
export declare const api: FilterApi<
  typeof fullApi,
  FunctionReference<any, "public">
>;

/**
 * A utility for referencing Convex functions in your app's internal API.
 *
 * Usage:
 * ```js
 * const myFunctionReference = internal.myModule.myFunction;
 * ```
 */
export declare const internal: FilterApi<
  typeof fullApi,
  FunctionReference<any, "internal">
>;

export declare const components: {};
