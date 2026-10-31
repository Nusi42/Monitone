#!/usr/bin/env node
// Master and work folders (docs/BUILDING.md "Master and work folders").
// Optional: only for a maintainer who keeps a master checkout. Run from a
// work folder; the master is its `origin` (a local checkout).
//
//   node tools/git/work-folders.mjs guard-master
//       mark `origin` as the master (local git config monitone.role=master)
//       and turn its hooks on (core.hooksPath .githooks)
//   node tools/git/work-folders.mjs create <n>
//       clone the master into <parent of this folder>/Monitone.win<n> on a
//       new branch WIN<n> from main, with the master's commit identity and
//       the hooks turned on
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

import { ROLE_KEY, isLocalUrl, isMaster, localConfig, localPath } from "./push-plan.mjs";

/** Prefixes of the work folders and their branches. */
export const FOLDER_PREFIX = "Monitone.win";
export const BRANCH_PREFIX = "WIN";

/**
 * Runs git in `cwd` and returns its trimmed output.
 *
 * @param {string} cwd
 * @param {string[]} args
 * @returns {string}
 */
function git(cwd, args) {
  return execFileSync("git", args, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "inherit"] }).trim();
}

/**
 * The local checkout this work folder's `origin` names.
 *
 * @param {string} root
 * @returns {string}
 */
function originCheckout(root) {
  const url = localConfig(root, "remote.origin.url");
  if (url === undefined || !isLocalUrl(url)) throw new Error(`origin of ${root} is not a local path`);
  const dir = localPath(root, url);
  if (!fs.existsSync(path.join(dir, ".git"))) throw new Error(`origin of ${root} is ${dir}, not a checkout`);
  return dir;
}

/**
 * Marks `origin` as the master and turns its hooks on.
 *
 * @param {string} root
 */
function guardMaster(root) {
  const master = originCheckout(root);
  git(master, ["config", "--local", ROLE_KEY, "master"]);
  git(master, ["config", "--local", "core.hooksPath", ".githooks"]);
  console.log(`${master}: marked as the master (${ROLE_KEY}=master); core.hooksPath = .githooks`);
}

/**
 * Creates work folder `n` next to this one.
 *
 * @param {string} root
 * @param {number} n
 */
function create(root, n) {
  const master = originCheckout(root);
  if (!isMaster(master)) throw new Error(`${master} is not marked as the master; run guard-master first`);
  const target = path.join(path.dirname(root), `${FOLDER_PREFIX}${String(n)}`);
  if (fs.existsSync(target)) throw new Error(`${target} exists already`);
  const branch = `${BRANCH_PREFIX}${String(n)}`;
  execFileSync("git", ["clone", "--quiet", master, target], { stdio: "inherit" });
  for (const key of ["user.name", "user.email"]) {
    const value = localConfig(master, key);
    if (value !== undefined) git(target, ["config", "--local", key, value]);
  }
  git(target, ["config", "--local", "core.hooksPath", ".githooks"]);
  git(target, ["switch", "--quiet", "--no-track", "-c", branch, "origin/main"]);
  console.log(`${target}: branch ${branch} from main; publish it with: git push -u origin ${branch}`);
}

const root = git(process.cwd(), ["rev-parse", "--show-toplevel"]);
const [command, arg, ...rest] = process.argv.slice(2);
try {
  if (command === "guard-master" && arg === undefined) {
    guardMaster(root);
  } else if (command === "create" && arg !== undefined && /^[1-9]\d*$/.test(arg) && rest.length === 0) {
    create(root, Number(arg));
  } else {
    console.error("usage: node tools/git/work-folders.mjs guard-master | create <n>");
    process.exitCode = 2;
  }
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
