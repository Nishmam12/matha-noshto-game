# Wayfarer (top-down) art bake: assets\**.png -> src\art_data.h
#
#   powershell -File tools\bake.ps1
#
# Nothing under assets\ ships. This script is the only thing that reads a PNG;
# the game never decodes an image at runtime and never opens an asset file.
#
# WHAT COMES OUT
#   ART_PAL      one GLOBAL palette. The entire delivered art set is 65 colours,
#                so a per-sprite palette (the isometric build's approach, 11,154
#                bytes across 138 sprites) would be pure overhead here. 195 bytes
#                total, and - the real win - a fixed palette makes the fogged
#                colour table a pure function of the reveal level, so main.c
#                builds fogpal[32][66] once at startup and every blit becomes an
#                index lookup with no per-draw fog arithmetic at all.
#   ART_DATA     RLE over palette indices. Control byte C:
#                    C <  0x80  -> RUN     of (C + 1) px, next byte is the index
#                    C >= 0x80  -> LITERAL of ((C & 0x7F) + 1) px, then that many
#                Runs cap at 128. Row-major over the sprite box; runs are allowed
#                to cross row boundaries (the decoder wraps x at sp->w).
#                Index 0 is transparent in every sprite.
#   ART_SPRITES  16-byte records: w, h, anchor_x, anchor_y, data_off, data_len.
#
# MEASURED, so nobody re-derives it: pure RLE beats a per-sprite "raw if smaller"
# fallback by 1 byte across all 190 records. The fallback flag is not worth its
# complexity, so there isn't one.
#
# ANCHORS - two conventions, on purpose, and this is the subtle part of the file.
#   Tiles       (0,0). A tile is placed by its top-left and must stay 16x16, so
#               its position in the grid keeps meaning. Deliberately NOT trimmed.
#   Decorations bottom-centre of the trimmed box: the ground-contact point.
#   Character   CELL-RELATIVE: (CHAR_CX - trimX, CHAR_FOOT - trimY). Bottom-centre
#               of a per-frame trim would be WRONG here - the frames' trimmed
#               heights vary 22..26 px and their tops vary too, so a per-frame
#               bottom-centre anchor makes the walk cycle bob and skate. Every
#               frame instead measures from the same point in its 48x64 cell.
#               --sprite-test asserts all 64 frames reconstruct that one origin.
# Changing either convention means re-baking, and re-checking the tests.
param([switch]$Quiet)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$ROOT    = Split-Path -Parent $PSScriptRoot
$ASSETS  = Join-Path $ROOT 'assets'
$OutFile = Join-Path $ROOT 'src\art_data.h'

$TilesetPng = Join-Path $ASSETS 'Fantasy Forest\Tiles\Tileset.png'
# The same 8x15 grid, delivered a second time with a 1px gutter around every
# cell (137x256 = 8*16+9 by 15*16+16). Not baked from - the gutter would put a
# +1 stride into every crop for nothing - but CHECKED against, cell for cell,
# below. Two copies of one art set is exactly the situation where the shipped
# one quietly stops matching the one an artist is editing, and a tileset is the
# hardest place to notice: a wrong cell is one 16x16 square somewhere in a
# forest. Currently identical, verified: 120 cells, 0 differing bytes.
$TilesetPaddedPng = Join-Path $ASSETS 'Fantasy Forest\Tiles\Tileset1xPadding.png'
$DecorPng   = Join-Path $ASSETS 'Fantasy Forest\Decorations\Decorations.png'
$CharDir    = Join-Path $ASSETS 'Character'

# ---- Underworld (biome 2) source paths -----------------------------------
$UwGroundPng  = Join-Path $ASSETS 'Underworld\PNG\Ground_rocks.png'
$UwWaterPng   = Join-Path $ASSETS 'Underworld\PNG\Water_coasts.png'
$UwObjectsDir = Join-Path $ASSETS 'Underworld\PNG\Objects_separately'
$PortalPng    = Join-Path $ASSETS 'Portal\Dimensional_Portal.png'

# ---- Lumiara (biome 3) source paths ---------------------------------------
$LumDir        = Join-Path $ASSETS 'Lumiara'
$LumTopDownDir = Join-Path $LumDir 'TopDown'
$LumChasmPng   = Join-Path $LumTopDownDir 'tileset_chasm_to_grass_16x16.png'
$LumCobblePng  = Join-Path $LumTopDownDir 'tileset_grass_to_cobblestone_16x16.png'
$LumWaterPng   = Join-Path $LumTopDownDir 'tileset_water_to_grass_16x16.png'

# ---- Tileset -------------------------------------------------------------
# 128x240 = an 8x15 grid of 16x16 cells, 92 of which have content. Emitted
# MECHANICALLY as ART_TILE_C<col>_R<row>, with no hand-written name table: which
# cell means "grass top-left edge" is a rendering decision, so it lives in
# main.c next to the code that indexes it, where a wrong entry is visible on
# screen. A curated 92-entry name table here could only drift from the PNG.
$TILE     = 16
$TilesCols = 8
$TilesRows = 15

# ---- Decorations --------------------------------------------------------
# Curated rects, because auto-segmentation cannot name things and gets two cases
# wrong on this sheet: the two logs' bounding boxes TOUCH (y31/y32) so any
# gap-based merge fuses them, and the small mushrooms come in intertwined
# triplets whose boxes overlap, so each cluster must be one sprite.
#
# The list is not trusted on faith - three assertions below make it self-checking:
# no two rects may overlap, every rect must contain opaque pixels, and EVERY
# opaque pixel on the sheet must fall inside exactly one rect. That last one is
# what catches a moved, resized or newly-added object, which is the failure a
# hand-written list would otherwise hide.
$Decor = @(
    @{ n = 'LOG_A';            x =  75; y =   0; w = 40; h = 32 }
    @{ n = 'BUSH_LARGE_A';     x =  13; y =   4; w = 35; h = 27 }
    @{ n = 'MUSHROOM_BIG_A';   x = 203; y =   8; w = 13; h = 14 }
    @{ n = 'BUSH_SMALL_A';     x = 136; y =   9; w = 16; h = 14 }
    @{ n = 'MUSHROOM_TINY_A';  x = 235; y =     9; w = 11; h = 14 }
    @{ n = 'MUSHROOM_MED_A';   x = 170; y =  10; w = 13; h = 13 }
    @{ n = 'LOG_B';            x =  75; y =  32; w = 39; h = 30 }
    @{ n = 'BUSH_LARGE_B';     x =  15; y =  35; w = 31; h = 25 }
    @{ n = 'MUSHROOM_BIG_B';   x = 204; y =  40; w = 12; h = 12 }
    @{ n = 'BUSH_SMALL_B';     x = 136; y =  41; w = 16; h = 13 }
    @{ n = 'MUSHROOM_TINY_B';  x = 235; y =   41; w = 11; h = 13 }
    @{ n = 'MUSHROOM_MED_B';   x = 170; y =  42; w = 12; h = 12 }
    @{ n = 'REED_A';           x =  68; y =  67; w = 24; h = 27 }
    @{ n = 'REED_B';           x = 100; y =  67; w = 24; h = 27 }
    @{ n = 'TUFT_A';           x =   8; y =  70; w = 14; h = 22 }
    @{ n = 'TUFT_B';           x =  40; y =  70; w = 14; h = 22 }
    @{ n = 'MUSHROOM_BIG_C';   x = 203; y =  71; w = 13; h = 14 }
    @{ n = 'MUSHROOM_TINY_C';  x = 235; y =   72; w = 11; h = 14 }
    @{ n = 'MUSHROOM_MED_C';   x = 170; y =  73; w = 13; h = 13 }
    @{ n = 'REED_C';           x =  68; y =  99; w = 24; h = 25 }
    @{ n = 'REED_D';           x = 100; y =  99; w = 24; h = 25 }
    @{ n = 'TUFT_C';           x =   8; y = 102; w = 14; h = 20 }
    @{ n = 'TUFT_D';           x =  40; y = 102; w = 14; h = 20 }
    @{ n = 'STONE_A';          x = 169; y = 109; w = 15; h = 11 }
    @{ n = 'STONE_B';          x = 200; y = 109; w = 16; h = 12 }
    @{ n = 'STONE_C';          x = 234; y = 109; w = 12; h = 10 }
    @{ n = 'PEBBLES';          x = 138; y = 110; w = 12; h = 10 }
    @{ n = 'ROCK_A';           x = 163; y = 133; w = 26; h = 22 }
    @{ n = 'ROCK_B';           x = 194; y = 133; w = 27; h = 23 }
    @{ n = 'ROCK_C';           x = 228; y = 133; w = 22; h = 21 }
    @{ n = 'TREE_A';           x =  11; y = 143; w = 70; h = 98 }
    @{ n = 'TREE_B';           x =  88; y = 143; w = 70; h = 94 }
    @{ n = 'PINE_A';           x = 163; y = 163; w = 38; h = 74 }
    @{ n = 'PINE_B';           x = 211; y = 163; w = 38; h = 70 }
)

