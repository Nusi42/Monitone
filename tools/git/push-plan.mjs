// What the pre-push hook runs for a push: the decision in one function,
// pushPlan(), from the pushing checkout's role and the remote's URL
// (tools/git/pre-push.mjs runs it; docs/BUILDING.md "Master and work folders").
//
// - The master (local git config `monitone.role=master`) is the only checkout
//   that publishes: it pushes `main` and release tags `v<X.Y.Z>` to a network
//   remote and runs only checks that build nothing (no merge commits). `main`
//   only moves to commits that passed the full check in a work folder.
// - A work folder (its `origin` is the master) pushes only to a local path: no
//   merge commits, then the full check (tools/check.mjs).
// - A plain clone (no master, e.g. a contributor's fork) pushes anywhere: no
//   merge commits.
// - Every other combination is refused.
import { execFileSync } from "node:child_process";
import path from "node:path";
import { fileURLToPath } from "node:url";

/** The local git config key that marks the master checkout. */
export const ROLE_KEY = "monitone.role";

/** The remote the master publishes to. */
export const PUBLIC_REMOTE = "github";

/** The branch the master publishes. */
export const PUBLISH_BRANCH = "main";

/**
 * The command that publishes `main` from the master.
 *
 * @param {string} master the master checkout's path
 * @returns {string}
 */
export function publishCommand(master) {
  return `git -C "${master}" push ${PUBLIC_REMOTE} ${PUBLISH_BRANCH}`;
}

/** URLs that name a path on this machine; everything else is a network remote. */
const LOCAL_URL = [
  /^[A-Za-z]:[\\/]/, // C:\... or C:/...
  /^\\\\/, // \\host\share
  /^\//, // /...
  /^\.\.?(?:[\\/]|$)/, // ./x, ../x
  /^file:\/\//i,
];

/**
 * Whether a remote URL is a local path; `https://...`, `ssh://...`,
 * `git@host:...` and every other form are network remotes.
 *
 * @param {string} url
 * @returns {boolean}
 */
export function isLocalUrl(url) {
  return LOCAL_URL.some((re) => re.test(url.trim()));
}

/**
 * The directory a local remote URL names, resolved against `root`.
 *
 * @param {string} root the checkout the URL is configured in
 * @param {string} url a URL for which isLocalUrl() holds
 * @returns {string}
 */
export function localPath(root, url) {
  const trimmed = url.trim();
  return /^file:\/\//i.test(trimmed) ? fileURLToPath(trimmed) : path.resolve(root, trimmed);
}

/**
 * A key of the repository's own config at `dir`, or `undefined`.
 *
 * @param {string} dir
 * @param {string} key
 * @returns {string | undefined}
 */
export function localConfig(dir, key) {
  try {
    const value = execFileSync("git", ["config", "--local", "--get", key], {
      cwd: dir,
      encoding: "utf8",
      stdio: ["ignore", "pipe", "ignore"],
    }).trim();
    return value === "" ? undefined : value;
  } catch {
    return undefined;
  }
}

/**
 * Whether the checkout at `dir` is marked as the master.
 *
 * @param {string} dir
 * @returns {boolean}
 */
export function isMaster(dir) {
  return localConfig(dir, ROLE_KEY) === "master";
}

/**
 * The master a work folder belongs to (its local `origin`, marked as the
 * master), or `undefined`.
 *
 * @param {string} root
 * @returns {string | undefined}
 */
export function masterOf(root) {
  const origin = localConfig(root, "remote.origin.url");
  if (origin === undefined || !isLocalUrl(origin)) return undefined;
  const dir = localPath(root, origin);
  return isMaster(dir) ? dir : undefined;
}

/** @typedef {"master" | "work" | "plain"} CheckoutRole */

/**
 * The role of the checkout at `root`.
 *
 * @param {string} root
 * @returns {CheckoutRole}
 */
export function checkoutRole(root) {
  if (isMaster(root)) return "master";
  return masterOf(root) === undefined ? "plain" : "work";
}

/** @typedef {{ localRef: string, localSha: string, remoteRef: string, remoteSha: string }} PushRef */

/** Whether the master may push this ref: `main` to `main`, or a release tag to itself. */
function publishable(/** @type {PushRef} */ ref) {
  const main = `refs/heads/${PUBLISH_BRANCH}`;
  if (ref.localRef === main && ref.remoteRef === main) return true;
  return ref.localRef === ref.remoteRef && /^refs\/tags\/v\d+\.\d+\.\d+$/.test(ref.localRef);
}

/**
 * @typedef {object} PushStep
 * @property {string} id
 * @property {string} title
 * @property {string} script a script of the hook's own checkout, relative to its root
 * @property {boolean} stdin gets git's ref lines on stdin
 */

/** @type {PushStep} */
export const LINEAR = {
  id: "linear",
  title: "no merge commits in the pushed history",
  script: "tools/git/check-linear.mjs",
  stdin: true,
};

/** @type {PushStep} */
export const FULL_CHECK = {
  id: "check",
  title: "full check (tools/check.mjs)",
  script: "tools/check.mjs",
  stdin: false,
};

/**
 * What a push runs, or why it is refused.
 *
 * @param {{ role: CheckoutRole, url: string, refs: PushRef[], master?: string }} push
 *   `master`: the master's path, named in a work folder's refusal
 * @returns {{ steps: PushStep[] } | { refusal: string }}
 */
export function pushPlan({ role, url, refs, master = "<master>" }) {
  const local = isLocalUrl(url);
  if (role === "plain") return { steps: [LINEAR] };
  if (role === "work") {
    if (local) return { steps: [LINEAR, FULL_CHECK] };
    return {
      refusal:
        `a work folder pushes only to the master, not to ${url}.\n` +
        "Push the branch to origin, fast-forward the master, then publish from it:\n" +
        `  ${publishCommand(master)}`,
    };
  }
  if (local) {
    return {
      refusal: `the master pushes only to the public remote, not to ${url}; work folders fetch from it instead.`,
    };
  }
  const other = refs.filter((ref) => !publishable(ref));
  if (other.length > 0) {
    return {
      refusal:
        `the master publishes only ${PUBLISH_BRANCH} and release tags (v<X.Y.Z>), not ` +
        `${other.map((ref) => `${ref.localRef} -> ${ref.remoteRef}`).join(", ")}.`,
    };
  }
  return { steps: [LINEAR] };
}
