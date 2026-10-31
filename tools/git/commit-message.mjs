// Commit message rules, checked by the commit-msg hook
// (.githooks/commit-msg -> tools/git/check-commit-msg.mjs). The subject follows
// Conventional Commits because tools/build-release.ps1 derives the version and
// the release notes from it (types: CONTRIBUTING.md "Commit messages"). No
// session links: the repository is public (CLAUDE.md).

/** Conventional Commits types, as listed in CONTRIBUTING.md. */
export const TYPES = ["feat", "fix", "perf", "refactor", "docs", "build", "test", "style", "chore"];

/** The longest subject line, type and scope included. */
export const MAX_SUBJECT = 72;

const SUBJECT = new RegExp(`^(?:${TYPES.join("|")})(?:\\([a-z0-9-]+\\))?!?: \\S`);

/** Subjects `git commit --fixup` / `--squash` writes; a rebase folds them away. */
const AUTOSQUASH = /^(?:fixup|squash|amend)! /;

/** What a message must not contain, with the reason shown to the writer. */
const FORBIDDEN = [
  { re: /^claude-session\b/im, why: "no session trailer lines" },
  { re: /claude\.ai\//i, why: "no links to claude.ai pages" },
];

/**
 * The message as git records it: comment lines and everything below the
 * scissors line (`git commit -v`) removed.
 *
 * @param {string} raw the commit message file
 * @returns {string}
 */
export function messageText(raw) {
  return (raw.split(/^# -+ >8 -+$/m)[0] ?? "")
    .split(/\r?\n/)
    .filter((line) => !line.startsWith("#"))
    .join("\n");
}

/**
 * The problems of a commit message; empty when it follows the rules.
 *
 * @param {string} text the message, comments already removed
 * @returns {string[]}
 */
export function commitMessageProblems(text) {
  const subject = text.trimStart().split("\n")[0]?.trimEnd() ?? "";
  /** @type {string[]} */
  const problems = [];
  if (!AUTOSQUASH.test(subject)) {
    if (!SUBJECT.test(subject)) {
      problems.push(`the subject must read "<type>(<scope>): <subject>" with a type of ${TYPES.join(", ")}`);
    }
    if (subject.length > MAX_SUBJECT) {
      problems.push(`the subject is ${String(subject.length)} characters long (at most ${String(MAX_SUBJECT)})`);
    }
  }
  for (const rule of FORBIDDEN) {
    const match = rule.re.exec(text);
    if (match) problems.push(`"${match[0].trim()}": ${rule.why}`);
  }
  return problems;
}