# ---- Character ----------------------------------------------------------
# Each sheet is 384x64 = 8 frames of 48x64. The filenames say "down/up/
# left_down/right_down"; left_down and right_down are the 3/4 views used for
# pure left and right, and they are INDEPENDENTLY DRAWN, not mirrors - verified,
# so both are baked. Row order here is the FACE4_* order main.c indexes by.
$CharCellW = 48
$CharCellH = 64
$CharFrames = 8
$CHAR_CX    = 24   # cell centre x; every frame is drawn centred on this
$CHAR_FOOT  = 44   # one row below row 43, the lowest foot pixel in ANY frame
$CharSheets = @(
    @{ n = 'IDLE_DOWN';  f = 'idle_down.png' }
    @{ n = 'IDLE_RIGHT'; f = 'idle_right_down.png' }
    @{ n = 'IDLE_UP';    f = 'idle_up.png' }
    @{ n = 'IDLE_LEFT';  f = 'idle_left_down.png' }
    @{ n = 'WALK_DOWN';  f = 'walk_down.png' }
    @{ n = 'WALK_RIGHT'; f = 'walk_right_down.png' }
    @{ n = 'WALK_UP';    f = 'walk_up.png' }
    @{ n = 'WALK_LEFT';  f = 'walk_left_down.png' }
)

# ---- PNG decode ---------------------------------------------------------
# GetPixel is far too slow at this volume, so LockBits + one marshalled copy.
# GDI+ normalises whatever the PNG actually is into 32bppArgb, so any bit depth,
# palette or interlacing the authoring tool emits is handled for free.
function Get-PixelData([string]$Path) {
    if (-not (Test-Path $Path)) { throw "bake: missing source $Path" }
    $bmp  = New-Object System.Drawing.Bitmap -ArgumentList $Path
    try {
        $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $bmp.Width, $bmp.Height
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        } finally { $bmp.UnlockBits($data) }
        return @{ W = $bmp.Width; H = $bmp.Height; Stride = $data.Stride; B = $bytes }
    } finally { $bmp.Dispose() }
}

# Global palette. Index 0 is transparent and is never stored, so real colours
# start at 1. Insertion-ordered, and the source order below is fixed, so a
# re-bake of unchanged art produces a byte-identical header.
$script:PalMap  = @{}
$script:PalList = New-Object System.Collections.Generic.List[int[]]
$script:PartialAlpha = 0

function Get-PaletteIndex([int]$r, [int]$g, [int]$b) {
    $key = "$r,$g,$b"
    if (-not $script:PalMap.ContainsKey($key)) {
        if ($script:PalList.Count -ge 254) { throw "bake: global palette exceeded 254 entries" }
        $script:PalList.Add(@($r, $g, $b))
        $script:PalMap[$key] = $script:PalList.Count   # 1-based
    }
    return $script:PalMap[$key]
}

# Region of a sheet -> palette-index array. Alpha is BINARY in this art set (0
# semi-transparent pixels, verified), so the test is a hard >= 128 cutoff. Any
# partial alpha is counted and REPORTED rather than silently rounded: a future
# delivery with soft edges would otherwise change how everything looks with no
# signal at all.
function Get-IndexArray($Img, [int]$x0, [int]$y0, [int]$w, [int]$h, [scriptblock]$Tint = $null) {
    $b = $Img.B; $stride = $Img.Stride
    $idx = New-Object byte[] ($w * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $row = ($y0 + $y) * $stride
        $dst = $y * $w
        for ($x = 0; $x -lt $w; $x++) {
            $o = $row + ($x0 + $x) * 4
            $a = $b[$o + 3]
            if ($a -lt 128) {
                if ($a -ne 0) { $script:PartialAlpha++ }
                $idx[$dst + $x] = 0
            } else {
                if ($a -ne 255) { $script:PartialAlpha++ }
                $r = $b[$o + 2]; $g = $b[$o + 1]; $bl = $b[$o]
                if ($Tint) {
                    $rgb = & $Tint $r $g $bl
                    $r = $rgb[0]; $g = $rgb[1]; $bl = $rgb[2]
                }
                $idx[$dst + $x] = [byte](Get-PaletteIndex $r $g $bl)
            }
        }
    }
    return ,$idx
}

function Get-OpaqueBox($Img, [int]$x0, [int]$y0, [int]$w, [int]$h) {
    $b = $Img.B; $stride = $Img.Stride
    $minX = $w; $minY = $h; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $h; $y++) {
        $row = ($y0 + $y) * $stride
        for ($x = 0; $x -lt $w; $x++) {
            if ($b[$row + ($x0 + $x) * 4 + 3] -ge 128) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { return $null }
    return @{ X = $minX; Y = $minY; W = $maxX - $minX + 1; H = $maxY - $minY + 1 }
}

# ---- Lumiara palette quantization ----------------------------------------
# Forest/Underworld are flat-shaded pixel art - each new delivery has added a
# few dozen colours to the global palette. Lumiara's source is painterly
# (soft gradients, dither), and a straight per-pixel scan of even just the
# curated tile cells below finds ~190 distinct opaque colours; the full
# curated set (tiles + decorations) over 600 - against a global-palette
# budget (254 entries, CLAUDE.md) that has ~59 slots left before this biome.
# Reusing every distinct source pixel is therefore not an option. A weighted
# median-cut quantizer reduces the WHOLE Lumiara set to a fixed
# LUM_PALETTE_BUDGET of representative colours up front; every Lumiara sprite
# is then baked through that one fixed mapping via Get-IndexArray's existing
# $Tint hook (the same mechanism the Underworld acid-fill tint uses), so the
# global palette gains at most LUM_PALETTE_BUDGET new entries no matter how
# much source art feeds it.
$LUM_PALETTE_BUDGET = 48

# ---- Lumiara ground-tile calming ------------------------------------------
# MEASURED PROBLEM, so nobody re-derives it. Mean local contrast ("dither" -
# the mean absolute luminance delta between horizontally/vertically adjacent
# pixels) over each biome's ground tiles:
#
#     Forest      grass_base   7.1    dirt_fill   4.9
#     Underworld  ground_base  7.1    dirt_fill  19.5
#     Lumiara     grass_base  22.1    cobble     40.4    <-- 3x and 8x Forest
#
# and over a whole rendered 480x270 frame (--lit --dev, seed 1):
#
#     Forest 13.5   Underworld 23.0   Lumiara 31.7
#
# Lumiara reads as harsh/glaring NOT because it is bright - measured over the
# same frames it is in fact the DARKEST of the three (mean luma 90.9 against
# Forest's 105.6) and less saturated than Forest (0.56 against 0.71) - but
# because of that pixel-level speckle. The source sheets are painterly
# illustrations cut into 16x16 cells, so what is a pleasing soft gradient at
# illustration scale becomes a high-frequency checkerboard when tiled as a
# ground fill, and the median-cut quantizer above sharpens it further by
# snapping neighbouring near-identical colours into different buckets.
#
# GROUND TILES ONLY. Ground should recede and props should read against it, so
# the 20 curated tile cells get calmed and the 16 decorations are left at full
# fidelity deliberately - blurring those would cost the dream trees and fauna
# exactly the detail they are on screen for.
#
# SMOOTH blends each pixel toward its 3x3 neighbourhood mean, which kills the
# speckle while keeping the tile's larger shapes and its hue.
#
# FLATTEN then blends toward the CELL's own mean colour, and it is the one
# doing the heavy lifting. Smoothing alone got a rendered frame from 31.7 to
# 25.1 and still looked wrong beside Forest, because the remaining contrast is
# not dither at all - it is dense little crystal and flower MOTIFS authored
# into every cell, which a blur softens but cannot remove. Forest's ground
# works precisely because it is nearly a flat colour carrying light texture
# (dither 7.1 / 4.9), so the ground recedes and the props read against it.
# FLATTEN is what buys that: it pulls each tile toward its own average, which
# keeps every tile's hue and its relationship to its neighbours while dropping
# the motif contrast that was competing with the sprites.
#
# DIM and DESAT are a mild tone trim on top: the water and void cells measure
# luma 118-145 at source, the brightest ground in the game, and saturated cyan
# reads as glare at any luminance. Deliberately gentle - the frame is already
# darker than Forest's, so the fix here is contrast, not exposure.
# Values chosen by baking three candidates and comparing rendered frames
# against Forest and Underworld. Measured mean local contrast over a whole
# --lit --dev frame (seed 1), Forest 13.5 / Underworld 23.0 for scale:
#
#     source                             31.7   the reported "too bright"
#     0.60 / 0.45 / 0.95 / 0.88          22.8   still visibly busy
#     0.70 / 0.62 / 0.93 / 0.76          21.0   <-- chosen
#     0.80 / 0.78 / 0.90 / 0.62          21.1   ground near-neutral grey
#
# Note the floor around 21: past this point the remaining frame contrast is
# the DECORATIONS, not the ground, so flattening the ground further only
# drains its colour and buys no measurable calm. Getting nearer Forest's 13.5
# would mean calming the props too, which is deliberately not done.
$LUM_TILE_SMOOTH  = 0.70  # 0 = untouched, 1 = full 3x3 box blur
$LUM_TILE_FLATTEN = 0.62  # 0 = keep tile detail, 1 = solid flat colour per tile
$LUM_TILE_DIM     = 0.93  # luminance scale
$LUM_TILE_DESAT   = 0.76  # 1 = source saturation, 0 = greyscale

# A cell pulled out as flat per-channel arrays, so it can be filtered before it
# ever reaches the palette. Alpha is carried through untouched - the filters
# below move colour only, so an opaque tile stays opaque and --tile-test's
# base-fill opacity assertion cannot be affected by any of this.
function Get-CellRGBA($Img, [int]$x0, [int]$y0, [int]$w, [int]$h) {
    $b = $Img.B; $stride = $Img.Stride
    $r = New-Object double[] ($w*$h)
    $g = New-Object double[] ($w*$h)
    $bl = New-Object double[] ($w*$h)
    $a = New-Object byte[] ($w*$h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $o = ($y0+$y)*$stride + ($x0+$x)*4
            $i = $y*$w + $x
            $r[$i] = [double]$b[$o+2]; $g[$i] = [double]$b[$o+1]; $bl[$i] = [double]$b[$o]
            $a[$i] = $b[$o+3]
        }
    }
    return @{ W = $w; H = $h; R = $r; G = $g; B = $bl; A = $a }
}

