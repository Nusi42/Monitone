// Finds a developer tool on PATH, else in the latest Visual Studio (found with
// vswhere), so the hooks and tools/check.mjs also work outside the Developer
// PowerShell.
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

/**
 * The installation folders of Visual Studio, newest first; empty without one.
 *
 * @param {NodeJS.ProcessEnv} env
 * @returns {string[]}
 */
export function visualStudioFolders(env = process.env) {
  const programFiles = env["ProgramFiles(x86)"];
  if (programFiles === undefined) return [];
  const vswhere = path.join(programFiles, "Microsoft Visual Studio", "Installer", "vswhere.exe");
  const found = spawnSync(vswhere, ["-latest", "-products", "*", "-property", "installationPath"], {
    encoding: "utf8",
  });
  return found.status === 0 ? found.stdout.split(/\r?\n/).filter(Boolean) : [];
}

/**
 * `name` when it runs from PATH, else the first `relative` path inside a
 * Visual Studio folder that exists, else `undefined`.
 *
 * @param {string} name
 * @param {string} relative path inside the Visual Studio folder
 * @param {NodeJS.ProcessEnv} env
 * @returns {string | undefined}
 */
export function findTool(name, relative, env = process.env) {
  if (spawnSync(name, ["--version"], { stdio: "ignore" }).status === 0) return name;
  for (const vs of visualStudioFolders(env)) {
    const exe = path.join(vs, relative);
    if (fs.existsSync(exe)) return exe;
  }
  return undefined;
}
