// Linear history: branches are combined by rebasing onto the target and
// fast-forwarding, never with a merge commit. The pre-push hook
// (.githooks/pre-push -> tools/git/check-linear.mjs) refuses merge commits in
// what a push adds. Merge commits already on the remote stay as they are.
import { execFileSync } from "node:child_process";

/** The object id git's pre-push hook reports for "no object" (a deletion, a new ref). */
const NO_OBJECT = /^0+$/;

/**
 * Parses the lines git passes to a pre-push hook on stdin:
 * `<local ref> <local sha> <remote ref> <remote sha>`.
 *
 * @param {string} text
 * @returns {{ localRef: string, localSha: string, remoteRef: string, remoteSha: string }[]}
 */
export function parsePushLines(text) {
  return text
    .split(/\r?\n/)
    .map((line) => line.trim().split(/\s+/))
    .filter((parts) => parts.length >= 4)
    .map(([localRef = "", localSha = "", remoteRef = "", remoteSha = ""]) => ({
      localRef,
      localSha,
      remoteRef,
      remoteSha,
    }));
}

/**
 * The revision arguments that select the commits a ref line adds: those on no
 * remote-tracking branch and not under the remote's old value. `undefined`
 * for a deletion.
 *
 * @param {{ localSha: string, remoteSha: string }} ref
 * @param {(sha: string) => boolean} known whether the local repository has the commit
 * @returns {string[] | undefined}
 */
export function pushedRange(ref, known) {
  if (NO_OBJECT.test(ref.localSha)) return undefined;
  const range = [ref.localSha, "--not", "--remotes"];
  if (!NO_OBJECT.test(ref.remoteSha) && known(ref.remoteSha)) range.push(ref.remoteSha);
  return range;
}

/**
 * Whether the repository at `root` has the commit `sha`.
 *
 * @param {string} root
 * @param {string} sha
 * @returns {boolean}
 */
export function hasCommit(root, sha) {
  try {
    execFileSync("git", ["cat-file", "-e", `${sha}^{commit}`], { cwd: root, stdio: "ignore" });
    return true;
  } catch {
    return false;
  }
}

/**
 * The merge commits (`<short sha> <subject>`) a push of `pushLines` would add.
 *
 * @param {string} root the pushing checkout
 * @param {string} pushLines git's pre-push stdin
 * @returns {string[]}
 */
export function pushedMerges(root, pushLines) {
  /** @type {string[]} */
  const merges = [];
  for (const ref of parsePushLines(pushLines)) {
    const range = pushedRange(ref, (sha) => hasCommit(root, sha));
    if (range === undefined) continue;
    const out = execFileSync("git", ["log", "--merges", "--format=%h %s", ...range], {
      cwd: root,
      encoding: "utf8",
    });
    merges.push(...out.split("\n").filter(Boolean));
  }
  return merges;
}
