# Contributing

## Commit messages

Commits on `main` follow [Conventional Commits](https://www.conventionalcommits.org/).
They drive versioning and the changelog, so the type matters:

| Type | Use for | Version bump (before 1.0 / after 1.0) |
| --- | --- | --- |
| `feat:` | new user-visible functionality | minor / minor |
| `fix:` | bug fixes | patch / patch |
| `perf:`, `refactor:` | no behaviour change | patch / patch |
| `docs:`, `build:`, `ci:`, `test:`, `style:`, `chore:` | everything else | none |
| `feat!:` or a `BREAKING CHANGE:` footer | incompatible changes | minor / major |

Pull requests are squash-merged and the PR title becomes the commit message;
the `pr-title` workflow checks it.

## Versions and releases

- `version.txt` is the single source of the version. Do not edit it by hand.
- [release-please](https://github.com/googleapis/release-please) keeps a
  release PR open that bumps `version.txt` and updates `CHANGELOG.md`.
- Merging the release PR tags `vX.Y.Z`, creates the GitHub release and the
  `release` workflow attaches `Monitone-X.Y.Z-x64.zip` plus its SHA-256.
- Builds embed the version in the Windows version resource (Explorer >
  Properties > Details) and show it in the About tab. Builds that are not
  exactly a release tag carry SemVer build metadata, e.g. `0.3.0+g1a2b3c4d`,
  with `.dirty` for uncommitted changes, and are flagged as pre-release.

Repository settings needed once: *Settings > Actions > General > Workflow
permissions*: "Read and write permissions" and "Allow GitHub Actions to create
and approve pull requests".

## Code

- C++20, formatted with the repository `.clang-format`.
- Build with `-DMONITONE_WARNINGS_AS_ERRORS=ON` before pushing; CI does.
