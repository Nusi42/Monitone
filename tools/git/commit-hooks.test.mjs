// Tests for the commit-msg and pre-commit hooks: node --test "tools/git/*.test.mjs".
// The hook tests commit in temporary repositories through the real hook files.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";

import { commitMessageProblems, messageText } from "./commit-message.mjs";
import { findClangFormat } from "./format.mjs";
import { git, initRepo, tempDir } from "./test-repos.mjs";
const clangFormat = findClangFormat();
const skipFormat = clangFormat === undefined ? "clang-format not found" : false;

test("messageText drops comments and everything below the scissors line", () => {
  const raw = "feat: x\n# a comment\r\nbody\n# ------------------------ >8 ------------------------\ndiff --git\n";
  assert.equal(messageText(raw), "feat: x\nbody\n");
});

test("commitMessageProblems accepts Conventional Commits subjects", () => {
  for (const ok of [
    "feat: add a tray tooltip",
    "fix(audio): keep the endpoint after sleep",
    "chore(release): 1.0.0",
    "feat!: drop the old INI keys",
    "fixup! fix(audio): keep the endpoint after sleep",
  ]) {
    assert.deepEqual(commitMessageProblems(`${ok}\n\nBody.\n`), [], ok);
  }
});

test("commitMessageProblems names what is wrong", () => {
  assert.match(commitMessageProblems("Add a tray tooltip")[0] ?? "", /<type>\(<scope>\): <subject>/);
  assert.match(commitMessageProblems("feature: x")[0] ?? "", /type of feat, fix/);
  assert.match(commitMessageProblems("feat:x")[0] ?? "", /<type>/);
  assert.match(commitMessageProblems(`feat: ${"x".repeat(80)}`)[0] ?? "", /at most 72/);
  assert.match(commitMessageProblems("feat: x\n\nClaude-Session: link\n")[0] ?? "", /session trailer/);
  assert.match(commitMessageProblems("feat: x\n\nsee https://claude.ai/chat/1\n")[0] ?? "", /claude\.ai/);
});

/** A repository that uses this repository's real hooks, check files and .clang-format. */
function makeRepo(t) {
  const dir = path.join(tempDir(t), "repo");
  initRepo(dir);
  return dir;
}

/** Writes and stages `name`, then commits with `message` without throwing. */
function commitFile(dir, name, content, message, env = process.env) {
  fs.mkdirSync(path.join(dir, path.dirname(name)), { recursive: true });
  fs.writeFileSync(path.join(dir, name), content);
  git(dir, "add", name);
  return spawnSync("git", ["commit", "-m", message], { cwd: dir, encoding: "utf8", env });
}

test("the commit-msg hook refuses a message without a type", (t) => {
  const dir = makeRepo(t);
  const bad = commitFile(dir, "notes.txt", "a", "Add notes");
  assert.notEqual(bad.status, 0);
  assert.match(bad.stderr, /commit message: the subject must read/);
  const good = spawnSync("git", ["commit", "-m", "docs: add notes"], { cwd: dir, encoding: "utf8" });
  assert.equal(good.status, 0, good.stderr);
});

test("the pre-commit hook refuses unformatted staged C++ and accepts formatted", { skip: skipFormat }, (t) => {
  const dir = makeRepo(t);
  const bad = commitFile(dir, "src/A.cpp", "int  main( ){return 0;}\n", "feat: a");
  assert.notEqual(bad.status, 0);
  assert.match(bad.stderr, /not formatted: src\/A\.cpp/);
  const good = commitFile(dir, "src/A.cpp", "int main() { return 0; }\n", "feat: a");
  assert.equal(good.status, 0, good.stderr);
});

test("the pre-commit hook checks the staged version, not the working tree", { skip: skipFormat }, (t) => {
  const dir = makeRepo(t);
  fs.mkdirSync(path.join(dir, "src"));
  fs.writeFileSync(path.join(dir, "src/B.h"), "int  f( );\n");
  git(dir, "add", "src/B.h");
  fs.writeFileSync(path.join(dir, "src/B.h"), "int f();\n");
  const commit = spawnSync("git", ["commit", "-m", "feat: b"], { cwd: dir, encoding: "utf8" });
  assert.notEqual(commit.status, 0);
  assert.match(commit.stderr, /not formatted: src\/B\.h/);
});

test("without clang-format the pre-commit hook passes with a notice", (t) => {
  const dir = makeRepo(t);
  const env = { ...process.env, MONITONE_CLANG_FORMAT: "" };
  const commit = commitFile(dir, "src/C.cpp", "int  g( );\n", "feat: c", env);
  assert.equal(commit.status, 0, commit.stderr);
  assert.match(commit.stderr, /clang-format not found/);
});
