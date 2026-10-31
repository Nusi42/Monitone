// Tests for the push decision (tools/git/push-plan.mjs) and the master and
// work-folder setup (tools/git/work-folders.mjs): node --test "tools/git/*.test.mjs".
// The hook tests run the real pre-push hook through git in throw-away repositories.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import path from "node:path";
import { test } from "node:test";

import { FULL_CHECK, LINEAR, checkoutRole, isLocalUrl, pushPlan } from "./push-plan.mjs";
import { CHECK_STUB_OUTPUT, commit, git, gitTry, initRepo, mergeSide, repoRoot, runPrePush, tempDir } from "./test-repos.mjs";

const ZERO = "0".repeat(40);
const A = "a".repeat(40);
const NETWORK = "https://github.com/example/Monitone.git";
const LOCAL = "C:/projects/Monitone";

/** A ref line for pushPlan(). */
const ref = (localRef, remoteRef = localRef, localSha = A) => ({ localRef, localSha, remoteRef, remoteSha: ZERO });
const MAIN = ref("refs/heads/main");

test("isLocalUrl tells paths from network remotes", () => {
  for (const url of ["C:\\x", "C:/x", "\\\\host\\share", "/srv/x.git", "../x", "./x", "file:///c/x"]) {
    assert.equal(isLocalUrl(url), true, url);
  }
  for (const url of [NETWORK, "ssh://git@github.com/x", "git@github.com:x/y.git", "origin"]) {
    assert.equal(isLocalUrl(url), false, url);
  }
});

test("pushPlan: plain clone checks linear history for every remote", () => {
  assert.deepEqual(pushPlan({ role: "plain", url: NETWORK, refs: [ref("refs/heads/x")] }), { steps: [LINEAR] });
  assert.deepEqual(pushPlan({ role: "plain", url: LOCAL, refs: [MAIN] }), { steps: [LINEAR] });
});

test("pushPlan: a work folder pushes only to a local path, with the full check", () => {
  assert.deepEqual(pushPlan({ role: "work", url: LOCAL, refs: [ref("refs/heads/WIN1")] }), {
    steps: [LINEAR, FULL_CHECK],
  });
  const refused = pushPlan({ role: "work", url: NETWORK, refs: [MAIN], master: "M:/master" });
  assert.ok("refusal" in refused);
  assert.match(refused.refusal, /pushes only to the master/);
  assert.match(refused.refusal, /git -C "M:\/master" push github main/);
});

test("pushPlan: the master publishes only main and release tags to a network remote", () => {
  assert.deepEqual(pushPlan({ role: "master", url: NETWORK, refs: [MAIN, ref("refs/tags/v1.2.3")] }), {
    steps: [LINEAR],
  });
  for (const refs of [
    [ref("refs/heads/WIN1")],
    [ref("refs/heads/WIN1", "refs/heads/main")],
    [ref("refs/tags/test")],
    [ref("refs/tags/v1.2")],
    [ref("(delete)", "refs/heads/main", ZERO)],
    [MAIN, ref("refs/heads/other")],
  ]) {
    const plan = pushPlan({ role: "master", url: NETWORK, refs });
    assert.ok("refusal" in plan, JSON.stringify(refs));
    assert.match(plan.refusal, /publishes only main and release tags/);
  }
  const local = pushPlan({ role: "master", url: LOCAL, refs: [MAIN] });
  assert.ok("refusal" in local);
  assert.match(local.refusal, /pushes only to the public remote/);
});

/** Runs tools/git/work-folders.mjs of this repository in `cwd`. */
function workFolders(cwd, ...args) {
  return spawnSync(process.execPath, [path.join(repoRoot, "tools", "git", "work-folders.mjs"), ...args], {
    cwd,
    encoding: "utf8",
  });
}