# Smooth, then tone, then round+clamp to integers. Rounding HERE rather than at
# bake time is load-bearing: the histogram and the bake both read this same
# already-integer cell, so every colour the bake asks the quantizer about is
# one the histogram actually saw. Rounding independently in two places would
# let a pixel land one unit away from any histogram key and throw.
function Invoke-LumGroundTone($cell) {
    $w = $cell.W; $h = $cell.H
    $chans = @($cell.R, $cell.G, $cell.B)
    $sm = @()
    foreach ($src in $chans) {
        $dst = New-Object double[] ($w*$h)
        for ($y = 0; $y -lt $h; $y++) {
            for ($x = 0; $x -lt $w; $x++) {
                $sum = 0.0; $cnt = 0
                for ($dy = -1; $dy -le 1; $dy++) {
                    $ny = $y + $dy
                    if ($ny -lt 0 -or $ny -ge $h) { continue }
                    for ($dx = -1; $dx -le 1; $dx++) {
                        $nx = $x + $dx
                        if ($nx -lt 0 -or $nx -ge $w) { continue }
                        $sum += $src[$ny*$w + $nx]; $cnt++
                    }
                }
                $i = $y*$w + $x
                $dst[$i] = $src[$i] * (1.0 - $LUM_TILE_SMOOTH) + ($sum/$cnt) * $LUM_TILE_SMOOTH
            }
        }
        $sm += ,$dst
    }
    $r = $sm[0]; $g = $sm[1]; $bl = $sm[2]
    # Flatten toward the cell's own mean, over the OPAQUE pixels only - letting
    # transparent pixels vote would drag the average toward whatever RGB the
    # source happens to store behind alpha 0.
    if ($LUM_TILE_FLATTEN -gt 0.0) {
        $mr = 0.0; $mg = 0.0; $mb = 0.0; $mn = 0
        for ($i = 0; $i -lt ($w*$h); $i++) {
            if ($cell.A[$i] -lt 128) { continue }
            $mr += $r[$i]; $mg += $g[$i]; $mb += $bl[$i]; $mn++
        }
        if ($mn -gt 0) {
            $mr /= $mn; $mg /= $mn; $mb /= $mn
            $f = $LUM_TILE_FLATTEN
            for ($i = 0; $i -lt ($w*$h); $i++) {
                $r[$i]  = $r[$i]  * (1.0-$f) + $mr * $f
                $g[$i]  = $g[$i]  * (1.0-$f) + $mg * $f
                $bl[$i] = $bl[$i] * (1.0-$f) + $mb * $f
            }
        }
    }
    for ($i = 0; $i -lt ($w*$h); $i++) {
        $lum = 0.299*$r[$i] + 0.587*$g[$i] + 0.114*$bl[$i]
        $vr = ($lum + ($r[$i]  - $lum) * $LUM_TILE_DESAT) * $LUM_TILE_DIM
        $vg = ($lum + ($g[$i]  - $lum) * $LUM_TILE_DESAT) * $LUM_TILE_DIM
        $vb = ($lum + ($bl[$i] - $lum) * $LUM_TILE_DESAT) * $LUM_TILE_DIM
        $ir = [int][Math]::Round($vr); $ig = [int][Math]::Round($vg); $ib = [int][Math]::Round($vb)
        if ($ir -lt 0) { $ir = 0 } elseif ($ir -gt 255) { $ir = 255 }
        if ($ig -lt 0) { $ig = 0 } elseif ($ig -gt 255) { $ig = 255 }
        if ($ib -lt 0) { $ib = 0 } elseif ($ib -gt 255) { $ib = 255 }
        $r[$i] = $ir; $g[$i] = $ig; $bl[$i] = $ib
    }
    return @{ W = $w; H = $h; R = $r; G = $g; B = $bl; A = $cell.A }
}

function Get-CellHistogram($cell, [hashtable]$Hist) {
    for ($i = 0; $i -lt ($cell.W * $cell.H); $i++) {
        if ($cell.A[$i] -lt 128) { continue }
        $key = "$([int]$cell.R[$i]),$([int]$cell.G[$i]),$([int]$cell.B[$i])"
        if ($Hist.ContainsKey($key)) { $Hist[$key]++ } else { $Hist[$key] = 1 }
    }
}

function Get-IndexArrayFromCell($cell, [hashtable]$Map) {
    $n = $cell.W * $cell.H
    $idx = New-Object byte[] $n
    for ($i = 0; $i -lt $n; $i++) {
        if ($cell.A[$i] -lt 128) { $idx[$i] = 0; continue }
        $key = "$([int]$cell.R[$i]),$([int]$cell.G[$i]),$([int]$cell.B[$i])"
        if (-not $Map.ContainsKey($key)) {
            throw "bake: toned Lumiara tile pixel $key has no quantized mapping"
        }
        $q = $Map[$key]
        $idx[$i] = [byte](Get-PaletteIndex $q[0] $q[1] $q[2])
    }
    return ,$idx
}

function Get-OpaqueHistogram($Img, [int]$x0, [int]$y0, [int]$w, [int]$h, [hashtable]$Hist) {
    $b = $Img.B; $stride = $Img.Stride
    for ($y = 0; $y -lt $h; $y++) {
        $row = ($y0 + $y) * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $o = $row + ($x0 + $x) * 4
            if ($b[$o + 3] -ge 128) {
                $key = "$($b[$o+2]),$($b[$o+1]),$($b[$o])"
                if ($Hist.ContainsKey($key)) { $Hist[$key]++ } else { $Hist[$key] = 1 }
            }
        }
    }
}

