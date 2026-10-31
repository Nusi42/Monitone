<#
.SYNOPSIS
    Publishes the release candidate built by tools\build-release.ps1 on GitHub.

.DESCRIPTION
    1. Checks that this is the master checkout or a plain clone (not a work
       folder), that main is clean and still at the commit the candidate was
       built from, and that the packages in <WorkDir>\dist are unchanged
       (SHA256SUMS.txt).
    2. Asks for confirmation.
    3. Updates version.txt and CHANGELOG.md (from release-notes.md), commits
       "chore(release): X.Y.Z" and creates the tag vX.Y.Z.
    4. Pushes main and the tag: from the master to its remote "github", from
       a plain clone to "origin". The pre-push hook checks the push.
    5. Creates the GitHub release with release-notes.md and uploads the zip,
       the setup, the MSI and SHA256SUMS.txt.

    Needs the GitHub CLI, signed in: winget install GitHub.cli; gh auth login

.EXAMPLE
    tools\publish-release.ps1
#>
[CmdletBinding()]
param(
    [switch]$Yes,
    [string]$WorkDir = (Join-Path $env:LOCALAPPDATA 'Monitone-release')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Repository = 'https://github.com/Nusi42/Monitone'
$Root = Split-Path -Parent $PSScriptRoot
$Dist = Join-Path $WorkDir 'dist'
$Utf8NoBom = New-Object System.Text.UTF8Encoding $false

function Invoke-Git {
    $output = & git -C $Root @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') failed" }
    return $output
}

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command $($Arguments -join ' ') failed" }
}

function Write-Utf8([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
}

# The checkout's role (docs/BUILDING.md "Master and work folders"): 'master'
# (local git config monitone.role=master), 'work' (its origin is the master)
# or 'plain'. The same logic is in tools\build-release.ps1.
function Get-CheckoutRole {
    if ((& git -C $Root config --local --get monitone.role) -eq 'master') { return 'master' }
    $origin = & git -C $Root config --local --get remote.origin.url
    if ($origin -and $origin -match '^([A-Za-z]:[\\/]|\\\\|/|\.\.?([\\/]|$)|file://)') {
        $dir = if ($origin -match '^file://') { ([uri]$origin).LocalPath }
               else { [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Root, $origin)) }
        if ((& git -C $dir config --local --get monitone.role) -eq 'master') { return 'work' }
    }
    return 'plain'
}

# --- Preconditions -------------------------------------------------------------

$role = Get-CheckoutRole
if ($role -eq 'work') { throw 'Releases are published from the master checkout, not from a work folder.' }
$remote = if ($role -eq 'master') { 'github' } else { 'origin' }

$manifestPath = Join-Path $Dist 'release.json'
if (-not (Test-Path $manifestPath)) { throw 'No release candidate. Run tools\build-release.ps1 first.' }
$release = Get-Content $manifestPath -Raw | ConvertFrom-Json
$Version = $release.version
$tag = $release.tag

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { throw 'The GitHub CLI (gh) is not installed: winget install GitHub.cli' }
& gh auth status --hostname github.com *> $null
if ($LASTEXITCODE -ne 0) { throw 'The GitHub CLI is not signed in: gh auth login' }

$branch = (Invoke-Git rev-parse --abbrev-ref HEAD) | Select-Object -First 1
if ($branch -ne 'main') { throw "Releases are made from main, not '$branch'." }
if (Invoke-Git status --porcelain) { throw 'The working tree has uncommitted changes.' }
$head = (Invoke-Git rev-parse HEAD) | Select-Object -First 1
if ($head -ne $release.commit) {
    throw "main has moved since the candidate was built ($($release.commit.Substring(0, 8)) -> $($head.Substring(0, 8))). Run tools\build-release.ps1 again."
}
if (& git -C $Root tag --list $tag) { throw "Tag $tag exists already." }

foreach ($line in Get-Content (Join-Path $Dist 'SHA256SUMS.txt')) {
    if ($line -notmatch '^(?<hash>[0-9a-f]{64})  (?<name>.+)$') { continue }
    $file = Join-Path $Dist $Matches.name
    if (-not (Test-Path $file)) { throw "$($Matches.name) is missing." }
    if ((Get-FileHash $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Matches.hash) {
        throw "$($Matches.name) changed after the build. Run tools\build-release.ps1 again."
    }
}
$assets = @($release.assets | ForEach-Object { Join-Path $Dist $_ })

$notesFile = Join-Path $Dist 'release-notes.md'
$releaseNotes = [System.IO.File]::ReadAllText($notesFile).Trim()

# --- Confirm -------------------------------------------------------------------------

Write-Host "Publishing Monitone $Version ($tag) to $Repository" -ForegroundColor Cyan
Write-Host ''
Write-Host $releaseNotes
Write-Host ''
$assets | ForEach-Object { Write-Host "  $(Split-Path -Leaf $_)" }
Write-Host ''
if (-not $Yes) {
    $answer = Read-Host 'This pushes main and the tag and creates a public GitHub release. Continue? [y/N]'
    if ($answer -notin 'y', 'yes') { Write-Host 'Nothing published.' -ForegroundColor Yellow; return }
}

# --- Commit and tag ------------------------------------------------------------------

$date = Get-Date -Format 'yyyy-MM-dd'
$heading = if ($release.previousTag) {
    "## [$Version]($Repository/compare/$($release.previousTag)...$tag) ($date)"
} else {
    "## $Version ($date)"
}
$changelogPath = Join-Path $Root 'CHANGELOG.md'
$previous = if (Test-Path $changelogPath) { [System.IO.File]::ReadAllText($changelogPath) -replace '^# Changelog\s*', '' } else { '' }
Write-Utf8 $changelogPath ("# Changelog`n`n$heading`n`n$releaseNotes`n`n$previous".TrimEnd() + "`n")
Write-Utf8 (Join-Path $Root 'version.txt') "$Version`n"

Invoke-Git add version.txt CHANGELOG.md | Out-Null
Invoke-Git commit --quiet -m "chore(release): $Version" | Out-Null
Invoke-Git tag -a $tag -m "Monitone $Version" | Out-Null

# --- Publish -------------------------------------------------------------------------

try {
    Invoke-Git push $remote main $tag | Out-Null
}
catch {
    Write-Host "Push failed. The release commit and tag $tag exist only locally; fix the problem and run:" -ForegroundColor Red
    Write-Host "  git push $remote main $tag"
    Write-Host "  gh release create $tag --repo $Repository --title `"Monitone $Version`" --notes-file `"$notesFile`" <assets in $Dist>"
    throw
}

Invoke-Checked gh (@('release', 'create', $tag) + $assets + @('--repo', $Repository,
    '--title', "Monitone $Version", '--notes-file', $notesFile))
Write-Host ''
Write-Host "Published $Repository/releases/tag/$tag" -ForegroundColor Green