/** A master (with a bare stand-in for GitHub) and work folder Monitone.win1 on WIN1, marked by guard-master. */
function makeSetup(t) {
  const dir = tempDir(t);
  const master = path.join(dir, "master");
  const github = path.join(dir, "github.git");
  initRepo(master);
  git(master, "config", "user.name", "Owner");
  git(master, "config", "user.email", "owner@example.com");
  git(dir, "init", "--bare", "--quiet", "--initial-branch=main", github);
  git(master, "remote", "add", "github", github);
  const work = path.join(dir, "Monitone.win1");
  git(dir, "clone", "--quiet", master, work);
  git(work, "config", "core.hooksPath", ".githooks");
  git(work, "config", "user.name", "Test");
  git(work, "config", "user.email", "test@example.com");
  git(work, "switch", "--quiet", "-c", "WIN1");
  const guard = workFolders(work, "guard-master");
  assert.equal(guard.status, 0, guard.stderr);
  return { dir, master, github, work };
}

test("guard-master marks the master and turns its hooks on", (t) => {
  const { master, work } = makeSetup(t);
  assert.equal(git(master, "config", "--local", "monitone.role"), "master");
  assert.equal(git(master, "config", "--local", "core.hooksPath"), ".githooks");
  assert.equal(checkoutRole(master), "master");
  assert.equal(checkoutRole(work), "work");
});

test("a work folder's push to the master runs the full check", (t) => {
  const { work } = makeSetup(t);
  commit(work, "change");
  const push = gitTry(work, "push", "origin", "WIN1");
  assert.equal(push.status, 0, push.stderr);
  assert.match(push.stdout, /no merge commits to push/);
  assert.match(push.stdout, new RegExp(CHECK_STUB_OUTPUT));
});

test("a work folder's push of a merge commit to the master is refused", (t) => {
  const { work } = makeSetup(t);
  mergeSide(work, "topic");
  const push = gitTry(work, "push", "origin", "WIN1");
  assert.notEqual(push.status, 0);
  assert.match(push.stderr, /merge commit [0-9a-f]+ chore: merge topic/);
});

test("a work folder's push to a network remote is refused with the master's command", (t) => {
  const { master, work } = makeSetup(t);
  const head = git(work, "rev-parse", "HEAD");
  const run = runPrePush(work, "github", NETWORK, `refs/heads/WIN1 ${head} refs/heads/main ${ZERO}\n`);
  assert.notEqual(run.status, 0);
  assert.match(run.stderr, /pre-push: refused: a work folder pushes only to the master/);
  assert.ok(run.stderr.includes(`push github main`), run.stderr);
  assert.ok(run.stderr.includes(path.resolve(master)) || run.stderr.includes(master), run.stderr);
});

test("the master publishes main to a network remote and nothing else", (t) => {
  const { master, github } = makeSetup(t);
  const head = git(master, "rev-parse", "HEAD");
  const main = runPrePush(master, "github", NETWORK, `refs/heads/main ${head} refs/heads/main ${ZERO}\n`);
  assert.equal(main.status, 0, main.stderr);
  const branch = runPrePush(master, "github", NETWORK, `refs/heads/WIN1 ${head} refs/heads/WIN1 ${ZERO}\n`);
  assert.notEqual(branch.status, 0);
  assert.match(branch.stderr, /publishes only main and release tags/);
  // A real push to a local path (the bare stand-in) is refused too.
  const local = gitTry(master, "push", "github", "main");
  assert.notEqual(local.status, 0);
  assert.match(local.stderr, /the master pushes only to the public remote/);
  assert.equal(git(github, "branch", "--list"), "");
});

test("create clones the master into a new work folder on WIN<n>", (t) => {
  const { dir, master, work } = makeSetup(t);
  const result = workFolders(work, "create", "2");
  assert.equal(result.status, 0, result.stderr);
  const second = path.join(dir, "Monitone.win2");
  assert.equal(git(second, "branch", "--show-current"), "WIN2");
  assert.equal(git(second, "config", "--local", "core.hooksPath"), ".githooks");
  assert.equal(git(second, "config", "--local", "user.email"), "owner@example.com");
  assert.equal(git(second, "rev-parse", "HEAD"), git(master, "rev-parse", "main"));
  assert.equal(checkoutRole(second), "work");
  const again = workFolders(work, "create", "2");
  assert.notEqual(again.status, 0);
  assert.match(again.stderr, /exists already/);
});
