#!/usr/bin/env node
// Commit-msg check (see tools/git/commit-message.mjs):
//   node tools/git/check-commit-msg.mjs <message file>
// Runs unconditionally on load, so the hook can never pass silently.
import fs from "node:fs";

import { commitMessageProblems, messageText } from "./commit-message.mjs";

const [file] = process.argv.slice(2);
if (file === undefined) {
  console.error("usage: node tools/git/check-commit-msg.mjs <message file>");
  process.exitCode = 2;
} else {
  const problems = commitMessageProblems(messageText(fs.readFileSync(file, "utf8")));
  if (problems.length > 0) {
    for (const problem of problems) console.error(`commit message: ${problem}`);
    console.error('\nSee CONTRIBUTING.md "Commit messages". Your message is in .git/COMMIT_EDITMSG.');
    process.exitCode = 1;
  }
}
