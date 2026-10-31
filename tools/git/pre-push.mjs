#!/usr/bin/env node
// The pre-push hook (.githooks/pre-push): decides from the checkout's role and
// the remote's URL what the push runs (tools/git/push-plan.mjs) and runs it in
// the pushing checkout. A refusal names the command to use instead. git passes
// the remote's name and URL as arguments and the ref lines on stdin.
//
// Runs unconditionally on load, so the hook can never pass silently. Never
// bypass it with `git push --no-verify`: fix the finding instead.
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { parsePushLines } from "./linear.mjs";
import { checkoutRole, masterOf, pushPlan } from "./push-plan.mjs";

/** The checkout these scripts belong to (the hook's own). */
const scriptsRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/**
 * @param {string[]} argv `<remote name> <remote URL>`
 * @returns {number} the exit code
 */
function main(argv) {
  const [remote = "", url = remote] = argv;
  const root = execFileSync("git", ["rev-parse", "--show-toplevel"], { encoding: "utf8" }).trim();
  const input = fs.readFileSync(0, "utf8");
  const plan = pushPlan({ role: checkoutRole(root), url, refs: parsePushLines(input), master: masterOf(root) });
  if ("refusal" in plan) {
    console.error(`pre-push: refused: ${plan.refusal}`);
    return 1;
  }
  for (const step of plan.steps) {
    console.log(`pre-push: ${step.title}`);
    const result = spawnSync(process.execPath, [path.join(scriptsRoot, ...step.script.split("/"))], {
      cwd: root,
      input: step.stdin ? input : "",
      stdio: ["pipe", "inherit", "inherit"],
    });
    if (result.status !== 0) {
      console.error(`pre-push: ${step.id} failed. Fix the findings; never push with --no-verify.`);
      return 1;
    }
  }
  return 0;
}

try {
  process.exitCode = main(process.argv.slice(2));
} catch (error) {
  console.error(`pre-push: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
}
