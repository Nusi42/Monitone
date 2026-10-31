#!/usr/bin/env node
// The full check, run by the pre-push hook before a work folder pushes to the
// master (tools/git/push-plan.mjs), and by hand:
//   node tools/check.mjs
// 1. CMake build with MSVC, Release, warnings as errors (build/);
// 2. the git tooling tests (tools/git/*.test.mjs);
// 3. the UI preview check (tools/ui-preview/preview.mjs, needs Playwright).
// cmake comes from PATH, else from the latest Visual Studio.
import { execFileSync, spawnSync } from "node:child_process";
import path from "node:path";

import { findTool } from "./git/find-tool.mjs";

const root = execFileSync("git", ["rev-parse", "--show-toplevel"], { encoding: "utf8" }).trim();
const build = path.join(root, "build");

/**
 * Runs one step; returns whether it passed.
 *
 * @param {string} title
 * @param {string} command
 * @param {string[]} args
 * @param {NodeJS.ProcessEnv} [env]
 * @returns {boolean}
 */
function step(title, command, args, env = process.env) {
  console.log(`check: ${title}`);
  const result = spawnSync(command, args, { cwd: root, stdio: "inherit", env });
  if (result.error) console.error(`check: ${result.error.message}`);
  return result.status === 0;
}

/** @returns {number} */
function main() {
  const cmake = findTool(
    "cmake",
    path.join("Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "cmake.exe"),
  );
  if (cmake === undefined) {
    console.error('check: cmake not found. Install "C++ CMake tools for Windows" with the Visual Studio Installer.');
    return 1;
  }
  // The tests of cmake/Checkout.cmake need cmake on PATH (Windows names the variable "Path").
  const pathKey = Object.keys(process.env).find((key) => key.toUpperCase() === "PATH") ?? "PATH";
  const env =
    cmake === "cmake"
      ? process.env
      : { ...process.env, [pathKey]: `${path.dirname(cmake)}${path.delimiter}${process.env[pathKey] ?? ""}` };
  if (
    !step("configure", cmake, ["-S", root, "-B", build, "-A", "x64", "-DMONITONE_WARNINGS_AS_ERRORS=ON"]) ||
    !step("build", cmake, ["--build", build, "--config", "Release", "--parallel"]) ||
    !step("git tooling tests", process.execPath, ["--test", "tools/git/*.test.mjs"], env)
  ) {
    console.error("check: failed");
    return 1;
  }
  if (!step("UI preview", process.execPath, ["tools/ui-preview/preview.mjs", path.join(build, "ui-preview")])) {
    console.error(
      "check: failed. The UI preview needs Playwright: npm install --no-save playwright && npx playwright install chromium",
    );
    return 1;
  }
  console.log("check: passed");
  return 0;
}

process.exitCode = main();
