#!/usr/bin/env node
// Pre-push check: no merge commits in what a push adds (see tools/git/linear.mjs).
// Reads git's ref lines on stdin. Runs unconditionally on load, so the hook
// can never pass silently.
import { execFileSync } from "node:child_process";
import fs from "node:fs";

import { pushedMerges } from "./linear.mjs";

const root = execFileSync("git", ["rev-parse", "--show-toplevel"], { encoding: "utf8" }).trim();
const merges = pushedMerges(root, fs.readFileSync(0, "utf8"));
if (merges.length === 0) {
  console.log("linear: no merge commits to push");
} else {
  for (const merge of merges) console.error(`merge commit ${merge}`);
  console.error(
    `\nlinear: ${String(merges.length)} merge commit(s) in the push. Combine branches by ` +
      "rebasing onto the target branch (`git rebase <target>`, then `git merge --ff-only`), " +
      "never with a merge commit.",
  );
  process.exitCode = 1;
}
