// Tests for the private-data guard and the identity check
// (tools/git/check-private.mjs): node --test "tools/git/*.test.mjs".
// Synthetic patterns only: never put the owner's real patterns (or anything
// derived from them) into a test. The hook tests run the real hooks through git.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";

import { identityOf, identityProblem } from "./identity.mjs";
import { PATTERNS_ENV, base64Forms, literalOf, mask, matchersFrom, scanText } from "./private-patterns.mjs";
import { git, initRepo, tempDir } from "./test-repos.mjs";

const PATTERNS = ["# synthetic patterns", "jo@corp\\.example\\.net", "", "\\bjoanne\\b", "inv[0-9]+x"].join("\n");
const OWNER = "Owner <owner@example.com>";

test("literal patterns are recognised; classes and operators are not", () => {
  assert.equal(literalOf("jo@corp\\.example\\.net"), "jo@corp.example.net");
  assert.equal(literalOf("\\bjoanne\\b"), "joanne");
  assert.equal(literalOf("inv[0-9]+x"), undefined);
  assert.equal(literalOf("a|b"), undefined);
});

test("base64 forms are found at every byte alignment", () => {
  const forms = base64Forms("joanne");
  for (const prefix of ["", "a", "ab", "user="]) {
    const encoded = Buffer.from(`${prefix}joanne!`).toString("base64");
    assert.ok(
      forms.some((form) => encoded.includes(form)),
      prefix,
    );
  }
});

test("scanText finds patterns case-insensitively and masks the match", () => {
  const findings = scanText("ok\nwrite to JO@corp.example.net, inv42x\n", matchersFrom(PATTERNS));
  assert.deepEqual(
    findings.map((f) => [f.line, f.column, f.masked]),
    [
      [2, 10, "J******************"],
      [2, 31, "i*****"],
    ],
  );
  assert.equal(mask("joanne"), "j*****");
});

test("identityProblem accepts only the configured identity", () => {
  assert.equal(identityOf(`${OWNER} 1700000000 +0100`), OWNER);
  assert.equal(identityProblem("author", `${OWNER} 1700000000 +0100`, OWNER), undefined);
  assert.match(identityProblem("author", "owner <owner@example.com>", OWNER) ?? "", /only "Owner <owner@example\.com>"/);
});

/** A repository with the real hooks, the synthetic patterns file and its environment. */
function makeRepo(t) {
  const dir = tempDir(t);
  const repo = path.join(dir, "repo");
  initRepo(repo);
  const patterns = path.join(dir, "patterns.txt");
  fs.writeFileSync(patterns, PATTERNS);
  return { dir, repo, env: { ...process.env, [PATTERNS_ENV]: patterns } };
}

/** Writes and stages `name`, then commits it; does not throw. */
function commitFile(repo, name, content, message, env) {
  fs.writeFileSync(path.join(repo, name), content);
  git(repo, "add", name);
  return spawnSync("git", ["commit", "-m", message], { cwd: repo, encoding: "utf8", env });
}

test("the pre-commit hook refuses private data in a staged file and masks it", (t) => {
  const { repo, env } = makeRepo(t);
  const result = commitFile(repo, "notes.txt", "ask joanne\n", "docs: notes", env);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /notes\.txt:1:5: private pattern 4 matched "j\*\*\*\*\*"/);
  assert.doesNotMatch(result.stderr, /joanne/);
});

test("the commit-msg hook refuses private data in the message", (t) => {
  const { repo, env } = makeRepo(t);
  const result = commitFile(repo, "notes.txt", "fine\n", "docs: thanks to joanne", env);
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /commit message:1:17: private pattern 4/);
});

test("without a patterns file the guard passes with a notice", (t) => {
  const { repo } = makeRepo(t);
  const env = { ...process.env };
  delete env[PATTERNS_ENV];
  const result = commitFile(repo, "notes.txt", "ask joanne\n", "docs: notes", env);
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout + result.stderr, /no private patterns configured/);
});

test("the commit-msg hook allows only the identity in monitone.identity", (t) => {
  const { repo, env } = makeRepo(t);
  git(repo, "config", "monitone.identity", OWNER);
  const wrong = commitFile(repo, "a.txt", "a\n", "docs: a", env);
  assert.notEqual(wrong.status, 0);
  assert.match(wrong.stderr, /commit author: the identity is "Test <test@example\.com>"/);
  assert.match(wrong.stderr, /commit committer: the identity is/);
  git(repo, "config", "user.name", "Owner");
  git(repo, "config", "user.email", "owner@example.com");
  const right = spawnSync("git", ["commit", "-m", "docs: a"], { cwd: repo, encoding: "utf8", env });
  assert.equal(right.status, 0, right.stderr);
});

/** A bare remote for `repo`, with `main` pushed to it before the test's commits. */
function addRemote(dir, repo) {
  const remote = path.join(dir, "remote.git");
  git(dir, "init", "--bare", "--quiet", "--initial-branch=main", remote);
  git(repo, "remote", "add", "origin", remote);
  git(repo, "push", "--quiet", "origin", "main");
}

test("the pre-push hook refuses private data added by a pushed commit", (t) => {
  const { dir, repo, env } = makeRepo(t);
  addRemote(dir, repo);
  // Committed while the guard had no patterns; the push still catches it.
  const noPatterns = { ...process.env };
  delete noPatterns[PATTERNS_ENV];
  assert.equal(commitFile(repo, "notes.txt", "ask joanne\n", "docs: notes", noPatterns).status, 0);
  const push = spawnSync("git", ["push", "origin", "main"], { cwd: repo, encoding: "utf8", env });
  assert.notEqual(push.status, 0);
  assert.match(push.stderr, /notes\.txt \(added lines; line = line of the diff\):\d+:5: private pattern 4/);
});

test("the pre-push hook refuses pushed commits with another identity", (t) => {
  const { dir, repo, env } = makeRepo(t);
  addRemote(dir, repo);
  assert.equal(commitFile(repo, "a.txt", "a\n", "docs: a", env).status, 0);
  git(repo, "config", "monitone.identity", OWNER);
  const push = spawnSync("git", ["push", "origin", "main"], { cwd: repo, encoding: "utf8", env });
  assert.notEqual(push.status, 0);
  assert.match(push.stderr, /commit [0-9a-f]{12} author: the identity is "Test <test@example\.com>"/);
});

test("the tree check finds private data in tracked files", (t) => {
  const { repo, env } = makeRepo(t);
  fs.writeFileSync(path.join(repo, "joanne.txt"), "x\n");
  git(repo, "add", "joanne.txt");
  git(repo, "-c", "core.hooksPath=", "commit", "--quiet", "-m", "docs: x");
  const result = spawnSync(process.execPath, [path.join(repo, "tools", "git", "check-private.mjs")], {
    cwd: repo,
    encoding: "utf8",
    env,
  });
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /j\*\*\*\*\*\.txt \(file name\)/);
});
