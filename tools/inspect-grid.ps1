# Scratch tool (not part of the build): crops a region of a source PNG, scales it
# up with nearest-neighbor, and overlays a 16px grid with col,row labels so tile
# cells can be picked precisely by eye. Not referenced by bake.ps1 or build.ps1.
param(
    [Parameter(Mandatory=$true)][string]$Src,
    [int]$X0 = 0, [int]$Y0 = 0, [int]$Cols = 8, [int]$Rows = 8,
    [int]$Scale = 6,
    [Parameter(Mandatory=$true)][string]$Out
)
Add-Type -AssemblyName System.Drawing
$TILE = 16
$srcImg = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Src))
$w = $Cols * $TILE
$h = $Rows * $TILE
$crop = New-Object System.Drawing.Bitmap $w, $h
$g0 = [System.Drawing.Graphics]::FromImage($crop)
$g0.DrawImage($srcImg, (New-Object System.Drawing.Rectangle 0,0,$w,$h), $X0, $Y0, $w, $h, [System.Drawing.GraphicsUnit]::Pixel)
$g0.Dispose()
$srcImg.Dispose()

$outW = $w * $Scale
$outH = $h * $Scale
$big = New-Object System.Drawing.Bitmap ($outW + 40), ($outH + 24)
$g = [System.Drawing.Graphics]::FromImage($big)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.Clear([System.Drawing.Color]::FromArgb(255,255,0,255))
$g.DrawImage($crop, (New-Object System.Drawing.Rectangle 40,24,$outW,$outH))

$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(180,255,0,255)), 1
$font = New-Object System.Drawing.Font "Consolas", 9
$brush = [System.Drawing.Brushes]::Yellow
for ($c = 0; $c -le $Cols; $c++) {
    $x = 40 + $c * $TILE * $Scale
    $g.DrawLine($pen, $x, 24, $x, 24 + $outH)
}
for ($r = 0; $r -le $Rows; $r++) {
    $y = 24 + $r * $TILE * $Scale
    $g.DrawLine($pen, 40, $y, 40 + $outW, $y)
}
for ($c = 0; $c -lt $Cols; $c++) {
    $g.DrawString(("{0}" -f ($X0/$TILE + $c)), $font, $brush, 40 + $c*$TILE*$Scale + 2, 4)
}
for ($r = 0; $r -lt $Rows; $r++) {
    $g.DrawString(("{0}" -f ($Y0/$TILE + $r)), $font, $brush, 2, 24 + $r*$TILE*$Scale + 2)
}
$g.Dispose()
$big.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$crop.Dispose()
$big.Dispose()
Write-Host "wrote $Out ($Cols x $Rows cells from $X0,$Y0)"
