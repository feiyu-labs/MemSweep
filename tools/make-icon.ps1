# Renders the MemSweep mark at 6 key sizes and packs res/icon.ico (PNG-compressed entries).
# Keep the 256-grid geometry identical to DrawBrandMark() in src/ui.cpp.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$outIco = Join-Path $PSScriptRoot '..\res\icon.ico'
# 16/20/24/32 for shell + 48 for Explorer + 256 for large views; Windows scales the rest.
$sizes = @(16, 20, 24, 32, 48, 256)
$pngs = @()
$preview = [System.Drawing.Bitmap]::new(1480, 330)
$pg = [System.Drawing.Graphics]::FromImage($preview)
$pg.Clear([System.Drawing.Color]::FromArgb(245, 246, 248))
$pg.InterpolationMode = 'HighQualityBicubic'
$x = 10
foreach ($s in $sizes) {
  $bmp = [System.Drawing.Bitmap]::new($s, $s)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'
  $g.PixelOffsetMode = 'HighQuality'
  $g.Clear([System.Drawing.Color]::Transparent)
  $k = $s / 256.0
  $g.ScaleTransform($k, $k)
  $sq = [System.Drawing.Drawing2D.GraphicsPath]::new()
  $r = [System.Drawing.RectangleF]::new(4, 4, 248, 248)
  $d = 112.0
  $sq.AddArc($r.X, $r.Y, $d, $d, 180, 90)
  $sq.AddArc($r.Right - $d, $r.Y, $d, $d, 270, 90)
  $sq.AddArc($r.Right - $d, $r.Bottom - $d, $d, $d, 0, 90)
  $sq.AddArc($r.X, $r.Bottom - $d, $d, $d, 90, 90)
  $sq.CloseFigure()
  $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
      [System.Drawing.PointF]::new(4, 4), [System.Drawing.PointF]::new(252, 252),
      [System.Drawing.Color]::FromArgb(255, 52, 211, 153), [System.Drawing.Color]::FromArgb(255, 4, 120, 87))
  $g.FillPath($br, $sq)
  if ($s -ge 32) {
    $hl = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(28, 255, 255, 255), 1.5)
    $g.DrawPath($hl, $sq)
  }
  $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, 36.0)
  $pen.LineJoin = 'Round'; $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
  $g.DrawLines($pen, [System.Drawing.PointF[]]@(
    [System.Drawing.PointF]::new(60, 194),
    [System.Drawing.PointF]::new(60, 72),
    [System.Drawing.PointF]::new(128, 146),
    [System.Drawing.PointF]::new(196, 72),
    [System.Drawing.PointF]::new(196, 194)))
  $g.Dispose()
  $ms = [System.IO.MemoryStream]::new()
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $pngs += ,($ms.ToArray())
  $ms.Dispose()
  $pg.DrawImage($bmp, $x, 20, $s, $s)
  $pg.DrawImage($bmp, $x, 300 - $s, $s, $s)
  $x += 280
  $bmp.Dispose()
}
$pg.Dispose()
$previewPath = Join-Path $env:TEMP 'memsweep-icon-sheet.png'
$preview.Save($previewPath, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host "preview: $previewPath"
$preview.Dispose()

# pack ICO (PNG-compressed entries; Vista+)
$fs = [System.IO.File]::Create($outIco)
$bw = [System.IO.BinaryWriter]::new($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
  $s = $sizes[$i]
  $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
  $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
  $bw.Write([byte]0); $bw.Write([byte]0)
  $bw.Write([UInt16]1); $bw.Write([UInt16]32)
  $bw.Write([UInt32]$pngs[$i].Length)
  $bw.Write([UInt32]$offset)
  $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Close(); $fs.Close()
Write-Host ("icon written: " + (Get-Item $outIco).Length + " bytes, " + $sizes.Count + " sizes")