# Classic weighted median-cut: repeatedly split the bucket with the widest
# single-channel range at its (unweighted) median along that channel, until
# there are K buckets, then average each bucket (weighted by pixel count) to
# get its representative colour. Every ORIGINAL histogram entry ends up in
# exactly one final bucket, so the r,g,b -> representative map falls out
# directly from bucket membership - no separate nearest-colour search needed.
function Get-MedianCutMap([hashtable]$Hist, [int]$K) {
    $entries = New-Object System.Collections.Generic.List[object]
    foreach ($key in $Hist.Keys) {
        $p = $key -split ','
        $entries.Add([pscustomobject]@{ R = [int]$p[0]; G = [int]$p[1]; B = [int]$p[2]; N = $Hist[$key] })
    }
    $buckets = New-Object System.Collections.Generic.List[object]
    $buckets.Add($entries)
    while ($buckets.Count -lt $K) {
        $bestIdx = -1; $bestRange = 0; $bestChan = 'R'
        for ($i = 0; $i -lt $buckets.Count; $i++) {
            $bk = $buckets[$i]
            if ($bk.Count -le 1) { continue }
            $rMin = 255; $rMax = 0; $gMin = 255; $gMax = 0; $bMin = 255; $bMax = 0
            foreach ($e in $bk) {
                if ($e.R -lt $rMin) { $rMin = $e.R }; if ($e.R -gt $rMax) { $rMax = $e.R }
                if ($e.G -lt $gMin) { $gMin = $e.G }; if ($e.G -gt $gMax) { $gMax = $e.G }
                if ($e.B -lt $bMin) { $bMin = $e.B }; if ($e.B -gt $bMax) { $bMax = $e.B }
            }
            $rr = $rMax - $rMin; $gr = $gMax - $gMin; $br = $bMax - $bMin
            $mx = [Math]::Max($rr, [Math]::Max($gr, $br))
            if ($mx -gt $bestRange) {
                $bestRange = $mx; $bestIdx = $i
                $bestChan = if ($mx -eq $rr) { 'R' } elseif ($mx -eq $gr) { 'G' } else { 'B' }
            }
        }
        if ($bestIdx -lt 0) { break }   # no bucket left with more than one colour
        $sorted = $buckets[$bestIdx] | Sort-Object $bestChan
        $mid = [int][Math]::Floor($sorted.Count / 2)
        $lo = New-Object System.Collections.Generic.List[object]
        $hi = New-Object System.Collections.Generic.List[object]
        for ($i = 0; $i -lt $sorted.Count; $i++) { if ($i -lt $mid) { $lo.Add($sorted[$i]) } else { $hi.Add($sorted[$i]) } }
        $buckets.RemoveAt($bestIdx)
        $buckets.Add($lo); $buckets.Add($hi)
    }
    $map = @{}
    foreach ($bk in $buckets) {
        $sr = 0; $sg = 0; $sb = 0; $sn = 0
        foreach ($e in $bk) { $sr += $e.R * $e.N; $sg += $e.G * $e.N; $sb += $e.B * $e.N; $sn += $e.N }
        if ($sn -eq 0) { continue }
        $ar = [int][Math]::Round($sr / $sn); $ag = [int][Math]::Round($sg / $sn); $ab = [int][Math]::Round($sb / $sn)
        foreach ($e in $bk) { $map["$($e.R),$($e.G),$($e.B)"] = @($ar, $ag, $ab) }
    }
    return $map
}

# ---- RLE ----------------------------------------------------------------
function Get-Rle([byte[]]$idx) {
    $out = New-Object System.Collections.Generic.List[byte]
    $lit = New-Object System.Collections.Generic.List[byte]
    $flush = {
        while ($lit.Count -gt 0) {
            $take = [Math]::Min(128, $lit.Count)
            $out.Add([byte](0x80 -bor ($take - 1)))
            for ($k = 0; $k -lt $take; $k++) { $out.Add($lit[$k]) }
            $lit.RemoveRange(0, $take)
        }
    }
    $i = 0; $n = $idx.Length
    while ($i -lt $n) {
        $v = $idx[$i]
        $run = 1
        while (($i + $run) -lt $n -and $idx[$i + $run] -eq $v -and $run -lt 128) { $run++ }
        if ($run -ge 2) {
            & $flush
            $out.Add([byte]($run - 1))
            $out.Add($v)
            $i += $run
        } else {
            $lit.Add($v)
            $i++
        }
    }
    & $flush
    # Comma-wrapped: PowerShell unrolls a returned array into Object[], which
    # then refuses to convert back to IEnumerable[byte] at the AddRange call.
    return ,$out.ToArray()
}

# ---- Accumulate ---------------------------------------------------------
$records = New-Object System.Collections.Generic.List[object]
$dataBytes = New-Object System.Collections.Generic.List[byte]
$dedupe = @{}          # rle-hex -> @{ Off; Len }
$script:RawPx = 0
$script:DedupeHits = 0

function Add-Sprite([string]$Name, [byte[]]$idx, [int]$w, [int]$h, [int]$ax, [int]$ay) {
    if ($ax -lt 0 -or $ay -lt 0) { throw "bake: $Name has a negative anchor ($ax,$ay)" }
    if ($ax -gt 65535 -or $ay -gt 65535) { throw "bake: $Name anchor out of range" }
    [byte[]]$rle = Get-Rle $idx
    $script:RawPx += $idx.Length
    # Identical pixel streams share one copy. The idle sheets repeat frames
    # exactly (6-7 unique of 8), so this is real bytes, not hygiene.
    $key = [System.BitConverter]::ToString($rle)
    if ($dedupe.ContainsKey($key)) {
        $hit = $dedupe[$key]
        $off = $hit.Off; $len = $hit.Len
        $script:DedupeHits++
    } else {
        $off = $dataBytes.Count
        $dataBytes.AddRange($rle)
        $len = $rle.Length
        $dedupe[$key] = @{ Off = $off; Len = $len }
    }
    $records.Add([pscustomobject]@{
        Name = $Name; W = $w; H = $h; AX = $ax; AY = $ay; Off = $off; Len = $len
    })
}

if (-not $Quiet) { Write-Host "bake: reading sources" -ForegroundColor Cyan }

# --- tiles ---
$img = Get-PixelData $TilesetPng
if ($img.W -ne ($TilesCols * $TILE) -or $img.H -ne ($TilesRows * $TILE)) {
    throw ("bake: tileset expected {0}x{1}, got {2}x{3}" -f ($TilesCols*$TILE), ($TilesRows*$TILE), $img.W, $img.H)
}

# PROVENANCE. Every cell baked below must be byte-identical to the same cell of
# the padded delivery, so "the shipped tiles are the delivered tiles" is a
# re-run rather than a promise. The gutter is 1px around and between, so cell
# (c,r) starts at (1 + c*17, 1 + r*17) there against (c*16, r*16) here.
#
# Compared on all four channels including alpha: a cell whose transparency
# moved is a cell whose SHAPE moved, and shape is what the autotile tables are
# indexing. Reported as a count with the first offender named, rather than
# failing on the first byte - "cell c6,r13 and 3 others differ" says an edit
# happened to one region; "1 byte differs" could be anything.
$imgPad = Get-PixelData $TilesetPaddedPng
$padStep = $TILE + 1
if ($imgPad.W -ne ($TilesCols * $padStep + 1) -or $imgPad.H -ne ($TilesRows * $padStep + 1)) {
    throw ("bake: padded tileset expected {0}x{1}, got {2}x{3}" -f `
        ($TilesCols*$padStep+1), ($TilesRows*$padStep+1), $imgPad.W, $imgPad.H)
}
$padMismatch = 0
$padFirst = ''
for ($r = 0; $r -lt $TilesRows; $r++) {
    for ($c = 0; $c -lt $TilesCols; $c++) {
        $bad = 0
        for ($y = 0; $y -lt $TILE; $y++) {
            $o1 = ($r * $TILE + $y) * $img.Stride + ($c * $TILE) * 4
            $o2 = (1 + $r * $padStep + $y) * $imgPad.Stride + (1 + $c * $padStep) * 4
            for ($k = 0; $k -lt ($TILE * 4); $k++) {
                if ($img.B[$o1 + $k] -ne $imgPad.B[$o2 + $k]) { $bad++ }
            }
        }
        if ($bad -gt 0) {
            $padMismatch++
            if (-not $padFirst) { $padFirst = "c{0},r{1} ({2} bytes)" -f $c, $r, $bad }
        }
    }
}
if ($padMismatch -gt 0) {
    throw ("bake: {0} of {1} tileset cells differ between Tileset.png and Tileset1xPadding.png - first {2}. The two deliveries of this sheet have diverged; reconcile them before baking." -f `
        $padMismatch, ($TilesCols * $TilesRows), $padFirst)
}
if (-not $Quiet) {
    Write-Host ("  provenance  {0} cells identical in Tileset.png and Tileset1xPadding.png" -f ($TilesCols * $TilesRows))
}
$tileCount = 0
# 28 of the 120 cells are empty, so sprite indices are NOT row*8+col - the enum
# order skips the gaps. ART_TILE_AT below restores the grid relationship, which
# anything wanting to walk the tileset by position needs. Emitting the empty
# cells as zero-length sprites instead would cost 448 bytes of records against
# this table's 240, and would put 28 undrawable entries in the enum.
$tileAt = New-Object 'int[,]' $TilesRows, $TilesCols
for ($r = 0; $r -lt $TilesRows; $r++) {
    for ($c = 0; $c -lt $TilesCols; $c++) { $tileAt[$r, $c] = -1 }
}
for ($r = 0; $r -lt $TilesRows; $r++) {
    for ($c = 0; $c -lt $TilesCols; $c++) {
        if ($null -eq (Get-OpaqueBox $img ($c * $TILE) ($r * $TILE) $TILE $TILE)) { continue }
        $idx = Get-IndexArray $img ($c * $TILE) ($r * $TILE) $TILE $TILE
        $tileAt[$r, $c] = $records.Count      # before Add-Sprite appends it
        Add-Sprite ("TILE_C{0}_R{1}" -f $c, $r) $idx $TILE $TILE 0 0
        $tileCount++
    }
}
if (-not $Quiet) { Write-Host ("  tiles       {0} of {1} cells have content" -f $tileCount, ($TilesCols*$TilesRows)) }

