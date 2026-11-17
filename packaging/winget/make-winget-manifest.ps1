# winget manifest generator for MemSweep
# Usage:  powershell -File make-winget-manifest.ps1 v1.9.18
# Output: packaging/winget/manifests/f/feiyu-labs/MemSweep/<version>/*.yaml
#
# Submitting to the community repo (one-time per version):
#   1. Fork https://github.com/microsoft/winget-pkgs
#   2. Copy the generated folder to manifests/f/feiyu-labs/MemSweep/<version>/
#      Keep the file names: <id>.yaml, <id>.installer.yaml, <id>.locale.en-US.yaml.
#   3. Validate before opening the PR:
#        winget validate --manifest manifests/f/feiyu-labs/MemSweep/<version>
#   4. PR title: "Add feiyu-labs.MemSweep version <version>".
#      The pipeline downloads each release asset and checks its hash.
# Notes:
#   - Schema 1.28.0 is current; the repo rejects 1.6 and older.
#   - Submit one version at a time and wait for the merge before the next one.

param([Parameter(Mandatory)][string]$Tag)   # e.g. v1.9.18

$ErrorActionPreference = 'Stop'
$repo = 'feiyu-labs/MemSweep'
$pkgId = 'feiyu-labs.MemSweep'
$version = $Tag.TrimStart('v')
$schema = '1.28.0'

# Only project asset fields: the release body carries multi-language text,
# and ConvertFrom-Json over the whole object is unreliable through the pipe.
$assets = gh api "repos/$repo/releases/tags/$Tag" --jq ".assets | map({name: .name, url: .browser_download_url})" | ConvertFrom-Json
$urlOf = { param($n) ($assets | Where-Object { $_.name -eq $n }).url }
$x64url = & $urlOf "MemSweep-$Tag-x64.zip"
$armurl = & $urlOf "MemSweep-$Tag-arm64.zip"
if (-not $x64url -or -not $armurl) { throw "release $Tag is missing the x64 or arm64 zip" }
function Get-Sha([string]$url) {
    $c = (Invoke-WebRequest $url).Content
    if ($c -is [byte[]]) { $c = [Text.Encoding]::ASCII.GetString($c) }
    return $c.Trim().Split(' ')[0].ToUpper()
}
$x64sha = Get-Sha (& $urlOf "MemSweep-$Tag-x64.zip.sha256")
$armsha = Get-Sha (& $urlOf "MemSweep-$Tag-arm64.zip.sha256")

$dir = Join-Path $PSScriptRoot "manifests/f/feiyu-labs/MemSweep/$version"
New-Item -ItemType Directory -Path $dir -Force | Out-Null

function Write-Manifest([string]$name, [string]$text) {
    $t = $text.Replace('__V__', $version)
    $t = $t.Replace('__X64URL__', $x64url).Replace('__X64SHA__', $x64sha)
    $t = $t.Replace('__ARMURL__', $armurl).Replace('__ARMSHA__', $armsha)
    $t = $t.Replace('__RELURL__', "https://github.com/$repo/releases/tag/$Tag")
    Set-Content -Path (Join-Path $dir $name) -Value $t -Encoding utf8NoBOM
    Write-Host "  -> $name"
}

$versionYaml = @'
# yaml-language-server: $schema=https://aka.ms/winget-manifest.version.__SCHEMA__.schema.json
PackageIdentifier: feiyu-labs.MemSweep
PackageVersion: __V__
DefaultLocale: en-US
ManifestType: version
ManifestVersion: __SCHEMA__
'@

$installerYaml = @'
# yaml-language-server: $schema=https://aka.ms/winget-manifest.installer.__SCHEMA__.schema.json
PackageIdentifier: feiyu-labs.MemSweep
PackageVersion: __V__
Installers:
  - Architecture: x64
    InstallerType: zip
    InstallerUrl: __X64URL__
    InstallerSha256: __X64SHA__
    NestedInstallerType: portable
    NestedInstallerFiles:
      - RelativeFilePath: MemSweep.exe
        PortableCommandAlias: memsweep
  - Architecture: arm64
    InstallerType: zip
    InstallerUrl: __ARMURL__
    InstallerSha256: __ARMSHA__
    NestedInstallerType: portable
    NestedInstallerFiles:
      - RelativeFilePath: MemSweep.exe
        PortableCommandAlias: memsweep
ManifestType: installer
ManifestVersion: __SCHEMA__
'@

$localeYaml = @'
# yaml-language-server: $schema=https://aka.ms/winget-manifest.defaultLocale.__SCHEMA__.schema.json
PackageIdentifier: feiyu-labs.MemSweep
PackageVersion: __V__
PackageLocale: en-US
Publisher: feiyu-labs
PublisherUrl: https://github.com/feiyu-labs
PublisherSupportUrl: https://github.com/feiyu-labs/MemSweep/issues
PackageName: MemSweep
PackageUrl: https://github.com/feiyu-labs/MemSweep
License: MIT
LicenseUrl: https://github.com/feiyu-labs/MemSweep/blob/main/LICENSE
ShortDescription: Reclaims memory from idle background processes without touching what you're using.
Description: |-
  MemSweep reclaims memory held by idle background processes while leaving running
  applications alone. It cleans when memory use crosses the threshold you set,
  and never flushes modified pages or sweeps every process. Works offline with
  native x64 and ARM64 builds and no runtime dependencies.
Moniker: memsweep
Tags:
  - memory
  - memory-cleaner
  - ram
  - windows
  - portable
ReleaseNotesUrl: __RELURL__
ManifestType: defaultLocale
ManifestVersion: __SCHEMA__
'@

Write-Manifest "$pkgId.yaml" ($versionYaml.Replace('__SCHEMA__', $schema))
Write-Manifest "$pkgId.installer.yaml" ($installerYaml.Replace('__SCHEMA__', $schema))
Write-Manifest "$pkgId.locale.en-US.yaml" ($localeYaml.Replace('__SCHEMA__', $schema))
Write-Host "manifests written to $dir"
Write-Host "validate with: winget validate --manifest `"$dir`""
