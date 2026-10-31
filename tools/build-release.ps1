<#
.SYNOPSIS
    Builds a Monitone release candidate for testing. Changes nothing in git.

.DESCRIPTION
    1. Checks that the working tree is clean and on branch main, and that this
       is the master checkout or a plain clone, not a work folder.
    2. Works out the next version from the Conventional Commits since the last
       release tag (feat = minor, fix/perf/refactor = patch, breaking = major,
       or minor before 1.0), unless -Version or -Bump says otherwise. The first
       release uses version.txt as it is.
    3. Writes the release notes. docs\release-notes\X.Y.Z.md, if present, is
       put in front of the list of changes; the first release uses only that
       file.
    4. Starts clean: deletes and recreates -WorkDir (default
       %LOCALAPPDATA%\Monitone-release, outside the checkout), exports HEAD
       into it (git archive: no untracked or ignored files) and builds that
       with CMake and MSVC (Release, warnings as errors) as exactly version
       X.Y.Z, then checks the version of the binaries. Nothing is built
       inside the checkout.
    5. Packages into <WorkDir>\dist:
         Monitone-X.Y.Z-x64.zip          portable
         Monitone-X.Y.Z-x64-setup.exe    NSIS setup
         Monitone-X.Y.Z-x64.msi          MSI package
         SHA256SUMS.txt
         release-notes.md                edit it before publishing if you like
         release.json                    read by tools\publish-release.ps1

    Test the packages, then publish them with tools\publish-release.ps1. If a
    test fails, fix it, commit, and run this script again.

    Needs Visual Studio 2022 (C++ and CMake tools), NSIS 3 (makensis) and the
    WiX Toolset 5 .NET tool with the Util extension:
        winget install NSIS.NSIS
        dotnet tool install --global wix --version 5.0.2
        wix extension add --global WixToolset.Util.wixext/5.0.2

.EXAMPLE
    tools\build-release.ps1 -DryRun
    Shows the next version and its release notes without building.

.EXAMPLE
    tools\build-release.ps1 -Version 1.0.0
    Builds an explicit version.
#>
[CmdletBinding()]
param(
    [string]$Version,
    [ValidateSet('auto', 'major', 'minor', 'patch')]
    [string]$Bump = 'auto',
    [switch]$DryRun,
    [string]$WorkDir = (Join-Path $env:LOCALAPPDATA 'Monitone-release')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Repository = 'https://github.com/Nusi42/Monitone'
$Root = Split-Path -Parent $PSScriptRoot
$WorkDir = [System.IO.Path]::GetFullPath($WorkDir)
$Source = Join-Path $WorkDir 'source'
$BuildDir = Join-Path $WorkDir 'build'
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
# or 'plain'. The same logic is in tools\publish-release.ps1.
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

# Enters the Visual Studio developer environment unless cmake is already on PATH.
function Enter-DeveloperShell {
    if (Get-Command cmake -ErrorAction SilentlyContinue) { return }
    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
    $vswhere = Join-Path $installer 'vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2022 is not installed.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio 2022 with the C++ workload is not installed.' }
    $env:Path = "$installer;$env:Path"
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -no_logo' | Out-Null
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        throw 'cmake was not found. Install "C++ CMake tools for Windows" with the Visual Studio Installer.'
    }
}

function Find-Tool([string]$Name, [string[]]$Candidates, [string]$Hint) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in $Candidates) { if (Test-Path $candidate) { return $candidate } }
    throw "$Name was not found. $Hint"
}

# --- Preconditions -------------------------------------------------------------

if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'git is not installed.' }
if ((Get-CheckoutRole) -eq 'work') { throw 'Releases are built in the master checkout, not in a work folder.' }
$branch = (Invoke-Git rev-parse --abbrev-ref HEAD) | Select-Object -First 1
if ($branch -ne 'main') { throw "Releases are made from main, not '$branch'." }
if (Invoke-Git status --porcelain) { throw 'The working tree has uncommitted changes.' }
if ("$WorkDir\".StartsWith("$Root\", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "-WorkDir must be outside the checkout ($Root)."
}

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

$commits = @()
if ($lastTag) {
    # One record per commit: hash, subject, body; separated by control characters.
    $log = (Invoke-Git log "$lastTag..HEAD" --format='%h%x1f%s%x1f%b%x1e') -join "`n"
    foreach ($record in ($log -split [char]0x1e)) {
        $fields = $record.Trim() -split [char]0x1f
        if ($fields.Count -lt 2 -or -not $fields[0]) { continue }
        if ($fields[1] -notmatch '^(?<type>[a-z]+)(\([^)]*\))?(?<bang>!)?:\s*(?<text>.+)$') { continue }
        $body = if ($fields.Count -gt 2) { $fields[2] } else { '' }
        $commits += [pscustomobject]@{
            Hash     = $fields[0]
            Type     = $Matches.type
            Text     = $Matches.text
            Breaking = $Matches.ContainsKey('bang') -or ($body -match 'BREAKING CHANGE')
        }
    }
}

if (-not $Version) {
    $level = $Bump
    if (-not $lastTag -and $level -eq 'auto') {
        $Version = $current
    }
    else {
        if ($level -eq 'auto') {
            if ($commits | Where-Object Breaking) { $level = if ($major -eq 0) { 'minor' } else { 'major' } }
            elseif ($commits | Where-Object Type -eq 'feat') { $level = 'minor' }
            elseif ($commits | Where-Object { $_.Type -in 'fix', 'perf', 'refactor' }) { $level = 'patch' }
            else { throw "Nothing to release: no feat, fix, perf or refactor commits since $lastTag." }
        }
        switch ($level) {
            'major' { $Version = "$($major + 1).0.0" }
            'minor' { $Version = "$major.$($minor + 1).0" }
            'patch' { $Version = "$major.$minor.$($patch + 1)" }
        }
    }
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "'$Version' is not MAJOR.MINOR.PATCH." }
$tag = "v$Version"
if (& git -C $Root tag --list $tag) { throw "Tag $tag exists already." }

