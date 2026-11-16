# Generate full set of high-resolution 150% DPI official screenshots for MemSweep
$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot = Split-Path -Parent $scriptDir
$exe = Join-Path $repoRoot 'dist\MemSweepDev.exe'
$outDir = Join-Path $repoRoot 'docs'
$tempPng = Join-Path $env:APPDATA 'MemSweep\ui.png'

if (!(Test-Path $exe)) {
    Write-Error "MemSweepDev.exe not found at $exe. Please run build.cmd first."
}

function RenderSnap($sheet, $theme, $lang, $scale, $targetName) {
    if ([string]::IsNullOrEmpty($sheet)) {
        Remove-Item Env:MEMSweep_SHEET -ErrorAction SilentlyContinue
    } else {
        $env:MEMSweep_SHEET = $sheet
    }
    $env:MEMSweep_THEME = $theme
    $env:MEMSweep_LANG = $lang
    $env:MEMSweep_DPISCALE = [string]$scale

    Remove-Item $tempPng -Force -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $exe -ArgumentList '/uipng' -WindowStyle Hidden -Wait -PassThru
    if ($p.ExitCode -ne 0 -or !(Test-Path $tempPng)) {
        Write-Warning "Failed rendering $targetName (ExitCode: $($p.ExitCode))"
        return
    }
    $dest = Join-Path $outDir $targetName
    Copy-Item $tempPng $dest -Force
    Write-Host "  -> Generated $targetName"
}

Write-Host "Rendering official MemSweep screenshots (150% DPI)..." -ForegroundColor Cyan

# 1. Main Home Screen (Bilingual x Dual-Theme)
RenderSnap "" "light" "zh" 1.5 "screenshot-home-light-zh.png"
RenderSnap "" "dark"  "zh" 1.5 "screenshot-home-dark-zh.png"
RenderSnap "" "light" "en" 1.5 "screenshot-home-light-en.png"
RenderSnap "" "dark"  "en" 1.5 "screenshot-home-dark-en.png"

# Legacy compatibility aliases
Copy-Item (Join-Path $outDir "screenshot-home-light-zh.png") (Join-Path $outDir "screenshot-light.png") -Force
Copy-Item (Join-Path $outDir "screenshot-home-dark-zh.png")  (Join-Path $outDir "screenshot-dark.png") -Force

# 2. Sheets: Chinese
RenderSnap "settings"  "light" "zh" 1.5 "screenshot-settings-zh.png"
RenderSnap "processes" "light" "zh" 1.5 "screenshot-processes-zh.png"
RenderSnap "system"    "light" "zh" 1.5 "screenshot-system-zh.png"
RenderSnap "advanced"  "light" "zh" 1.5 "screenshot-advanced-zh.png"
RenderSnap "about"     "light" "zh" 1.5 "screenshot-about-zh.png"

Copy-Item (Join-Path $outDir "screenshot-settings-zh.png")  (Join-Path $outDir "screenshot-settings.png") -Force
Copy-Item (Join-Path $outDir "screenshot-processes-zh.png") (Join-Path $outDir "screenshot-processes.png") -Force
Copy-Item (Join-Path $outDir "screenshot-system-zh.png")    (Join-Path $outDir "screenshot-system-check.png") -Force
Copy-Item (Join-Path $outDir "screenshot-advanced-zh.png")  (Join-Path $outDir "screenshot-advanced.png") -Force
Copy-Item (Join-Path $outDir "screenshot-about-zh.png")     (Join-Path $outDir "screenshot-about.png") -Force

# 3. Sheets: English
RenderSnap "settings"  "light" "en" 1.5 "screenshot-settings-en.png"
RenderSnap "processes" "light" "en" 1.5 "screenshot-processes-en.png"
RenderSnap "system"    "light" "en" 1.5 "screenshot-system-en.png"
RenderSnap "advanced"  "light" "en" 1.5 "screenshot-advanced-en.png"
RenderSnap "about"     "light" "en" 1.5 "screenshot-about-en.png"

Write-Host "All screenshots generated successfully into $outDir!" -ForegroundColor Green
