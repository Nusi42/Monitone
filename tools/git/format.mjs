// Formatting check for the pre-commit hook (.githooks/pre-commit ->
// tools/git/check-format.mjs): the staged version of every added or changed
// C++ file must match .clang-format. It only checks, never rewrites. clang-format
// comes from PATH, else from the latest Visual Studio (VC\Tools\Llvm).
import { execFileSync, spawnSync } from "node:child_process";
import path from "node:path";

import { findTool } from "./find-tool.mjs";

/** The files the check covers. */
export const CPP_FILE = /\.(?:cpp|cc|cxx|h|hpp|hh|inl)$/i;

/**
 * The clang-format executable, or `undefined` when there is none.
 * `MONITONE_CLANG_FORMAT` overrides the search (empty: none).
 *
 * @param {NodeJS.ProcessEnv} env
 * @returns {string | undefined}
 */
export function findClangFormat(env = process.env) {
  if (env.MONITONE_CLANG_FORMAT !== undefined) {
    return env.MONITONE_CLANG_FORMAT === "" ? undefined : env.MONITONE_CLANG_FORMAT;
  }
  return findTool("clang-format", path.join("VC", "Tools", "Llvm", "x64", "bin", "clang-format.exe"), env);
}

/**
 * The staged C++ files that are added, copied, modified or renamed.
 *
 * @param {string} root
 * @returns {string[]}
 */
export function stagedCppFiles(root) {
  const out = execFileSync("git", ["diff", "--cached", "--name-only", "-z", "--diff-filter=ACMR"], {
    cwd: root,
    encoding: "utf8",
  });
  return out.split("\0").filter((file) => CPP_FILE.test(file));
}

/**
 * The staged files whose content clang-format would change.
 *
 * @param {string} root
 * @param {string} clangFormat
 * @param {string[]} files paths relative to `root`
 * @returns {string[]}
 */
export function unformattedFiles(root, clangFormat, files) {
  return files.filter((file) => {
    const staged = execFileSync("git", ["show", `:${file}`], { cwd: root });
    const result = spawnSync(clangFormat, ["--dry-run", "-Werror", "--style=file", `--assume-filename=${file}`], {
      cwd: root,
      input: staged,
      encoding: "utf8",
    });
    if (result.error) throw result.error;
    return result.status !== 0;
  });
}
