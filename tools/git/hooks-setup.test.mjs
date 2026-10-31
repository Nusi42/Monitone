// Tests for cmake/GitHooks.cmake, which CMake runs at configure time to set
// core.hooksPath: node --test "tools/git/*.test.mjs". Skipped without cmake on
// PATH (Developer PowerShell for VS 2022 has it).
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const script = path.join(repoRoot, "cmake", "GitHooks.cmake");
const hasCmake = spawnSync("cmake", ["--version"], { stdio: "ignore" }).status === 0;
const skip = hasCmake ? false : "cmake is not on PATH";

/** Runs cmake/GitHooks.cmake for `dir`. */
function runScript(dir) {
  return spawnSync("cmake", [`-DSOURCE_DIR=${dir}`, "-P", script], { encoding: "utf8" });
}

/** The local core.hooksPath of the repository at `dir`, or "" when unset. */
function hooksPath(dir) {
  const result = spawnSync("git", ["config", "--local", "--get", "core.hooksPath"], {
    cwd: dir,
    encoding: "utf8",
  });
  return result.stdout.trim();
}

/** A temporary directory, removed after the test. */
function tempDir(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "monitone-hooks-"));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

test("a fresh clone gets core.hooksPath .githooks", { skip }, (t) => {
  const dir = tempDir(t);
  execFileSync("git", ["init", "--quiet", dir]);
  const first = runScript(dir);
  assert.equal(first.status, 0, first.stderr);
  assert.equal(hooksPath(dir), ".githooks");
  assert.match(first.stdout + first.stderr, /core\.hooksPath set to \.githooks/);
  const again = runScript(dir);
  assert.equal(again.status, 0, again.stderr);
  assert.doesNotMatch(again.stdout + again.stderr, /set to/);
});

test("a different core.hooksPath is kept, with a warning", { skip }, (t) => {
  const dir = tempDir(t);
  execFileSync("git", ["init", "--quiet", dir]);
  execFileSync("git", ["config", "core.hooksPath", "elsewhere"], { cwd: dir });
  const result = runScript(dir);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(hooksPath(dir), "elsewhere");
  assert.match(result.stderr, /core\.hooksPath is 'elsewhere'/);
});

test("outside a git checkout nothing happens", { skip }, (t) => {
  const dir = tempDir(t);
  const result = runScript(dir);
  assert.equal(result.status, 0, result.stderr);
  assert.equal(result.stdout + result.stderr, "");
});
