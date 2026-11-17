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

# 只生成 README 实际引用的文件：尺寸与语言一一对应，仓库里不留没人用的图。

try {
    # 1. Home screen: bilingual x dual theme
    RenderSnap "" "light" "zh" 1.5 "screenshot-home-light-zh.png"
    RenderSnap "" "dark"  "zh" 1.5 "screenshot-home-dark-zh.png"
    RenderSnap "" "light" "en" 1.5 "screenshot-home-light-en.png"
    RenderSnap "" "dark"  "en" 1.5 "screenshot-home-dark-en.png"

    # 2. Sheets: Chinese
    RenderSnap "settings"  "light" "zh" 1.5 "screenshot-settings-zh.png"
    RenderSnap "processes" "light" "zh" 1.5 "screenshot-processes-zh.png"

    # 3. Sheets: English
    RenderSnap "settings"  "light" "en" 1.5 "screenshot-settings-en.png"
    RenderSnap "processes" "light" "en" 1.5 "screenshot-processes-en.png"

    # 系统状态页需要管理员权限才读得到真实状态；非提权渲染会显示
    # “Run as administrator to see system status”，不能作为文档配图。
    # 在提权 PowerShell 里设置同样四个环境变量后运行 /uipng，再把 ui.png 拷到 docs：
    #   $env:MEMSweep_SHEET='system'; $env:MEMSweep_THEME='light'
    #   $env:MEMSweep_LANG='en'; $env:MEMSweep_DPISCALE='1.5'
    #   Start-Process .\dist\MemSweepDev.exe -ArgumentList '/uipng' -WindowStyle Hidden -Wait
    #   中英文各一次，分别覆盖 screenshot-system-en.png / screenshot-system-zh.png

    # 4. Hotkey recording state (advanced sheet)
    $env:MEMSweep_CAPTURE = '1'
    RenderSnap "advanced" "light" "zh" 1.5 "screenshot-hotkey-zh.png"
    RenderSnap "advanced" "light" "en" 1.5 "screenshot-hotkey-en.png"

    Write-Host "All screenshots generated successfully into $outDir!" -ForegroundColor Green
} finally {
    Remove-Item Env:MEMSweep_SHEET, Env:MEMSweep_THEME, Env:MEMSweep_LANG, Env:MEMSweep_DPISCALE, Env:MEMSweep_CAPTURE -ErrorAction SilentlyContinue
}