# --- Release notes ------------------------------------------------------------------

$notes = New-Object System.Collections.Generic.List[string]
$introPath = Join-Path $Root "docs\release-notes\$Version.md"
if (Test-Path $introPath) {
    $notes.Add([System.IO.File]::ReadAllText($introPath).Trim()); $notes.Add('')
}
elseif (-not $lastTag) {
    throw "The first release needs docs\release-notes\$Version.md describing Monitone."
}

$sections = [ordered]@{ feat = 'Features'; fix = 'Bug Fixes'; perf = 'Performance'; refactor = 'Code Refactoring' }
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

Write-Host "Release candidate $Version ($tag), previous release: $(if ($lastTag) { $lastTag } else { 'none' })" -ForegroundColor Cyan
Write-Host ''
Write-Host $releaseNotes
Write-Host ''
if ($DryRun) { Write-Host 'Dry run: nothing built.' -ForegroundColor Yellow; return }

# --- Build -------------------------------------------------------------------------

Enter-DeveloperShell
$makensis = Find-Tool 'makensis' @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe") `
    'Install NSIS 3: winget install NSIS.NSIS'
$wix = Find-Tool 'wix' @("$env:USERPROFILE\.dotnet\tools\wix.exe") `
    'Install the WiX Toolset: dotnet tool install --global wix --version 5.0.2'

# A clean start: the work folder is made anew, the sources are an export of HEAD.
$marker = Join-Path $WorkDir '.monitone-release'
if (Test-Path $WorkDir) {
    if (-not (Test-Path $marker)) { throw "$WorkDir exists but was not made by this script. Remove it or pass -WorkDir." }
    Remove-Item $WorkDir -Recurse -Force
}
New-Item -ItemType Directory -Path $WorkDir | Out-Null
Write-Utf8 $marker "Made by tools\build-release.ps1; its next run deletes this folder.`n"
$archive = Join-Path $WorkDir 'source.zip'
Invoke-Git archive --format=zip -o $archive HEAD | Out-Null
Expand-Archive -Path $archive -DestinationPath $Source
Remove-Item $archive

Invoke-Checked cmake @('-S', $Source, '-B', $BuildDir, '-A', 'x64', '-DMONITONE_WARNINGS_AS_ERRORS=ON',
    "-DMONITONE_RELEASE_VERSION=$Version")
Invoke-Checked cmake @('--build', $BuildDir, '--config', 'Release', '--parallel')

$bin = Join-Path $BuildDir 'bin\Release'
foreach ($file in 'Monitone.exe', 'MonitoneSettings.dll') {
    $built = (Get-Item (Join-Path $bin $file)).VersionInfo.ProductVersion
    if ($built -ne $Version) { throw "$file reports version '$built', expected '$Version'." }
}

# --- Package -------------------------------------------------------------------------

New-Item -ItemType Directory -Path $Dist -Force | Out-Null
$base = "Monitone-$Version-x64"

$stage = Join-Path $BuildDir "stage\$base"
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item (Join-Path $bin 'Monitone.exe'), (Join-Path $bin 'MonitoneSettings.dll'), (Join-Path $Source 'README.md') $stage
Copy-Item (Join-Path $Source 'LICENSE') (Join-Path $stage 'LICENSE.txt')
$zip = Join-Path $Dist "$base.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

$setup = Join-Path $Dist "$base-setup.exe"
Invoke-Checked $makensis @('/V2', "/DVERSION=$Version", "/DBINDIR=$bin", "/DOUTFILE=$setup",
    (Join-Path $Source 'installer\Monitone.nsi'))

$msi = Join-Path $Dist "$base.msi"
Invoke-Checked $wix @('build', (Join-Path $Source 'installer\Monitone.wxs'), '-arch', 'x64',
    '-ext', 'WixToolset.Util.wixext', '-d', "Version=$Version", '-d', "BinDir=$bin", '-o', $msi)
Get-ChildItem $Dist -Filter '*.wixpdb' | Remove-Item -Force

$assets = @($zip, $setup, $msi)
$sums = foreach ($asset in $assets) {
    "$((Get-FileHash $asset -Algorithm SHA256).Hash.ToLowerInvariant())  $(Split-Path -Leaf $asset)"
}
[System.IO.File]::WriteAllText((Join-Path $Dist 'SHA256SUMS.txt'), (($sums -join "`n") + "`n"), [System.Text.Encoding]::ASCII)
Write-Utf8 (Join-Path $Dist 'release-notes.md') "$releaseNotes`n"
$manifest = [ordered]@{
    version     = $Version
    tag         = $tag
    previousTag = $lastTag
    commit      = (Invoke-Git rev-parse HEAD) | Select-Object -First 1
    assets      = @($assets | ForEach-Object { Split-Path -Leaf $_ }) + 'SHA256SUMS.txt'
}
Write-Utf8 (Join-Path $Dist 'release.json') (($manifest | ConvertTo-Json) + "`n")

Write-Host ''
Write-Host "Release candidate $Version is ready in $Dist" -ForegroundColor Green
$sums | ForEach-Object { Write-Host "  $_" }
Write-Host ''
Write-Host 'Test the zip, the setup and the MSI. Edit release-notes.md there if you like. Then:'
if ($PSBoundParameters.ContainsKey('WorkDir')) { Write-Host "  tools\publish-release.ps1 -WorkDir `"$WorkDir`"" }
else { Write-Host '  tools\publish-release.ps1' }
