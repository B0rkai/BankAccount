<#
.SYNOPSIS
    Cuts a BankAccount release: bumps include\Version.h, moves docs\changelog.json's
    "unreleased" entries into a new "released" entry, builds Release|x64, computes the built
    exe's CRC32, and publishes BankAccount.exe + release.json + changelog.json to a network
    release folder - the counterpart to cMain::CheckForUpdate()/SelfUpdater::ApplyUpdate()/
    cMain::ShowChangelogIfJustUpdated() on the client side (see CLAUDE.md's "deploy releases
    through this network location" and "Changelog" notes).

.DESCRIPTION
    This is the manual, human-run release process for a project with no CI/CD - run it once
    per release from a normal PowerShell prompt (not from inside a Claude Code session).
    It does NOT touch git (no commit/tag) - review and commit the Version.h and
    docs\changelog.json changes yourself after a successful run, the same way you would
    review any other code change.

.PARAMETER Version
    The new version string, e.g. "1.1.0" - must be exactly major.minor.patch (matching
    ParseVersion() in include/Version.cpp), or the client's update check will silently
    never recognize it as parseable.

.PARAMETER ReleaseFolder
    The network folder to publish into (e.g. \\server\bankaccount\release) - matches
    DbLocationSettings::release_folder on the client (db\location.json's "path" + "\release" by
    default, or its "release_path" override). Required - no default, so this can never run
    against an unintended target by muscle memory.

.PARAMETER Configuration
    Build configuration to publish. Defaults to Release.

.PARAMETER SkipBuild
    Skip the MSBuild step and just re-package/re-publish whatever is already at
    x64\<Configuration>\BankAccount.exe (e.g. if you already built it separately).

.EXAMPLE
    .\scripts\PackageRelease.ps1 -Version 1.1.0 -ReleaseFolder \\myserver\bankaccount\release
#>
param(
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$ReleaseFolder,
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$versionHeaderPath = Join-Path $repoRoot "include\Version.h"
$exePath = Join-Path $repoRoot "x64\$Configuration\BankAccount.exe"

# 1. Validate the version string - same shape ParseVersion() (include/Version.cpp) accepts,
# so a typo here can't silently publish a manifest the client will never recognize as newer.
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version '$Version' is not major.minor.patch (e.g. 1.1.0) - ParseVersion() on the client requires exactly that shape."
}

# 2. Validate docs\changelog.json has release notes ready to go, and hasn't already published
# this version - refuses to proceed rather than silently overwriting existing release notes or
# publishing a release with nothing to say. See CLAUDE.md's "Changelog" section: an "unreleased"
# entry is expected to be appended on every commit, so by release time this should never be empty.
$changelogPath = Join-Path $repoRoot "docs\changelog.json"
# Explicit UTF8 read: Get-Content's default encoding for a BOM-less file is the system ANSI
# codepage, not UTF-8, which silently mangles any non-ASCII characters (e.g. turns "->" into
# mojibake) before ConvertFrom-Json ever sees them.
$changelog = [System.IO.File]::ReadAllText($changelogPath, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
if (@($changelog.released | Where-Object { $_.version -eq $Version })) {
    throw "Version '$Version' is already in docs\changelog.json's 'released' list - bump to a new version, or fix the existing entry by hand if this release hasn't actually shipped yet."
}
$unreleasedChanges = @($changelog.unreleased | Where-Object { $_ -ne $null })
if ($unreleasedChanges.Count -eq 0) {
    throw "docs\changelog.json has no 'unreleased' entries - add release notes before cutting a release."
}

# 3. Bump include\Version.h
Write-Host "Bumping APP_VERSION to $Version in $versionHeaderPath"
$headerContent = Get-Content $versionHeaderPath -Raw
$pattern = 'constexpr const char\* APP_VERSION = "[^"]*";'
if ($headerContent -notmatch $pattern) {
    throw ("Could not find the expected constexpr const char* APP_VERSION = ""...""; line in " + $versionHeaderPath + " - has its shape changed? Update this script's `$pattern variable to match.")
}
$newHeaderContent = $headerContent -replace $pattern, "constexpr const char* APP_VERSION = `"$Version`";"
Set-Content -Path $versionHeaderPath -Value $newHeaderContent -NoNewline -Encoding utf8

# 4. Build Release|x64 (unless skipped)
if (-not $SkipBuild) {
    $msbuild = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
        -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
    if (-not $msbuild) {
        throw "Could not locate MSBuild via vswhere - is Visual Studio installed?"
    }
    Write-Host "Building $Configuration|x64 via $msbuild"
    & $msbuild (Join-Path $repoRoot "BankAccount.vcxproj") "/p:Configuration=$Configuration" "/p:Platform=x64" /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed (exit code $LASTEXITCODE) - Version.h has already been bumped to $Version on disk; fix the build and re-run, or revert the header manually if you're abandoning this release."
    }
} else {
    Write-Host "Skipping build (-SkipBuild) - using whatever is already at $exePath"
}

if (-not (Test-Path $exePath)) {
    throw "$exePath does not exist - build it first (omit -SkipBuild), or check -Configuration."
}

# 5. Compute the built exe's CRC32 - same algorithm as Crc32Update/Crc32Finish in
# include/Crc32.h (reflected IEEE 802.3 / zlib-compatible: poly 0xEDB88320, init/final XOR
# 0xFFFFFFFF), reimplemented here in C# since this script has no access to the C++ code.
Add-Type @"
public static class ReleaseCrc32 {
    public static uint Compute(byte[] bytes) {
        uint[] table = new uint[256];
        for (uint i = 0; i < 256; i++) {
            uint c = i;
            for (int k = 0; k < 8; k++) {
                c = ((c & 1) != 0) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table[i] = c;
        }
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in bytes) {
            crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }
}
"@
$exeBytes = [System.IO.File]::ReadAllBytes($exePath)
$crc32Hex = [ReleaseCrc32]::Compute($exeBytes).ToString("X8")
Write-Host "Computed CRC32: $crc32Hex"

# 6. Move docs\changelog.json's "unreleased" entries into a new "released" entry for
# $Version, clear "unreleased", and write the result back to the repo file - the same "bump by
# hand, review and commit afterwards" treatment as Version.h above.
$newChangelogEntry = [ordered]@{
    version = $Version
    date    = (Get-Date -Format "yyyy-MM-dd")
    changes = $unreleasedChanges
}
$changelog.released = @($changelog.released) + $newChangelogEntry
$changelog.unreleased = @()
# Explicit UTF8-no-BOM write (matching the read above): Out-File -Encoding utf8 writes a BOM,
# which the repo's other JSON files don't carry.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($changelogPath, ($changelog | ConvertTo-Json -Depth 5), $utf8NoBom)

# 7. Publish: exe + manifests into $ReleaseFolder
if (-not (Test-Path $ReleaseFolder)) {
    Write-Host "Creating $ReleaseFolder"
    New-Item -ItemType Directory -Path $ReleaseFolder -Force | Out-Null
}
Copy-Item -Path $exePath -Destination (Join-Path $ReleaseFolder "BankAccount.exe") -Force
$manifestPath = Join-Path $ReleaseFolder "release.json"
[ordered]@{ version = $Version; crc32 = $crc32Hex } | ConvertTo-Json | Out-File -FilePath $manifestPath -Encoding ascii -NoNewline
Copy-Item -Path $changelogPath -Destination (Join-Path $ReleaseFolder "changelog.json") -Force

Write-Host ""
Write-Host "Published version $Version to $ReleaseFolder"
Write-Host "  $exePath -> $ReleaseFolder\BankAccount.exe"
Write-Host "  $manifestPath (version=$Version, crc32=$crc32Hex)"
Write-Host "  $changelogPath -> $ReleaseFolder\changelog.json ($($unreleasedChanges.Count) change note(s) for $Version)"
Write-Host ""
Write-Host "include\Version.h and docs\changelog.json now reflect $Version - review and commit those changes."
