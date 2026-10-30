<#
.SYNOPSIS
    Builds and packages a Monitone release on this PC.

.DESCRIPTION
    1. Checks that the working tree is clean and on branch main.
    2. Works out the next version from the Conventional Commits since the last
       release tag (feat = minor, fix/perf/refactor = patch, breaking = major,
       or minor before 1.0), unless -Version or -Bump says otherwise.
    3. Updates version.txt and CHANGELOG.md, commits "chore(release): X.Y.Z"
       and creates the tag vX.Y.Z.
    4. Builds with CMake and MSVC (Release, warnings as errors) and checks that
       the binaries carry exactly version X.Y.Z.
    5. Packages build\release\dist\Monitone-X.Y.Z-x64.zip and its SHA-256.
    6. With -Publish: pushes main and the tag, and creates the GitHub release
       with the zip if the GitHub CLI (gh) is installed and signed in.

    If the build fails, the release commit and tag are removed again.

.EXAMPLE
    tools\release.ps1 -DryRun
    Shows the next version and its changelog without changing anything.

.EXAMPLE
    tools\release.ps1 -Publish
    Releases the next version and publishes it on GitHub.

.EXAMPLE
    tools\release.ps1 -Version 1.0.0
    Releases an explicit version, locally only.
#>
[CmdletBinding()]
param(
    [string]$Version,
    [ValidateSet('auto', 'major', 'minor', 'patch')]
    [string]$Bump = 'auto',
    [switch]$Publish,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Repository = 'https://github.com/Nusi42/Monitone'
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root 'build\release'
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

# --- Preconditions -------------------------------------------------------------

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'git is not installed.' }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'cmake is not on PATH. Run this script from the "Developer PowerShell for VS 2022".'
}

$branch = (Invoke-Git rev-parse --abbrev-ref HEAD) | Select-Object -First 1
if ($branch -ne 'main') { throw "Releases are made from main, not '$branch'." }
if (Invoke-Git status --porcelain) { throw 'The working tree has uncommitted changes.' }

# --- Next version ----------------------------------------------------------------

$current = (Get-Content (Join-Path $Root 'version.txt') -TotalCount 1).Trim()
if ($current -notmatch '^(\d+)\.(\d+)\.(\d+)$') { throw "version.txt contains '$current'." }
$major = [int]$Matches[1]; $minor = [int]$Matches[2]; $patch = [int]$Matches[3]

# Windows PowerShell turns redirected stderr of native commands into errors,
# so relax the preference while asking for a tag that may not exist yet.
$ErrorActionPreference = 'Continue'
$lastTag = & git -C $Root describe --tags --abbrev=0 --match 'v*' 2>$null
if ($LASTEXITCODE -ne 0) { $lastTag = $null }
$ErrorActionPreference = 'Stop'
$range = if ($lastTag) { "$lastTag..HEAD" } else { 'HEAD' }

# One record per commit: hash, subject, body; separated by control characters.
$log = (Invoke-Git log $range --format='%h%x1f%s%x1f%b%x1e') -join "`n"
$commits = @()
foreach ($record in ($log -split [char]0x1e)) {
    $fields = $record.Trim() -split [char]0x1f
    if ($fields.Count -lt 2 -or -not $fields[0]) { continue }
    if ($fields[1] -notmatch '^(?<type>[a-z]+)(\([^)]*\))?(?<bang>!)?:\s*(?<text>.+)$') { continue }
    $body = if ($fields.Count -gt 2) { $fields[2] } else { '' }
    $commits += [pscustomobject]@{
        Hash     = $fields[0]
        Type     = $Matches.type
        Text     = $Matches.text
        Breaking = [bool]$Matches.bang -or ($body -match 'BREAKING CHANGE')
    }
}

$level = $Bump
if (-not $Version -and $level -eq 'auto') {
    if ($commits | Where-Object Breaking) { $level = if ($major -eq 0) { 'minor' } else { 'major' } }
    elseif ($commits | Where-Object Type -eq 'feat') { $level = 'minor' }
    elseif ($commits | Where-Object { $_.Type -in 'fix', 'perf', 'refactor' }) { $level = 'patch' }
    else { throw "Nothing to release: no feat, fix, perf or refactor commits since $(if ($lastTag) { $lastTag } else { 'the start' })." }
}
if (-not $Version) {
    switch ($level) {
        'major' { $Version = "$($major + 1).0.0" }
        'minor' { $Version = "$major.$($minor + 1).0" }
        'patch' { $Version = "$major.$minor.$($patch + 1)" }
    }
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "'$Version' is not MAJOR.MINOR.PATCH." }
$tag = "v$Version"
if (& git -C $Root tag --list $tag) { throw "Tag $tag exists already." }

# --- Changelog section --------------------------------------------------------------

