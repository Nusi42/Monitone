// Tests for the linear-history pre-push check: node --test "tools/git/*.test.mjs"
import assert from "node:assert/strict";
import path from "node:path";
import { test } from "node:test";

import { parsePushLines, pushedRange } from "./linear.mjs";
import { commit, git, gitTry, initRepo, mergeSide, runPrePush, tempDir } from "./test-repos.mjs";

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

/** A bare remote plus a plain clone (no master) that uses this repository's real hooks. */
function makeRepos(t) {
  const dir = tempDir(t);
  const remote = path.join(dir, "remote.git");
  const work = path.join(dir, "work");
  git(dir, "init", "--bare", "--quiet", "--initial-branch=main", remote);
  initRepo(work);
  git(work, "remote", "add", "origin", remote);
  return { dir, remote, work };
}

test("a push of linear commits passes the real hook", (t) => {
  const { work } = makeRepos(t);
  const first = gitTry(work, "push", "--quiet", "origin", "main");
  assert.equal(first.status, 0, first.stderr);
  commit(work, "next");
  const second = gitTry(work, "push", "origin", "main");
  assert.equal(second.status, 0, second.stderr);
  assert.match(second.stdout + second.stderr, /linear: no merge commits to push/);
});

test("a push that adds a merge commit is refused", (t) => {
  const { work } = makeRepos(t);
  assert.equal(gitTry(work, "push", "--quiet", "origin", "main").status, 0);
  mergeSide(work, "feature");
  const push = gitTry(work, "push", "origin", "main");
  assert.notEqual(push.status, 0);
  assert.match(push.stderr, /merge commit [0-9a-f]+ chore: merge feature/);
  assert.match(push.stderr, /rebasing onto the target branch/);
});

test("a merge commit already on the remote does not block later pushes", (t) => {
  const { work } = makeRepos(t);
  mergeSide(work, "old");
  const seed = gitTry(work, "push", "--no-verify", "--quiet", "origin", "main");
  assert.equal(seed.status, 0, seed.stderr);
  commit(work, "after");
  const push = gitTry(work, "push", "origin", "main");
  assert.equal(push.status, 0, push.stderr);
});

test("deleting a remote branch passes", (t) => {
  const { work } = makeRepos(t);
  assert.equal(gitTry(work, "push", "--quiet", "origin", "main", "main:topic").status, 0);
  const push = gitTry(work, "push", "origin", ":topic");
  assert.equal(push.status, 0, push.stderr);
});

test("git hook run feeds the hook and fails on a merge", (t) => {
  const { remote, work } = makeRepos(t);
  mergeSide(work, "topic");
  const head = git(work, "rev-parse", "HEAD");
  const run = runPrePush(work, "origin", remote, `refs/heads/main ${head} refs/heads/main ${ZERO}
`);
  assert.notEqual(run.status, 0);
  assert.match(run.stderr, /chore: merge topic/);
});
