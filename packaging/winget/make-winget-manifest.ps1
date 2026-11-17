# winget manifest generator for MemSweep
# Usage:  powershell -File make-winget-manifest.ps1 v1.9.17
# Output: packaging/winget/manifests/f/feiyu-labs/MemSweep/<version>/*.yaml
#
# Submitting to the community repo (one-time per version):
#   1. Fork https://github.com/microsoft/winget-pulls
#   2. Copy the generated folder into manifests/f/feiyu-labs/MemSweep/<version>/
#   3. Open a PR titled "Add MemSweep <version>". The bot validates the
#      installer hash against the release asset automatically.
# Note: the validator prefers a stable release cadence - avoid submitting
#       multiple versions within a couple of days.

param([Parameter(Mandatory)][string]$Tag)   # e.g. v1.9.17

$ErrorActionPreference = 'Stop'
$repo = 'feiyu-labs/MemSweep'
$pkgId = 'feiyu-labs.MemSweep'
$version = $Tag.TrimStart('v')

# 只取资产字段：release body 含多语言文本，整包 ConvertFrom-Json 在管道编码上不可靠
$assets = gh api "repos/$repo/releases/tags/$Tag" --jq ".assets | map({name: .name, url: .browser_download_url})" | ConvertFrom-Json
$urlOf = { param($n) ($assets | Where-Object { $_.name -eq $n }).url }
$x64url = & $urlOf "MemSweep-$Tag-x64.zip"
$armurl = & $urlOf "MemSweep-$Tag-arm64.zip"
function Get-Sha([string]$url) {
    $c = (Invoke-WebRequest $url).Content
    if ($c -is [byte[]]) { $c = [Text.Encoding]::ASCII.GetString($c) }
    return $c.Trim().Split(' ')[0].ToLower()
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
# yaml-language-server: $schema=https://aka.ms/winget-manifest.version.1.6.0.schema.json
PackageIdentifier: feiyu-labs.MemSweep
PackageVersion: __V__
DefaultLocale: en-US
'@

$installerYaml = @'
# yaml-language-server: $schema=https://aka.ms/winget-manifest.installer.1.6.0.schema.json
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
ManifestVersion: 1.6.0
'@

$localeYaml = @'
# yaml-language-server: $schema=https://aka.ms/winget-manifest.defaultlocale.1.6.0.schema.json
PackageIdentifier: feiyu-labs.MemSweep
PackageVersion: __V__
PackageLocale: en-US
Publisher: feiyu-labs
PublisherUrl: https://github.com/feiyu-labs
PackageName: MemSweep
PackageUrl: https://github.com/feiyu-labs/MemSweep
License: MIT
LicenseUrl: https://github.com/feiyu-labs/MemSweep/blob/main/LICENSE
ShortDescription: Reclaims memory from idle background processes, without touching what you are using.
Description: |-
  A Windows memory cleaner that reclaims memory held by idle background processes,
  leaving running applications alone. Triggered by real memory pressure instead of a
  percentage, immune to foreground activity, and fully offline. 403 KB single binary,
  native x64 and ARM64, no runtime dependencies.
Tags:
  - memory
  - memory-cleaner
  - ram
  - windows
  - portable
ReleaseNotesUrl: __RELURL__
ManifestType: defaultLocale
ManifestVersion: 1.6.0
'@

Write-Manifest "$pkgId.yaml" $versionYaml
Write-Manifest "$pkgId.installer.yaml" $installerYaml
Write-Manifest "$pkgId.locale.yaml" $localeYaml
Write-Host "manifests written to $dir"
