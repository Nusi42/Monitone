#!/usr/bin/env node
// Pre-commit check: staged C++ files match .clang-format (see tools/git/format.mjs).
// Runs unconditionally on load, so the hook can never pass silently.
import { execFileSync } from "node:child_process";

import { findClangFormat, stagedCppFiles, unformattedFiles } from "./format.mjs";

const root = execFileSync("git", ["rev-parse", "--show-toplevel"], { encoding: "utf8" }).trim();
const files = stagedCppFiles(root);
const clangFormat = files.length > 0 ? findClangFormat() : undefined;
if (files.length > 0 && clangFormat === undefined) {
  console.error("format: clang-format not found (PATH or Visual Studio); formatting not checked");
} else if (clangFormat !== undefined) {
  const bad = unformattedFiles(root, clangFormat, files);
  if (bad.length > 0) {
    for (const file of bad) console.error(`not formatted: ${file}`);
    console.error(
      `\nformat: ${String(bad.length)} staged file(s) differ from .clang-format. ` +
        "Run clang-format -i on them, stage the result and commit again.",
    );
    process.exitCode = 1;
  }
}
