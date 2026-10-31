// Helpers for the hook tests (tools/git/*.test.mjs): throw-away repositories
// that run this repository's real hooks and scripts through git.
import { execFileSync, spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

/** What the stub of tools/check.mjs prints, so a test sees that the full check ran. */
export const CHECK_STUB_OUTPUT = "stub: full check ran";

/**
 * Runs git in `cwd`; returns trimmed stdout, throws on failure.
 *
 * @param {string} cwd
 * @param {...string} args
 * @returns {string}
 */
export function git(cwd, ...args) {
  return execFileSync("git", args, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }).trim();
}

/**
 * Runs git in `cwd` without throwing.
 *
 * @param {string} cwd
 * @param {...string} args
 */
export function gitTry(cwd, ...args) {
  return spawnSync("git", args, { cwd, encoding: "utf8" });
}

/**
 * A temporary directory, removed after the test.
 *
 * @param {import("node:test").TestContext} t
 * @returns {string}
 */
export function tempDir(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "monitone-git-"));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  return dir;
}

/**
 * Initializes a repository at `dir` with this repository's hooks, the
 * scripts in tools/git, .clang-format and a stub tools/check.mjs, commits
 * them as "chore: base" and turns the hooks on.
 *
 * @param {string} dir
 */
export function initRepo(dir) {
  git(path.dirname(dir), "init", "--quiet", "--initial-branch=main", dir);
  const files = [".clang-format", ...fs.readdirSync(path.join(repoRoot, ".githooks")).map((f) => `.githooks/${f}`)];
  for (const file of fs.readdirSync(path.join(repoRoot, "tools", "git"))) {
    if (file.endsWith(".mjs") && !file.endsWith(".test.mjs")) files.push(`tools/git/${file}`);
  }
  for (const rel of files) {
    fs.mkdirSync(path.join(dir, path.dirname(rel)), { recursive: true });
    fs.copyFileSync(path.join(repoRoot, rel), path.join(dir, rel));
  }
  fs.writeFileSync(path.join(dir, "tools", "check.mjs"), `console.log(${JSON.stringify(CHECK_STUB_OUTPUT)});\n`);
  git(dir, "config", "user.name", "Test");
  git(dir, "config", "user.email", "test@example.com");
  git(dir, "config", "core.hooksPath", ".githooks");
  git(dir, "add", "-A");
  git(dir, "commit", "--quiet", "-m", "chore: base");
}

/**
 * Adds a file and commits it as "chore: <name>".
 *
 * @param {string} cwd
 * @param {string} name
 */
export function commit(cwd, name) {
  fs.writeFileSync(path.join(cwd, `${name}.txt`), name);
  git(cwd, "add", "-A");
  git(cwd, "commit", "--quiet", "-m", `chore: ${name}`);
}

/**
 * Makes a merge commit on the current branch from a side branch.
 *
 * @param {string} cwd
 * @param {string} name
 */
export function mergeSide(cwd, name) {
  const branch = git(cwd, "branch", "--show-current");
  git(cwd, "switch", "--quiet", "-c", name);
  commit(cwd, `${name}-side`);
  git(cwd, "switch", "--quiet", branch);
  commit(cwd, `${name}-main`);
  git(cwd, "merge", "--no-ff", "--quiet", "-m", `chore: merge ${name}`, name);
}

let pushLinesFiles = 0;

/**
 * Runs the pre-push hook of `cwd` through `git hook run` with `lines` on stdin.
 *
 * @param {string} cwd
 * @param {string} remote
 * @param {string} url
 * @param {string} lines git's pre-push ref lines
 */
export function runPrePush(cwd, remote, url, lines) {
  const stdin = path.join(path.dirname(cwd), `push-lines-${String(++pushLinesFiles)}.txt`);
  fs.writeFileSync(stdin, lines);
  return gitTry(cwd, "hook", "run", `--to-stdin=${stdin}`, "pre-push", "--", remote, url);
}
