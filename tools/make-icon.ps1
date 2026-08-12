# Builds assets/icons/app.ico from assets/icons/logo.png.
# Run this after replacing the logo; the .ico is committed so the normal build
# and CI do not need an image toolchain.
#
# Frames are stored as PNG (allowed since Vista) to keep the file small and the
# alpha channel intact at every size.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root 'assets\icons\logo.png'
$dst  = Join-Path $root 'assets\icons\app.ico'

# 256 is the shell's large-icon size; the rest cover list/taskbar/titlebar.
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256

$source = [System.Drawing.Image]::FromFile($src)
try {
    $frames = foreach ($s in $sizes) {
        $bmp = New-Object System.Drawing.Bitmap $s, $s,
                   ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode  = 'HighQualityBicubic'
        $g.PixelOffsetMode    = 'HighQuality'
        $g.SmoothingMode      = 'HighQuality'
        $g.CompositingQuality = 'HighQuality'
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($source, (New-Object System.Drawing.Rectangle 0, 0, $s, $s))
        $g.Dispose()

        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        [pscustomobject]@{ Size = $s; Bytes = $ms.ToArray() }
        $ms.Dispose()
    }
} finally {
    $source.Dispose()
}

$out = [System.IO.File]::Create($dst)
$w   = New-Object System.IO.BinaryWriter $out
try {
    # ICONDIR: reserved, type 1 (icon), image count.
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$frames.Count)

    # Directory entries come first, so pixel data starts after all of them.
    $offset = 6 + (16 * $frames.Count)
    foreach ($f in $frames) {
        # 256 is encoded as 0 in a single byte.
        $dim = if ($f.Size -ge 256) { 0 } else { $f.Size }
        $w.Write([byte]$dim)          # width
        $w.Write([byte]$dim)          # height
        $w.Write([byte]0)             # palette entries (0 = truecolor)
        $w.Write([byte]0)             # reserved
        $w.Write([uint16]1)           # colour planes
        $w.Write([uint16]32)          # bits per pixel
        $w.Write([uint32]$f.Bytes.Length)
        $w.Write([uint32]$offset)
        $offset += $f.Bytes.Length
    }
    foreach ($f in $frames) { $w.Write($f.Bytes) }
} finally {
    $w.Dispose(); $out.Dispose()
}

Write-Host "Wrote $dst ($([math]::Round((Get-Item $dst).Length / 1KB, 1)) KB, $($frames.Count) sizes)"
