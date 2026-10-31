// The commit identity: in a checkout whose git config sets monitone.identity
// (`Name <address>`, the owner's master and work folders), every author and
// committer must be exactly that identity. Checked by tools/git/check-private.mjs
// in the commit-msg hook (the identity about to be recorded) and the pre-push
// hook (the pushed commits). Without the key (a contributor's clone) nothing
// is checked.

/** The git config key that names the only identity allowed in this checkout. */
export const IDENTITY_KEY = "monitone.identity";

/**
 * `Name <address>` of a git identity line (`git var GIT_AUTHOR_IDENT` adds a
 * time stamp after the address).
 *
 * @param {string} ident
 * @returns {string}
 */
export function identityOf(ident) {
  return ident.trim().replace(/>.*$/s, ">");
}

/**
 * The problem with one identity, or `undefined` when it is the expected one.
 *
 * @param {string} where e.g. `commit 1a2b3c4d author`
 * @param {string} actual `Name <address>`
 * @param {string} expected `Name <address>`
 * @returns {string | undefined}
 */
export function identityProblem(where, actual, expected) {
  if (identityOf(actual) === identityOf(expected)) return undefined;
  return `${where}: the identity is "${identityOf(actual)}", only "${identityOf(expected)}" is allowed here`;
}
