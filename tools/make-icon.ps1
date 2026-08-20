# Builds assets/icons/app.ico from assets/icons/icon.png.
# Run this after replacing the logo; the .ico is committed so the normal build
# and CI do not need an image toolchain.
#
# Frames are stored as PNG (allowed since Vista) to keep the file small and the
# alpha channel intact at every size.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root 'assets\icons\icon.png'
$dst  = Join-Path $root 'assets\icons\app.ico'

# 256 is the shell's large-icon size; the rest cover list/taskbar/titlebar.
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256

# A hand-tuned small icon beats anything downscaling can produce: at 16px the
# detail in the full-size art turns to mush. Drop a purpose-drawn PNG at one of
# these names and it is used verbatim for that size instead of being resampled.
$handTuned = @{}
foreach ($s in $sizes) {
    $candidate = Join-Path $root "assets\icons\icon-$s.png"
    if (Test-Path $candidate) { $handTuned[$s] = $candidate }
}

$original = [System.Drawing.Image]::FromFile($src)

# Source art often carries wide transparent margins, which make the 16px taskbar
# icon look shrunken. Crop to the visible pixels, then pad back to a square so
# the aspect ratio is preserved, with a small breathing margin.
$source = $null
try {
    $bmp = New-Object System.Drawing.Bitmap $original
    $minX = $bmp.Width; $minY = $bmp.Height; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        for ($x = 0; $x -lt $bmp.Width; $x++) {
            if ($bmp.GetPixel($x, $y).A -gt 12) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }

    if ($maxX -lt 0) { throw "$src is fully transparent." }

    $cw = $maxX - $minX + 1
    $ch = $maxY - $minY + 1

    # Scale the artwork to FILL the square rather than fitting it inside one.
    # The source is wider than it is tall (456x341 for the current icon), so
    # fitting would letterbox it and leave transparent bands top and bottom --
    # which is exactly what makes a taskbar icon look undersized.
    $side = 512
    $inner = [int]($side * 0.94)                       # small uniform margin
    $ratio = [Math]::Min($inner / $cw, $inner / $ch)
    $dw = [int]($cw * $ratio)
    $dh = [int]($ch * $ratio)

    $source = New-Object System.Drawing.Bitmap $side, $side,
                  ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($source)
    $g.InterpolationMode  = 'HighQualityBicubic'
    $g.PixelOffsetMode    = 'HighQuality'
    $g.CompositingQuality = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($bmp,
        (New-Object System.Drawing.Rectangle ([int](($side-$dw)/2)), ([int](($side-$dh)/2)), $dw, $dh),
        (New-Object System.Drawing.Rectangle $minX, $minY, $cw, $ch),
        [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
    $bmp.Dispose()
} finally {
    $original.Dispose()
}

try {
    $frames = foreach ($s in $sizes) {
        if ($handTuned.ContainsKey($s)) {
            $hand = New-Object System.Drawing.Bitmap $handTuned[$s]
            $bmp = New-Object System.Drawing.Bitmap $s, $s,
                       ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $hg = [System.Drawing.Graphics]::FromImage($bmp)
            $hg.Clear([System.Drawing.Color]::Transparent)
            $hg.DrawImage($hand, (New-Object System.Drawing.Rectangle 0, 0, $s, $s))
            $hg.Dispose(); $hand.Dispose()

            $hms = New-Object System.IO.MemoryStream
            $bmp.Save($hms, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Dispose()
            Write-Host "  ${s}px: using hand-tuned icon-$s.png"
            [pscustomobject]@{ Size = $s; Bytes = $hms.ToArray() }
            $hms.Dispose()
            continue
        }

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
