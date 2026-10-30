# Contributing

## Commit messages

Commits on `main` follow [Conventional Commits](https://www.conventionalcommits.org/).
They drive versioning and the changelog, so the type matters:

| Type | Use for | Version bump (before 1.0 / after 1.0) |
| --- | --- | --- |
| `feat:` | new user-visible functionality | minor / minor |
| `fix:` | bug fixes | patch / patch |
| `perf:`, `refactor:` | no behaviour change | patch / patch |
| `docs:`, `build:`, `test:`, `style:`, `chore:` | everything else | none |
| `feat!:` or a `BREAKING CHANGE:` footer | incompatible changes | minor / major |

Pull requests are squash-merged, so the PR title becomes the commit message
and must follow the same format.

## Versions and releases

Releases are built and published from a Windows PC with
`tools\release.ps1`; there is no CI.

- `version.txt` is the single source of the version. Do not edit it by hand.
- Run the script from the *Developer PowerShell for VS 2022* on a clean
  `main`:

  ```
  tools\release.ps1 -DryRun     # shows the next version and its changelog
  tools\release.ps1             # releases locally: commit, tag, build, zip
  tools\release.ps1 -Publish    # also pushes and creates the GitHub release
  ```

  The next version follows from the commits since the last tag; `-Bump
  major|minor|patch` or `-Version X.Y.Z` override it.
- The script updates `version.txt` and `CHANGELOG.md`, commits
  `chore(release): X.Y.Z`, tags `vX.Y.Z`, builds with MSVC and checks that the
  binaries carry exactly that version. If the build fails, commit and tag are
  removed again.
- The result is `build\release\dist\Monitone-X.Y.Z-x64.zip` plus its SHA-256.
  `-Publish` uploads both to a new GitHub release if the GitHub CLI (`gh`) is
  installed and signed in; otherwise it prints the link to do it by hand.
- Builds embed the version in the Windows version resource (Explorer >
  Properties > Details) and show it on the About page. Builds that are not
  exactly a release tag carry SemVer build metadata, e.g. `0.3.0+g1a2b3c4d`,
  with `.dirty` for uncommitted changes, and are flagged as pre-release.

## Code

- C++20, formatted with the repository `.clang-format`.
- Build with `-DMONITONE_WARNINGS_AS_ERRORS=ON` before pushing.