# --- decorations ---
$img = Get-PixelData $DecorPng
# Assertion 1: no two curated rects may overlap.
for ($i = 0; $i -lt $Decor.Count; $i++) {
    for ($j = $i + 1; $j -lt $Decor.Count; $j++) {
        $a = $Decor[$i]; $b = $Decor[$j]
        if ($a.x -lt ($b.x + $b.w) -and $b.x -lt ($a.x + $a.w) -and
            $a.y -lt ($b.y + $b.h) -and $b.y -lt ($a.y + $a.h)) {
            throw ("bake: decoration rects {0} and {1} overlap" -f $a.n, $b.n)
        }
    }
}
# Assertion 2: every opaque pixel on the sheet must be inside exactly one rect.
# This is what makes the hand-written list above safe: a moved, resized or added
# object fails the bake instead of quietly not being drawn.
$covered = New-Object 'byte[]' ($img.W * $img.H)
foreach ($d in $Decor) {
    for ($y = $d.y; $y -lt ($d.y + $d.h); $y++) {
        for ($x = $d.x; $x -lt ($d.x + $d.w); $x++) { $covered[$y * $img.W + $x] = 1 }
    }
}
$loose = 0
for ($y = 0; $y -lt $img.H; $y++) {
    for ($x = 0; $x -lt $img.W; $x++) {
        if ($img.B[$y * $img.Stride + $x * 4 + 3] -ge 128 -and $covered[$y * $img.W + $x] -eq 0) { $loose++ }
    }
}
if ($loose -gt 0) {
    throw ("bake: {0} opaque pixel(s) on the decoration sheet lie outside every curated rect - an object was added, moved or resized" -f $loose)
}
foreach ($d in $Decor) {
    $box = Get-OpaqueBox $img $d.x $d.y $d.w $d.h
    if ($null -eq $box) { throw ("bake: decoration rect {0} is fully transparent" -f $d.n) }
    if ($box.W -ne $d.w -or $box.H -ne $d.h) {
        Write-Host ("  note: {0} rect {1}x{2} trims to {3}x{4} - rect is loose" -f $d.n, $d.w, $d.h, $box.W, $box.H) -ForegroundColor Yellow
    }
    $idx = Get-IndexArray $img ($d.x + $box.X) ($d.y + $box.Y) $box.W $box.H
    # Ground-contact anchor: bottom-centre of the opaque box.
    Add-Sprite $d.n $idx $box.W $box.H ([int][Math]::Floor($box.W / 2)) $box.H
}
if (-not $Quiet) { Write-Host ("  decorations {0} objects, all opaque pixels accounted for" -f $Decor.Count) }

# ---- Underworld tiles (biome 2) ------------------------------------------
#
# NOTE ON ORDER: everything Underworld (tiles, decorations, portal) is baked
# BEFORE the character sheets below, deliberately - sprite_selftest identifies
# character frames as "everything from ART_CH_IDLE_DOWN_0 to the end of
# ART_SPRITES[]" (main.c, see the `i >= ART_CH_IDLE_DOWN_0` check), so the
# character block must stay LAST or that heuristic silently misclassifies
# whatever comes after it.
# Ground_rocks.png and Water_coasts.png are vendor "cave wall" autotile mega
# sheets in a layout that is NOT the engine's 3x3 blob format and not worth
# reverse-engineering cell-by-cell. Rather than mechanically baking either
# whole sheet (a 31x37 and a 66x16 grid - CLAUDE.md: "bake only what a caller
# in main.c actually draws"), specific 16x16 cells are curated by hand, the
# same way $Decor curates Fantasy Forest's decorations. Each cell was located
# with tools/inspect-grid.ps1 (a scratch dev tool, not part of the build) and
# alpha-verified opaque before being picked.
#
# One clean 4x3 "hole" motif in Water_coasts.png (cols 0-3, rows 0-2) supplies
# a real 3x3 blob_slice mapping (see main.c's blob_slice: row = N?(S?1:2):0,
# col = W?(E?1:2):0) for the acid rim - its natural green algae highlight is
# reserved for the acid/hazard edge specifically, so it is not reused for the
# cosmetic ground/dirt edge below.
# Every (c,r) below was verified fully opaque (256/256 px, alpha>=250) by a
# full-sheet automated scan, not by eye - the first pass here was picked by
# eye against tools/inspect-grid.ps1 crops and got three cells wrong (two
# reading as "flat grey floor" that were in fact mostly transparent "you can
# see through to the background" hole interior, one rock-wall cell the same
# way), caught only once --tile-test's opacity/obstacle-visibility checks ran
# against real Underworld tables. ACID_NW/N/NE are the only three cells of the
# curated acid-rim "hole" motif confirmed opaque by that same scan - the
# W/E/S/SW/SE positions of the original 3x3 read are NOT (the motif's side and
# bottom bands carry real transparency), so main.c's water_edge/rock_ring
# tables reuse these three for all eight non-centre slots rather than
# referencing cells that would fail the same obstacle-visibility check.
$UwTileCells = @(
    # Acid water coastlines / hole edges (3x3 blob_slice mapping)
    @{ n = 'UW_ACID_NW';    src = 'wc'; c = 5; r = 1 }
    @{ n = 'UW_ACID_N';     src = 'wc'; c = 6; r = 1 }
    @{ n = 'UW_ACID_NE';    src = 'wc'; c = 8; r = 1 }
    @{ n = 'UW_ACID_W';     src = 'wc'; c = 5; r = 2 }
    @{ n = 'UW_ACID_E';     src = 'wc'; c = 8; r = 2 }
    @{ n = 'UW_ACID_SW';    src = 'wc'; c = 5; r = 4 }
    @{ n = 'UW_ACID_S';     src = 'wc'; c = 6; r = 4 }
    @{ n = 'UW_ACID_SE';    src = 'wc'; c = 8; r = 4 }

    # Ground floor tiles: Ground_rocks
    @{ n = 'UW_FLOOR_A';    src = 'gr'; c = 2; r = 2 }
    @{ n = 'UW_FLOOR_B';    src = 'gr'; c = 3; r = 2 }
    @{ n = 'UW_FLOOR_C';    src = 'gr'; c = 1; r = 2 }
    @{ n = 'UW_FLOOR_D';    src = 'gr'; c = 2; r = 1 }

    # Darker surface / rubble (Rubble A..D) from Ground_rocks (all 256/256 opaque)
    @{ n = 'UW_RUBBLE_A';   src = 'gr'; c = 13; r = 57 }
    @{ n = 'UW_RUBBLE_B';   src = 'gr'; c =  7; r = 20 }
    @{ n = 'UW_RUBBLE_C';   src = 'gr'; c =  9; r = 20 }
    @{ n = 'UW_RUBBLE_D';   src = 'gr'; c =  7; r = 15 }

    # Dark spiked rock cliffs (Cliff & Rockwall): Ground_rocks
    @{ n = 'UW_ROCKWALL_A'; src = 'gr'; c =  2; r =  3 }
    @{ n = 'UW_ROCKWALL_B'; src = 'gr'; c = 10; r =  9 }
    @{ n = 'UW_ROCK_NW';    src = 'gr'; c =  1; r =  1 }
    @{ n = 'UW_ROCK_N';     src = 'gr'; c =  2; r =  1 }
    @{ n = 'UW_ROCK_NE';    src = 'gr'; c =  3; r =  1 }
    @{ n = 'UW_ROCK_W';     src = 'gr'; c =  1; r =  2 }
    @{ n = 'UW_ROCK_E';     src = 'gr'; c =  3; r =  2 }
    @{ n = 'UW_ROCK_SW';    src = 'gr'; c =  1; r =  3 }
    @{ n = 'UW_ROCK_S';     src = 'gr'; c =  2; r =  3 }
    @{ n = 'UW_ROCK_SE';    src = 'gr'; c =  3; r =  3 }

    # Clean toxic acid water fill (col 22, row 0 in water_coasts is 100% opaque toxic green)
    @{ n = 'UW_ACIDFILL_A'; src = 'wc'; c = 22; r =  0 }
    @{ n = 'UW_ACIDFILL_B'; src = 'wc'; c = 22; r =  0 }
    @{ n = 'UW_ACIDFILL_C'; src = 'wc'; c = 22; r =  0 }
    @{ n = 'UW_ACIDFILL_D'; src = 'wc'; c = 22; r =  0 }

    # Stone stairs for climbable high-area passes (Ground_rocks cols 18-19, rows 47-49)
    @{ n = 'UW_STAIRS_TL';  src = 'gr'; c = 18; r = 47 }
    @{ n = 'UW_STAIRS_TR';  src = 'gr'; c = 19; r = 47 }
    @{ n = 'UW_STAIRS_ML';  src = 'gr'; c = 18; r = 48 }
    @{ n = 'UW_STAIRS_MR';  src = 'gr'; c = 19; r = 48 }
    @{ n = 'UW_STAIRS_BL';  src = 'gr'; c = 18; r = 49 }
    @{ n = 'UW_STAIRS_BR';  src = 'gr'; c = 19; r = 49 }
)
$imgGr = Get-PixelData $UwGroundPng
$imgWc = Get-PixelData $UwWaterPng
foreach ($t in $UwTileCells) {
    $img = if ($t.src -eq 'wc') { $imgWc } else { $imgGr }
    $idx = Get-IndexArray $img ($t.c * $TILE) ($t.r * $TILE) $TILE $TILE
    Add-Sprite $t.n $idx $TILE $TILE 0 0
}
if (-not $Quiet) { Write-Host ("  uw tiles    {0} curated cells baked" -f $UwTileCells.Count) }

