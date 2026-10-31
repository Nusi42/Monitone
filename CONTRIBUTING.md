# Contributing

Thanks for helping. Monitone is a small project with one maintainer, so short,
focused changes are easiest to review.

## Questions, bugs and ideas

- Questions and setup help: [Discussions](https://github.com/Nusi42/Monitone/discussions).
- Bugs and feature requests: [open an issue](https://github.com/Nusi42/Monitone/issues/new/choose)
  with the form. For bugs, your screens, speakers and the affected program
  matter most.
- Security problems: privately, see [SECURITY.md](SECURITY.md).

Everyone follows the [code of conduct](CODE_OF_CONDUCT.md).

## Pull requests

1. For anything larger than a small fix, open an issue first so we can agree
   on the approach.
2. Fork, create a branch, make the change. Building is described in
   [docs/BUILDING.md](docs/BUILDING.md), the design in
   [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
3. Build with `-DMONITONE_WARNINGS_AS_ERRORS=ON` and test on Windows.
4. Open the pull request with a title in Conventional Commits format (below)
   and fill in the checklist.

`main` is protected: changes arrive through pull requests and are
squash-merged. There is no CI; the maintainer builds and tests locally.

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

Releases are built, tested and published from a Windows PC in two steps;
there is no CI.

```
tools\build-release.ps1 -DryRun   # shows the next version and its release notes
tools\build-release.ps1           # builds the release candidate; changes nothing in git
# ... test the packages in build\release-candidate\dist ...
tools\publish-release.ps1         # commits, tags, pushes, creates the GitHub release
```

- `version.txt` is the single source of the version. Do not edit it by hand.
- **Build** (`build-release.ps1`, from any PowerShell on a clean `main`; it
  enters the Visual Studio developer environment itself): the next version
  follows from the commits since the last tag; `-Bump major|minor|patch` or
  `-Version X.Y.Z` override it. The first release uses `version.txt` as it is.
  It builds with MSVC as exactly that version (`-DMONITONE_RELEASE_VERSION`),
  checks the binaries and writes to `build\release-candidate\dist`: the portable zip,
  the NSIS setup, the MSI package, `SHA256SUMS.txt`, `release-notes.md` and
  `release.json`.
- **Release notes** are generated from the commits. If
  `docs\release-notes\X.Y.Z.md` exists, it opens the notes; the first release
  consists of that file only. You can also edit `release-notes.md` in `dist`
  before publishing.
- **Test** the three packages. If something is wrong, fix it, commit, and
  build again.
- **Publish** (`publish-release.ps1`) checks that `main` is still at the built
  commit and the packages are unchanged, asks for confirmation, updates
  `version.txt` and `CHANGELOG.md`, commits `chore(release): X.Y.Z`, tags
  `vX.Y.Z`, pushes both and creates the GitHub release with the packages. It
  needs the GitHub CLI, signed in (`gh auth login`).
- Tools: Visual Studio 2022 with C++ and CMake tools, NSIS 3
  (`winget install NSIS.NSIS`), WiX Toolset 5 with the Util extension
  (`dotnet tool install --global wix --version 5.0.2`, then
  `wix extension add --global WixToolset.Util.wixext/5.0.2`), GitHub CLI
  (`winget install GitHub.cli`).
- Installers: `installer\Monitone.nsi` and `installer\Monitone.wxs`. Both
  install per user into `%LOCALAPPDATA%\Programs\Monitone` without
  administrator rights, close a running Monitone first, and remove *Start
  with Windows* on uninstall. Never change the MSI `UpgradeCode`.
- Builds embed the version in the Windows version resource (Explorer >
  Properties > Details) and show it on the About page. Builds that are not
  exactly a release tag carry SemVer build metadata, e.g. `0.3.0+g1a2b3c4d`,
  with `.dirty` for uncommitted changes, and are flagged as pre-release.

## Code

- C++20, formatted with the repository `.clang-format`.
- Build with `-DMONITONE_WARNINGS_AS_ERRORS=ON` before pushing.