$sections = [ordered]@{ feat = 'Features'; fix = 'Bug Fixes'; perf = 'Performance'; refactor = 'Code Refactoring' }
$date = Get-Date -Format 'yyyy-MM-dd'
$heading = if ($lastTag) { "## [$Version]($Repository/compare/$lastTag...$tag) ($date)" } else { "## $Version ($date)" }
$notes = New-Object System.Collections.Generic.List[string]
$breaking = @($commits | Where-Object Breaking)
if ($breaking.Count) {
    $notes.Add('### Breaking Changes'); $notes.Add('')
    foreach ($c in $breaking) { $notes.Add("* $($c.Text) ([$($c.Hash)]($Repository/commit/$($c.Hash)))") }
    $notes.Add('')
}
foreach ($type in $sections.Keys) {
    $matching = @($commits | Where-Object Type -eq $type)
    if (-not $matching.Count) { continue }
    $notes.Add("### $($sections[$type])"); $notes.Add('')
    foreach ($c in $matching) { $notes.Add("* $($c.Text) ([$($c.Hash)]($Repository/commit/$($c.Hash)))") }
    $notes.Add('')
}
$releaseNotes = ($notes -join "`n").TrimEnd()

Write-Host "Releasing $current -> $Version ($tag)" -ForegroundColor Cyan
Write-Host ''
Write-Host $heading
Write-Host ''
Write-Host $releaseNotes
Write-Host ''
if ($DryRun) { Write-Host 'Dry run: nothing changed.' -ForegroundColor Yellow; return }

# --- Commit and tag ---------------------------------------------------------------

$changelogPath = Join-Path $Root 'CHANGELOG.md'
$previous = if (Test-Path $changelogPath) { [System.IO.File]::ReadAllText($changelogPath) -replace '^# Changelog\s*', '' } else { '' }
Write-Utf8 $changelogPath ("# Changelog`n`n$heading`n`n$releaseNotes`n`n$previous".TrimEnd() + "`n")
Write-Utf8 (Join-Path $Root 'version.txt') "$Version`n"

Invoke-Git add version.txt CHANGELOG.md | Out-Null
Invoke-Git commit --quiet -m "chore(release): $Version" | Out-Null
Invoke-Git tag -a $tag -m "Monitone $Version" | Out-Null

# --- Build -------------------------------------------------------------------------

try {
    Invoke-Checked cmake @('-S', $Root, '-B', $BuildDir, '-A', 'x64', '-DMONITONE_WARNINGS_AS_ERRORS=ON')
    Invoke-Checked cmake @('--build', $BuildDir, '--config', 'Release', '--parallel')

    $bin = Join-Path $BuildDir 'bin\Release'
    foreach ($file in 'Monitone.exe', 'MonitoneSettings.dll') {
        $built = (Get-Item (Join-Path $bin $file)).VersionInfo.ProductVersion
        if ($built -ne $Version) { throw "$file reports version '$built', expected '$Version'." }
    }
}
catch {
    Write-Host "Build failed, removing commit and tag $tag." -ForegroundColor Red
    & git -C $Root tag -d $tag | Out-Null
    & git -C $Root reset --hard --quiet HEAD~1
    throw
}

# --- Package -------------------------------------------------------------------------

$dist = Join-Path $BuildDir 'dist'
$stage = Join-Path $dist "Monitone-$Version"
$zip = Join-Path $dist "Monitone-$Version-x64.zip"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item (Join-Path $bin 'Monitone.exe'), (Join-Path $bin 'MonitoneSettings.dll') $stage
Copy-Item (Join-Path $Root 'README.md'), (Join-Path $Root 'LICENSE'), $changelogPath $stage
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText("$zip.sha256", "$hash  $(Split-Path -Leaf $zip)`n", [System.Text.Encoding]::ASCII)

Write-Host ''
Write-Host "Built $zip" -ForegroundColor Green
Write-Host "SHA-256 $hash"

# --- Publish -----------------------------------------------------------------------

if (-not $Publish) {
    Write-Host ''
    Write-Host 'Not published. When you are happy with the build:' -ForegroundColor Yellow
    Write-Host "  git push origin main $tag"
    Write-Host "  then create the release for $tag on $Repository/releases/new and attach the zip and .sha256"
    return
}

Invoke-Git push origin main $tag | Out-Null
if (Get-Command gh -ErrorAction SilentlyContinue) {
    $notesFile = Join-Path $dist 'release-notes.md'
    Write-Utf8 $notesFile "$releaseNotes`n"
    Invoke-Checked gh @('release', 'create', $tag, $zip, "$zip.sha256", '--repo', $Repository,
        '--title', "Monitone $Version", '--notes-file', $notesFile)
    Write-Host "Published $Repository/releases/tag/$tag" -ForegroundColor Green
}
else {
    Write-Host "Pushed main and $tag. The GitHub CLI (gh) is not installed, so create the release by hand:" -ForegroundColor Yellow
    Write-Host "  $Repository/releases/new?tag=$tag  (attach the zip and .sha256)"
}
