#!/usr/bin/env node
// The private-data guard (tools/git/private-patterns.mjs) and the commit
// identity check (tools/git/identity.mjs). The guard fails when a private
// pattern matches, case-insensitively, or the base64 form of a literal
// pattern matches; without a patterns file it passes ("no private patterns
// configured"), so other machines work. Findings name the place and line; the
// matched text is masked.
//
//   node tools/git/check-private.mjs              every tracked file (content + name)
//   node tools/git/check-private.mjs --staged     staged content + names (pre-commit hook)
//   node tools/git/check-private.mjs --message F  a commit message file + the author and
//                                                committer about to be recorded (commit-msg hook)
//   node tools/git/check-private.mjs --push       the pushed commits: identities, messages,
//                                                added lines (pre-push; git's ref lines on stdin)
//
// Runs unconditionally on load, so a hook can never pass silently.
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

import { messageText } from "./commit-message.mjs";
import { IDENTITY_KEY, identityProblem } from "./identity.mjs";
import { hasCommit, parsePushLines, pushedRange } from "./linear.mjs";
import {
  PATTERNS_ENV,
  PATTERNS_KEY,
  PUSH_LOG_FORMAT,
  describe,
  loadMatchers,
  maskAll,
  pushSections,
  scanText,
} from "./private-patterns.mjs";

// The repository the hook or check runs in (its working directory).
const root = execFileSync("git", ["rev-parse", "--show-toplevel"], { encoding: "utf8" }).trim();

/** Runs git in the checked repository; returns stdout as a string. */
const git = (/** @type {string[]} */ args, input = "") =>
  execFileSync("git", args, { cwd: root, input, maxBuffer: 1 << 30 }).toString("utf8");

/** A git config value (any scope), or `undefined`. */
function config(/** @type {string} */ key) {
  try {
    return git(["config", "--get", key]).trim() || undefined;
  } catch {
    return undefined;
  }
}

const configured = process.env[PATTERNS_ENV] ?? config(PATTERNS_KEY);
const matchers = configured === undefined ? undefined : loadMatchers(path.resolve(root, configured));
const identity = config(IDENTITY_KEY);

/** @type {string[]} */
const problems = [];
const check = (/** @type {string} */ where, /** @type {string} */ text) => {
  if (matchers === undefined) return;
  for (const finding of scanText(text, matchers)) problems.push(describe(maskAll(where, matchers), finding));
};
const checkName = (/** @type {string} */ name) => check(`${name} (file name)`, name);
const checkIdentity = (/** @type {string} */ where, /** @type {string} */ actual) => {
  if (identity === undefined) return;
  const problem = identityProblem(where, actual, identity);
  if (problem !== undefined) problems.push(matchers === undefined ? problem : maskAll(problem, matchers));
};

const [mode = "--tree", modeArg] = process.argv.slice(2);
let scanned = "";
switch (mode) {
  case "--tree": {
    const files = git(["ls-files", "-z"]).split("\0").filter(Boolean);
    for (const name of files) {
      checkName(name);
      let content = "";
      try {
        content = fs.readFileSync(path.join(root, name), "utf8");
      } catch {
        continue; // deleted in the working tree, or a directory (submodule)
      }
      check(name, content);
    }
    scanned = `${String(files.length)} tracked files`;
    break;
  }
  case "--staged": {
    const files = git(["diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR"]).split("\0").filter(Boolean);
    for (const name of files) {
      checkName(name);
      check(name, git(["show", `:${name}`]));
    }
    scanned = `${String(files.length)} staged files`;
    break;
  }
  case "--message": {
    if (modeArg === undefined) throw new Error("--message needs the message file");
    check("commit message", messageText(fs.readFileSync(modeArg, "utf8")));
    // The identity git records with this commit (user.name / user.email or
    // the GIT_AUTHOR_* / GIT_COMMITTER_* overrides).
    for (const [who, variable] of [
      ["author", "GIT_AUTHOR_IDENT"],
      ["committer", "GIT_COMMITTER_IDENT"],
    ]) {
      const ident = git(["var", variable]).replace(/>.*$/s, ">");
      check(`commit ${who}`, ident);
      checkIdentity(`commit ${who}`, ident);
    }
    scanned = "the commit message and identity";
    break;
  }
  case "--push": {
    let commits = 0;
    for (const ref of parsePushLines(fs.readFileSync(0, "utf8"))) {
      const range = pushedRange(ref, (sha) => hasCommit(root, sha));
      if (range === undefined) continue;
      check(`${ref.localRef} ${ref.remoteRef} (ref)`, `${ref.localRef} ${ref.remoteRef}`);
      const log = git(["log", "--no-color", "--no-ext-diff", "-p", PUSH_LOG_FORMAT, ...range]);
      for (const section of pushSections(log)) {
        check(section.where, section.text);
        if (/ (author|committer)$/.test(section.where)) checkIdentity(section.where, section.text);
      }
      commits += log.split("\0commit ").length - 1;
    }
    scanned = `${String(commits)} commits to push`;
    break;
  }
  default:
    console.error(`check-private: unknown mode ${mode}`);
    process.exit(2);
}

if (problems.length > 0) {
  for (const problem of problems) console.error(problem);
  console.error(
    `\nprivate: ${String(problems.length)} problem(s) in ${scanned}. Replace private data with neutral ` +
      `example data; commit with the identity in ${IDENTITY_KEY} (git config user.name / user.email). ` +
      "Never use --no-verify, never copy the patterns file into the repository.",
  );
  process.exitCode = 1;
} else {
  const what = [
    matchers === undefined ? "no private patterns configured" : `${String(matchers.length)} matchers`,
    identity === undefined ? "no identity configured" : "identity checked",
  ];
  console.log(`private: ${scanned} clean (${what.join(", ")})`);
}