# ---- Underworld decorations -----------------------------------------------
$UwObjects = @(
    @{ n = 'UW_TREE_1';     f = 'Dead_tree_shadow1_1.png' }
    @{ n = 'UW_TREE_2';     f = 'Dead_tree_shadow1_2.png' }
    @{ n = 'UW_TREE_3';     f = 'Tree_shadow1_1.png' }
    @{ n = 'UW_PINE_1';     f = 'Broken_tree_shadow1_4.png' }
    @{ n = 'UW_PINE_2';     f = 'Broken_tree_shadow1_6.png' }
    @{ n = 'UW_PINE_3';     f = 'Broken_tree_shadow1_7.png' }
    @{ n = 'UW_BUSH_1';     f = 'Thorn_plant_shadow1_3.png' }
    @{ n = 'UW_BUSH_2';     f = 'Thorn_plant_shadow1_2.png' }
    @{ n = 'UW_BUSH_3';     f = 'Thorn_plant_shadow1_1.png' }
    @{ n = 'UW_LOG_1';      f = 'Broken_tree_shadow1_4.png' }
    @{ n = 'UW_LOG_2';      f = 'Broken_tree_shadow1_5.png' }
    @{ n = 'UW_LOG_3';      f = 'Broken_tree_shadow1_6.png' }
    @{ n = 'UW_LOG_4';      f = 'Broken_tree_shadow1_7.png' }
    @{ n = 'UW_ROCKPROP_1'; f = 'Rock_shadow1_1.png' }
    @{ n = 'UW_ROCKPROP_2'; f = 'Rock_shadow1_2.png' }
    @{ n = 'UW_ROCKPROP_3'; f = 'Rock_shadow1_3.png' }
    @{ n = 'UW_STONE_1';    f = 'Grave_shadow1_1.png' }
    @{ n = 'UW_STONE_2';    f = 'Grave_shadow1_2.png' }
    @{ n = 'UW_STONE_3';    f = 'Grave_shadow1_3.png' }
    @{ n = 'UW_CRYSTAL_1';  f = 'Crystal_shadow1_1.png' }
    @{ n = 'UW_CRYSTAL_2';  f = 'Crystal_shadow1_2.png' }
    @{ n = 'UW_CRYSTAL_3';  f = 'Crystal_shadow1_3.png' }
    @{ n = 'UW_CRYSTAL_4';  f = 'Crystal_shadow1_4.png' }
    @{ n = 'UW_TUFT_1';     f = 'Bones_shadow1_2.png' }
    @{ n = 'UW_TUFT_2';     f = 'Bones_shadow1_18.png' }
    @{ n = 'UW_TUFT_3';     f = 'Bones_shadow1_16.png' }
    @{ n = 'UW_TUFT_4';     f = 'Bones_shadow1_5.png' }
    @{ n = 'UW_REED_1';     f = 'Bones_shadow1_1.png' }
    @{ n = 'UW_REED_2';     f = 'Bones_shadow1_3.png' }
    @{ n = 'UW_REED_3';     f = 'Rock_shadow1_4.png' }
)
foreach ($o in $UwObjects) {
    $path = Join-Path $UwObjectsDir $o.f
    $img = Get-PixelData $path
    $box = Get-OpaqueBox $img 0 0 $img.W $img.H
    if ($null -eq $box) { throw ("bake: {0} is fully transparent" -f $o.f) }
    $idx = Get-IndexArray $img $box.X $box.Y $box.W $box.H
    Add-Sprite $o.n $idx $box.W $box.H ([int][Math]::Floor($box.W / 2)) $box.H
}
if (-not $Quiet) { Write-Host ("  uw objects  {0} decorations, one PNG each" -f $UwObjects.Count) }

# ---- Portal ---------------------------------------------------------------
# 96x64 = 3x2 grid of 32x32 frames, a looping swirl animation. Baked as plain
# decoration-anchor sprites (bottom-centre of each frame's own trimmed box) -
# not the character sheet's cell-relative convention, since this is a small
# fixed-size prop, not a walk cycle whose stride matters.
$PORTAL_CELL = 32
$imgPortal = Get-PixelData $PortalPng
$portalNames = @('UW_PORTAL_A', 'UW_PORTAL_B', 'UW_PORTAL_C', 'UW_PORTAL_D', 'UW_PORTAL_E', 'UW_PORTAL_F')
for ($f = 0; $f -lt 6; $f++) {
    $pc = $f % 3; $pr = [int][Math]::Floor($f / 3)
    $box = Get-OpaqueBox $imgPortal ($pc * $PORTAL_CELL) ($pr * $PORTAL_CELL) $PORTAL_CELL $PORTAL_CELL
    if ($null -eq $box) { throw ("bake: portal frame {0} is empty" -f $f) }
    $idx = Get-IndexArray $imgPortal ($pc * $PORTAL_CELL + $box.X) ($pr * $PORTAL_CELL + $box.Y) $box.W $box.H
    Add-Sprite $portalNames[$f] $idx $box.W $box.H ([int][Math]::Floor($box.W / 2)) $box.H
}
if (-not $Quiet) { Write-Host "  portal      6 frames" }

