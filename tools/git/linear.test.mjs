// Tests for the linear-history pre-push check: node --test tools/git/
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import { parsePushLines, pushedRange } from "./linear.mjs";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const ZERO = "0".repeat(40);
const A = "a".repeat(40);
const B = "b".repeat(40);

test("parsePushLines reads git's ref lines and skips blank ones", () => {
  const text = `refs/heads/main ${A} refs/heads/main ${B}\r\n\nrefs/tags/v1.0.0 ${B} refs/tags/v1.0.0 ${ZERO}\n`;
  assert.deepEqual(parsePushLines(text), [
    { localRef: "refs/heads/main", localSha: A, remoteRef: "refs/heads/main", remoteSha: B },
    { localRef: "refs/tags/v1.0.0", localSha: B, remoteRef: "refs/tags/v1.0.0", remoteSha: ZERO },
  ]);
});

test("pushedRange: deletion, new ref, known and unknown remote value", () => {
  const known = () => true;
  const unknown = () => false;
  assert.equal(pushedRange({ localSha: ZERO, remoteSha: A }, known), undefined);
  assert.deepEqual(pushedRange({ localSha: A, remoteSha: ZERO }, known), [A, "--not", "--remotes"]);
  assert.deepEqual(pushedRange({ localSha: A, remoteSha: B }, known), [A, "--not", "--remotes", B]);
  assert.deepEqual(pushedRange({ localSha: A, remoteSha: B }, unknown), [A, "--not", "--remotes"]);
});

/** Runs git in `cwd`; returns trimmed stdout, throws on failure. */
function git(cwd, ...args) {
  return execFileSync("git", args, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
}

/** Runs git in `cwd` without throwing. */
function gitTry(cwd, ...args) {
  return spawnSync("git", args, { cwd, encoding: "utf8" });
}

/** A bare remote plus a clone that uses this repository's real hook and check files. */
function makeRepos() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "monitone-linear-"));
  const remote = path.join(dir, "remote.git");
  const work = path.join(dir, "work");
  git(dir, "init", "--bare", "--quiet", "--initial-branch=main", remote);
  git(dir, "init", "--quiet", "--initial-branch=main", work);
  for (const rel of [".githooks/pre-push", "tools/git/linear.mjs", "tools/git/check-linear.mjs"]) {
    fs.mkdirSync(path.join(work, path.dirname(rel)), { recursive: true });
    fs.copyFileSync(path.join(repoRoot, rel), path.join(work, rel));
  }
  git(work, "config", "user.name", "Test");
  git(work, "config", "user.email", "test@example.com");
  git(work, "config", "core.hooksPath", ".githooks");
  git(work, "remote", "add", "origin", remote);
  commit(work, "base");
  return { dir, remote, work };
}

/** Adds a file and commits it. */
function commit(cwd, name) {
  fs.writeFileSync(path.join(cwd, `${name}.txt`), name);
  git(cwd, "add", "-A");
  git(cwd, "commit", "--quiet", "-m", `chore: ${name}`);
}

/** Makes a merge commit on the current branch from a side branch. */
function mergeSide(cwd, name) {
  git(cwd, "switch", "--quiet", "-c", name);
  commit(cwd, `${name}-side`);
  git(cwd, "switch", "--quiet", "main");
  commit(cwd, `${name}-main`);
  git(cwd, "merge", "--no-ff", "--quiet", "-m", `Merge ${name}`, name);
}

test("a push of linear commits passes the real hook", (t) => {
  const { dir, work } = makeRepos();
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const first = gitTry(work, "push", "--quiet", "origin", "main");
  assert.equal(first.status, 0, first.stderr);
  commit(work, "next");
  const second = gitTry(work, "push", "origin", "main");
  assert.equal(second.status, 0, second.stderr);
  assert.match(second.stdout + second.stderr, /linear: no merge commits to push/);
});

test("a push that adds a merge commit is refused", (t) => {
  const { dir, work } = makeRepos();
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  assert.equal(gitTry(work, "push", "--quiet", "origin", "main").status, 0);
  mergeSide(work, "feature");
  const push = gitTry(work, "push", "origin", "main");
  assert.notEqual(push.status, 0);
  assert.match(push.stderr, /merge commit [0-9a-f]+ Merge feature/);
  assert.match(push.stderr, /rebasing onto the target branch/);
});

test("a merge commit already on the remote does not block later pushes", (t) => {
  const { dir, work } = makeRepos();
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  mergeSide(work, "old");
  const seed = gitTry(work, "push", "--no-verify", "--quiet", "origin", "main");
  assert.equal(seed.status, 0, seed.stderr);
  commit(work, "after");
  const push = gitTry(work, "push", "origin", "main");
  assert.equal(push.status, 0, push.stderr);
});

test("deleting a remote branch passes", (t) => {
  const { dir, work } = makeRepos();
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  assert.equal(gitTry(work, "push", "--quiet", "origin", "main", "main:topic").status, 0);
  const push = gitTry(work, "push", "origin", ":topic");
  assert.equal(push.status, 0, push.stderr);
});

test("git hook run feeds the hook and fails on a merge", (t) => {
  const { dir, remote, work } = makeRepos();
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  mergeSide(work, "topic");
  const head = git(work, "rev-parse", "HEAD");
  const stdin = path.join(dir, "push-lines.txt");
  fs.writeFileSync(stdin, `refs/heads/main ${head} refs/heads/main ${ZERO}\n`);
  const run = gitTry(work, "hook", "run", `--to-stdin=${stdin}`, "pre-push", "--", "origin", remote);
  assert.notEqual(run.status, 0);
  assert.match(run.stderr, /Merge topic/);
});