# ---- Lumiara tiles (biome 3) -----------------------------------------------
#
# Unlike Ground_rocks.png/Water_coasts.png (Underworld's vendor mega-sheets,
# not laid out as autotile pieces at all), each Lumiara sheet IS a clean 4x4
# grid of sixteen fully-opaque 16x16 cells - verified by a full-sheet scan,
# every cell 256/256 px opaque, no exceptions. But the CONTENT is still not
# the engine's 3x3 directional blob format: each sheet is one painterly scene
# (an irregular terrain-B shape scattered over a terrain-A background) cut
# into a grid for delivery, not sixteen individually-authored edge pieces. So
# specific cells are curated by hand exactly the way Underworld's are (see
# that block above) - a handful of "pure A", "pure B" and "boundary" cells,
# with the boundary ones reused across every blob_slice slot rather than
# matched to a real direction, same simplification Underworld's
# grass_edge/olive_edge/water_edge/rock_ring already document.
#
# Three sheets, three GT_* roles, matching the plan's three transition pairs:
#   tileset_grass_to_cobblestone -> ground_base (grass) + dirt_fill (cobble)
#   tileset_water_to_grass       -> water_fill + water_edge/water_cap border
#   tileset_chasm_to_grass       -> rock_fill (void) + rock_ring border
# GT_ROCK reads as "Void Chasm" here, not stone outcrop - the same hollow-ring
# hazard shape Underworld's toxic rock wall reuses, just re-skinned. No new
# GT_* value and no new TileSet field (the plan's "cobble_edge" does not map
# to any existing render_world pass - see main.c's tile_lum_* comment): only
# rendering is biome-specific, per CLAUDE.md's Collision vs Render rule.
$imgLumCobble = Get-PixelData $LumCobblePng
$imgLumWater  = Get-PixelData $LumWaterPng
$imgLumChasm  = Get-PixelData $LumChasmPng
function Get-LumTileImg([string]$src) {
    if ($src -eq 'cobble') { return $imgLumCobble }
    if ($src -eq 'water')  { return $imgLumWater }
    return $imgLumChasm
}
$LumTileCells = @(
    @{ n = 'LUM_GRASS_A';       src = 'cobble'; c = 0; r = 0 }
    @{ n = 'LUM_GRASS_B';       src = 'cobble'; c = 3; r = 0 }
    @{ n = 'LUM_GRASS_C';       src = 'cobble'; c = 0; r = 3 }
    @{ n = 'LUM_GRASS_D';       src = 'cobble'; c = 3; r = 3 }
    # Cobble/water/void picks are the CALMEST cells of their region of each
    # sheet, chosen by a full 16-cell dither scan rather than by eye. The
    # first pass here picked c2,r1 for cobble (44.7, the single noisiest cell
    # in that sheet) and c2,r0/c2,r3 for water - both of which carry a big
    # flower motif that tiled into an obvious repeating grid across the
    # water. The calm interior cells below cost nothing and fix both.
    @{ n = 'LUM_COBBLE_A';      src = 'cobble'; c = 1; r = 2 }
    @{ n = 'LUM_COBBLE_B';      src = 'cobble'; c = 2; r = 2 }
    @{ n = 'LUM_GRASSEDGE';     src = 'cobble'; c = 2; r = 0 }
    @{ n = 'LUM_OLIVEEDGE';     src = 'cobble'; c = 1; r = 3 }
    @{ n = 'LUM_WATER_A';       src = 'water';  c = 2; r = 1 }
    @{ n = 'LUM_WATER_B';       src = 'water';  c = 1; r = 1 }
    @{ n = 'LUM_WATER_C';       src = 'water';  c = 2; r = 2 }
    @{ n = 'LUM_WATER_D';       src = 'water';  c = 3; r = 1 }
    @{ n = 'LUM_WBORDER_NW';    src = 'water';  c = 0; r = 0 }
    @{ n = 'LUM_WBORDER_N';     src = 'water';  c = 0; r = 2 }
    @{ n = 'LUM_WBORDER_NE';    src = 'water';  c = 3; r = 0 }
    @{ n = 'LUM_VOID_A';        src = 'chasm';  c = 2; r = 1 }
    @{ n = 'LUM_VOID_B';        src = 'chasm';  c = 1; r = 1 }
    @{ n = 'LUM_VOIDBORDER_NW'; src = 'chasm';  c = 0; r = 0 }
    @{ n = 'LUM_VOIDBORDER_N';  src = 'chasm';  c = 3; r = 0 }
    @{ n = 'LUM_VOIDBORDER_NE'; src = 'chasm';  c = 3; r = 3 }
)

# ---- Lumiara decorations ---------------------------------------------------
# Every file here is its own pre-cropped PNG (like Underworld's
# Objects_separately), so no curated-rect bookkeeping is needed. Landmark
# entities (tree, monolith) come from TopDown/; curated props and "ethereal
# fauna" come from the flat assets/Lumiara/ files.
#
# LUM_PORTAL is the odd one out and is last on purpose: it is NOT a prop and
# no prop_art slot references it. It is the Area 2 -> Area 3 gate, drawn by
# props_build's own portal branch, and it lives in this list only because the
# list is already the "one PNG, decoration anchor, through the Lumiara
# palette" path - which is exactly what a portal standing on a tile needs.
# Being appended AFTER LUM_STAG keeps draw_atlas's decoration sweep
# (ART_LUM_TREE..ART_LUM_STAG) unchanged.
#
# Deliberately excluded: every ISOMETRIC source has been retired to
# Lumiara\_isometric_archive\ (dreamgate_portal, mana_crystal_monolith and the
# seven tile_*_block cubes) - they were drawn in 2:1 dimetric projection with a
# diamond footprint and would read as tilted in a top-down world. The TopDown\
# variants are what this list bakes. dream_tree_large stays in place: it is a
# front elevation, not isometric, but it is a higher-resolution duplicate of
# topdown_dream_tree - the same class of exclusion as Underworld's
# Lich_shadow*/Ruin_shadow* (no slot needs a second copy).
#
# TopDown\topdown_stone_stairs.png and TopDown\topdown_waterfall_cliff.png are
# the top-down replacements for the archived stairs/waterfall cubes, which were
# the only two archived terrains the three Wang tilesets do NOT already cover.
# They are deliberately NOT in this list: no render pass in main.c draws them,
# and per CLAUDE.md a baked sprite with no caller is dead shipped bytes. Add
# them here in the same commit that adds the code that draws them.
$LumObjects = @(
    @{ n = 'LUM_TREE';      f = 'TopDown\topdown_dream_tree.png' }
    @{ n = 'LUM_MONOLITH';  f = 'TopDown\topdown_mana_monolith.png' }
    @{ n = 'LUM_BUSH';      f = 'flora_purple_mushrooms.png' }
    @{ n = 'LUM_MUSHROOM';  f = 'flora_crystal_flower.png' }
    @{ n = 'LUM_BENCH';     f = 'stone_bench_mossy.png' }
    @{ n = 'LUM_ARCHWAY';   f = 'archway_ruined_runic.png' }
    @{ n = 'LUM_STATUE';    f = 'statue_guardian_gargoyle.png' }
    @{ n = 'LUM_CHEST';     f = 'runic_chest.png' }
    @{ n = 'LUM_URN';       f = 'relic_urn.png' }
    @{ n = 'LUM_SIGNPOST';  f = 'signpost_wayfinding.png' }
    @{ n = 'LUM_LANTERN';   f = 'lantern_post_purple.png' }
    @{ n = 'LUM_BANNER';    f = 'banner_faded_kingdom.png' }
    @{ n = 'LUM_JELLYFISH'; f = 'fauna_dream_jellyfish.png' }
    @{ n = 'LUM_MANTA';     f = 'fauna_sky_manta.png' }
    @{ n = 'LUM_FOX';       f = 'fauna_spirit_fox.png' }
    @{ n = 'LUM_STAG';      f = 'fauna_star_stag.png' }
    @{ n = 'LUM_PORTAL';    f = 'TopDown\topdown_dreamgate_portal.png' }
)

# ---- Lumiara palette build ------------------------------------------------
# One histogram pass over EXACTLY the pixels the two loops below will bake
# (same source images, same regions), so the quantized map has an entry for
# every opaque pixel actually baked - a mismatch between this scan and the
# bake below would surface as a hard "no quantized mapping" throw from the
# tint closure rather than a silently wrong colour.
$LumHist = @{}
# Tile cells are calmed FIRST and the histogram is taken from the result, so
# the quantizer allocates its buckets to the colours that actually ship rather
# than to speckle that is about to be filtered away.
$LumTileToned = @{}
foreach ($t in $LumTileCells) {
    $cell = Get-CellRGBA (Get-LumTileImg $t.src) ($t.c * $TILE) ($t.r * $TILE) $TILE $TILE
    $cell = Invoke-LumGroundTone $cell
    $LumTileToned[$t.n] = $cell
    Get-CellHistogram $cell $LumHist
}
$LumObjImgs = @{}
foreach ($o in $LumObjects) {
    $path = Join-Path $LumDir $o.f
    $img = Get-PixelData $path
    $LumObjImgs[$o.n] = $img
    $box = Get-OpaqueBox $img 0 0 $img.W $img.H
    if ($null -eq $box) { throw ("bake: {0} is fully transparent" -f $o.f) }
    Get-OpaqueHistogram $img $box.X $box.Y $box.W $box.H $LumHist
}
$LumColorMap = Get-MedianCutMap $LumHist $LUM_PALETTE_BUDGET
$LumTint = {
    param($r, $g, $b)
    $key = "$r,$g,$b"
    if (-not $LumColorMap.ContainsKey($key)) {
        throw "bake: Lumiara pixel $key has no quantized mapping - histogram pass missed it"
    }
    $LumColorMap[$key]
}.GetNewClosure()
if (-not $Quiet) {
    Write-Host ("  lum palette {0} source colours -> {1} quantized (budget {2})" -f `
        $LumHist.Count, (($LumColorMap.Values | ForEach-Object { "$($_[0]),$($_[1]),$($_[2])" } | Sort-Object -Unique).Count), $LUM_PALETTE_BUDGET)
}

foreach ($t in $LumTileCells) {
    $idx = Get-IndexArrayFromCell $LumTileToned[$t.n] $LumColorMap
    Add-Sprite $t.n $idx $TILE $TILE 0 0
}
if (-not $Quiet) {
    Write-Host ("  lum tiles   {0} cells (calmed: smooth {1}, flatten {2}, dim {3}, desat {4})" -f `
        $LumTileCells.Count, $LUM_TILE_SMOOTH, $LUM_TILE_FLATTEN, $LUM_TILE_DIM, $LUM_TILE_DESAT)
}

foreach ($o in $LumObjects) {
    $img = $LumObjImgs[$o.n]
    $box = Get-OpaqueBox $img 0 0 $img.W $img.H
    $idx = Get-IndexArray $img $box.X $box.Y $box.W $box.H $LumTint
    Add-Sprite $o.n $idx $box.W $box.H ([int][Math]::Floor($box.W / 2)) $box.H
}
if (-not $Quiet) { Write-Host ("  lum objects {0} decorations, one PNG each" -f $LumObjects.Count) }

# --- character --- (must stay last - see the NOTE ON ORDER above)
$charLowestFoot = -1
foreach ($s in $CharSheets) {
    $path = Join-Path $CharDir $s.f
    $img = Get-PixelData $path
    if ($img.W -ne ($CharCellW * $CharFrames) -or $img.H -ne $CharCellH) {
        throw ("bake: {0} expected {1}x{2}, got {3}x{4}" -f $s.f, ($CharCellW*$CharFrames), $CharCellH, $img.W, $img.H)
    }
    for ($f = 0; $f -lt $CharFrames; $f++) {
        $box = Get-OpaqueBox $img ($f * $CharCellW) 0 $CharCellW $CharCellH
        if ($null -eq $box) { throw ("bake: {0} frame {1} is empty" -f $s.f, $f) }
        $bottom = $box.Y + $box.H - 1
        if ($bottom -gt $charLowestFoot) { $charLowestFoot = $bottom }
        if ($bottom -ge $CHAR_FOOT) {
            throw ("bake: {0} frame {1} has a foot pixel on row {2}, at or below CHAR_FOOT {3} - the ground line must sit below every frame" -f $s.f, $f, $bottom, $CHAR_FOOT)
        }
        $idx = Get-IndexArray $img (($f * $CharCellW) + $box.X) $box.Y $box.W $box.H
        # Cell-relative anchor - see the header. Every frame measures from the
        # same point in its cell, so the stride cannot wander.
        Add-Sprite ("CH_{0}_{1}" -f $s.n, $f) $idx $box.W $box.H ($CHAR_CX - $box.X) ($CHAR_FOOT - $box.Y)
    }
}
if (-not $Quiet) {
    Write-Host ("  character   {0} sheets x {1} frames; lowest foot pixel row {2}, ground line {3}" -f $CharSheets.Count, $CharFrames, $charLowestFoot, $CHAR_FOOT)
}

# ---- Emit ---------------------------------------------------------------
function Format-ByteArray($list) {
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $list.Count; $i++) {
        if (($i % 16) -eq 0) { [void]$sb.Append("`n   ") }
        [void]$sb.Append(("{0,3}," -f $list[$i]))
    }
    return $sb.ToString()
}

$palBytes = New-Object System.Collections.Generic.List[byte]
foreach ($c in $script:PalList) {
    $palBytes.Add([byte]$c[0]); $palBytes.Add([byte]$c[1]); $palBytes.Add([byte]$c[2])
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("/* GENERATED by tools/bake.ps1 from assets/. Do not edit by hand.")
[void]$sb.AppendLine(" *")
[void]$sb.AppendLine(" * One global palette; index 0 is transparent in every sprite.")
[void]$sb.AppendLine(" * RLE control byte C:")
[void]$sb.AppendLine(" *     C <  0x80  -> RUN     of (C + 1) px of the following index")
[void]$sb.AppendLine(" *     C >= 0x80  -> LITERAL of ((C & 0x7F) + 1) px, then that many indices")
[void]$sb.AppendLine(" * Row-major over the sprite box; runs may cross row boundaries.")
[void]$sb.AppendLine(" *")
[void]$sb.AppendLine(" * Anchors: tiles (0,0) top-left; decorations bottom-centre of the opaque")
[void]$sb.AppendLine(" * box; character frames cell-relative to (24, 44) of their 48x64 cell.")
[void]$sb.AppendLine(" */")
[void]$sb.AppendLine("#ifndef WAYFARER_ART_DATA_H")
[void]$sb.AppendLine("#define WAYFARER_ART_DATA_H")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#define ART_SPRITE_COUNT $($records.Count)")
[void]$sb.AppendLine("#define ART_CHAR_CX      $CHAR_CX")
[void]$sb.AppendLine("#define ART_CHAR_FOOT    $CHAR_FOOT")
[void]$sb.AppendLine("#define ART_TILE_PX      $TILE")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("enum {")
for ($i = 0; $i -lt $records.Count; $i++) {
    [void]$sb.AppendLine(("    ART_{0} = {1}," -f $records[$i].Name, $i))
}
[void]$sb.AppendLine("    ART_NONE = -1")
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("typedef struct {")
[void]$sb.AppendLine("    unsigned short w, h;               /* sprite box */")
[void]$sb.AppendLine("    unsigned short anchor_x, anchor_y;  /* see header for the two conventions */")
[void]$sb.AppendLine("    unsigned int   data_off, data_len;  /* bytes into ART_DATA */")
[void]$sb.AppendLine("} ArtSprite;")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("static const ArtSprite ART_SPRITES[ART_SPRITE_COUNT] = {")
foreach ($r in $records) {
    [void]$sb.AppendLine(("    {{ {0,3}, {1,3}, {2,3}, {3,3}, {4,6}, {5,5} }}, /* {6} */" -f `
        $r.W, $r.H, $r.AX, $r.AY, $r.Off, $r.Len, $r.Name))
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("/* Tileset grid -> sprite id, ART_NONE where the source cell is empty. The")
[void]$sb.AppendLine(" * enum order skips empty cells, so index != row*8+col; this is the only")
[void]$sb.AppendLine(" * thing that knows where a tile sits in the authored sheet. */")
[void]$sb.AppendLine("#define ART_TILE_COLS $TilesCols")
[void]$sb.AppendLine("#define ART_TILE_ROWS $TilesRows")
[void]$sb.AppendLine("static const short ART_TILE_AT[ART_TILE_ROWS][ART_TILE_COLS] = {")
for ($r = 0; $r -lt $TilesRows; $r++) {
    $row = @()
    for ($c = 0; $c -lt $TilesCols; $c++) { $row += ("{0,4}" -f $tileAt[$r, $c]) }
    [void]$sb.AppendLine(("    {{{0} }}, /* row {1} */" -f ($row -join ','), $r))
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#define ART_PAL_N     $($script:PalList.Count)")
[void]$sb.AppendLine("#define ART_PAL_BYTES $($palBytes.Count)")
[void]$sb.AppendLine("static const unsigned char ART_PAL[ART_PAL_BYTES] = {" + (Format-ByteArray $palBytes))
[void]$sb.AppendLine("")
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#define ART_DATA_BYTES $($dataBytes.Count)")
[void]$sb.AppendLine("static const unsigned char ART_DATA[ART_DATA_BYTES] = {" + (Format-ByteArray $dataBytes))
[void]$sb.AppendLine("")
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("#endif /* WAYFARER_ART_DATA_H */")

[System.IO.File]::WriteAllText($OutFile, $sb.ToString(), (New-Object System.Text.ASCIIEncoding))

# ---- Report -------------------------------------------------------------
# Every count prints, including the zeros. A quiet zero is the failure mode: if a
# future delivery has soft alpha edges, or the dedupe silently stops matching,
# the only signal is a number that used to be different.
$recBytes = $records.Count * 16
$total = $dataBytes.Count + $recBytes + $palBytes.Count
$ratio = 0.0
if ($dataBytes.Count -gt 0) { $ratio = $script:RawPx / $dataBytes.Count }
Write-Host ""
Write-Host ("BAKE OK    {0}" -f $OutFile) -ForegroundColor Green
Write-Host ("sprites    {0}  ({1} tiles + {2} decorations + {3} character frames)" -f `
    $records.Count, $tileCount, $Decor.Count, ($CharSheets.Count * $CharFrames))
Write-Host ("palette    {0,7:N0} bytes  ({1} colours, global)" -f $palBytes.Count, $script:PalList.Count)
Write-Host ("rle data   {0,7:N0} bytes  (from {1:N0} trimmed px = {2:N2}x)" -f $dataBytes.Count, $script:RawPx, $ratio)
Write-Host ("records    {0,7:N0} bytes  ({1} x 16)" -f $recBytes, $records.Count)
Write-Host ("total      {0,7:N0} bytes of const data" -f $total)
Write-Host ("dedupe     {0} sprite(s) share an identical earlier pixel stream" -f $script:DedupeHits)
Write-Host ("alpha      {0} partial-alpha pixel(s) (expected 0: this art set is binary alpha)" -f $script:PartialAlpha)
