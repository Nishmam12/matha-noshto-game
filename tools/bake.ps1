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
$DecorPng   = Join-Path $ASSETS 'Fantasy Forest\Decorations\Decorations.png'
$CharDir    = Join-Path $ASSETS 'Character'

# ---- Underworld (biome 2) source paths -----------------------------------
$UwGroundPng  = Join-Path $ASSETS 'Underworld\PNG\Ground_rocks.png'
$UwWaterPng   = Join-Path $ASSETS 'Underworld\PNG\Water_coasts.png'
$UwObjectsDir = Join-Path $ASSETS 'Underworld\PNG\Objects_separately'
$PortalPng    = Join-Path $ASSETS 'Portal\Dimensional_Portal.png'

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
    @{ n = 'UW_ACID_NW';    src = 'wc'; c = 0; r = 0 }
    @{ n = 'UW_ACID_N';     src = 'wc'; c = 1; r = 0 }
    @{ n = 'UW_ACID_NE';    src = 'wc'; c = 3; r = 0 }
    @{ n = 'UW_FLOOR_A';    src = 'gr'; c = 6; r = 21 }
    @{ n = 'UW_FLOOR_B';    src = 'gr'; c = 8; r = 21 }
    @{ n = 'UW_RUBBLE_A';   src = 'wc'; c = 0; r = 4 }
    @{ n = 'UW_RUBBLE_B';   src = 'wc'; c = 2; r = 4 }
    @{ n = 'UW_FLOOR_C';    src = 'gr'; c = 4; r = 21 }
    @{ n = 'UW_FLOOR_D';    src = 'gr'; c = 9; r = 21 }
    @{ n = 'UW_RUBBLE_C';   src = 'gr'; c = 2; r = 21 }
    @{ n = 'UW_RUBBLE_D';   src = 'gr'; c = 3; r = 21 }
    @{ n = 'UW_ROCKWALL_A'; src = 'gr'; c = 0; r = 21 }
    @{ n = 'UW_ROCKWALL_B'; src = 'gr'; c = 1; r = 21 }
)
$imgGr = Get-PixelData $UwGroundPng
$imgWc = Get-PixelData $UwWaterPng
foreach ($t in $UwTileCells) {
    $img = if ($t.src -eq 'wc') { $imgWc } else { $imgGr }
    $idx = Get-IndexArray $img ($t.c * $TILE) ($t.r * $TILE) $TILE $TILE
    Add-Sprite $t.n $idx $TILE $TILE 0 0
}
# The acid FILL (pond interior) has no ready-made opaque liquid tile in the
# source art - the hole centres are transparent by design, meant to show
# whatever is composited underneath. Rather than invent new pixels, the two
# floor tiles above are re-baked here through a fixed toxic-green tint, so
# acid reads as visibly distinct from plain floor at a glance (the same
# legibility concern CLAUDE.md's "ponds invisible on the minimap" note is
# about) while staying byte-for-byte reproducible from the same source rects.
$AcidTint = { param($r, $g, $b) @([int]($r * 0.55), [int][Math]::Min(255, $g * 0.95 + 40), [int]($b * 0.55)) }
$idx = Get-IndexArray $imgGr (6 * $TILE) (21 * $TILE) $TILE $TILE $AcidTint
Add-Sprite 'UW_ACIDFILL_A' $idx $TILE $TILE 0 0
$idx = Get-IndexArray $imgGr (8 * $TILE) (21 * $TILE) $TILE $TILE $AcidTint
Add-Sprite 'UW_ACIDFILL_B' $idx $TILE $TILE 0 0
$idx = Get-IndexArray $imgGr (4 * $TILE) (21 * $TILE) $TILE $TILE $AcidTint
Add-Sprite 'UW_ACIDFILL_C' $idx $TILE $TILE 0 0
$idx = Get-IndexArray $imgGr (9 * $TILE) (21 * $TILE) $TILE $TILE $AcidTint
Add-Sprite 'UW_ACIDFILL_D' $idx $TILE $TILE 0 0
if (-not $Quiet) { Write-Host ("  uw tiles    {0} curated cells + 4 tinted acid-fill variants" -f $UwTileCells.Count) }

# ---- Underworld decorations -----------------------------------------------
# Objects_separately ships each object as its OWN pre-cropped PNG (not a
# shared sheet), so no curated-rect overlap bookkeeping is needed - each file
# already is one object. 2-4 numbered poses are picked per family (from the
# 'shadow1' recolour; shadow2/shadow3 are near-identical recolours of the same
# poses, verified by near-identical file sizes) for genuine silhouette variety
# rather than picking three copies of the same pose. Lich_shadow* (reads as an
# NPC/enemy - no such system exists), Animation*.png (unrelated reference
# sheet), and Ruin_shadow*/Scull_door_shadow* (no PROP_* slot needs them - see
# UNDERWORLD_BIOME_PLAN.md discussion) are deliberately excluded.
$UwObjects = @(
    @{ n = 'UW_TREE_1';    f = 'Tree_shadow1_1.png' }
    @{ n = 'UW_TREE_2';    f = 'Tree_shadow1_2.png' }
    @{ n = 'UW_TREE_3';    f = 'Tree_shadow1_3.png' }
    @{ n = 'UW_PINE_1';    f = 'Broken_tree_shadow1_1.png' }
    @{ n = 'UW_PINE_2';    f = 'Broken_tree_shadow1_2.png' }
    @{ n = 'UW_PINE_3';    f = 'Broken_tree_shadow1_3.png' }
    @{ n = 'UW_BUSH_1';    f = 'Thorn_plant_shadow1_3.png' }
    @{ n = 'UW_BUSH_2';    f = 'Thorn_plant_shadow1_5.png' }
    @{ n = 'UW_BUSH_3';    f = 'Thorn_plant_shadow1_6.png' }
    @{ n = 'UW_LOG_1';     f = 'Dead_arm_shadow1_1.png' }
    @{ n = 'UW_LOG_2';     f = 'Dead_arm_shadow1_2.png' }
    @{ n = 'UW_LOG_3';     f = 'Dead_arm_shadow1_3.png' }
    @{ n = 'UW_LOG_4';     f = 'Dead_arm_shadow1_4.png' }
    @{ n = 'UW_ROCKPROP_1';f = 'Rock_shadow1_1.png' }
    @{ n = 'UW_ROCKPROP_2';f = 'Rock_shadow1_2.png' }
    @{ n = 'UW_ROCKPROP_3';f = 'Rock_shadow1_3.png' }
    @{ n = 'UW_STONE_1';   f = 'Bones_shadow1_1.png' }
    @{ n = 'UW_STONE_2';   f = 'Bones_shadow1_3.png' }
    @{ n = 'UW_STONE_3';   f = 'Bones_shadow1_13.png' }
    @{ n = 'UW_CRYSTAL_1'; f = 'Crystal_shadow1_1.png' }
    @{ n = 'UW_CRYSTAL_2'; f = 'Crystal_shadow1_2.png' }
    @{ n = 'UW_CRYSTAL_3'; f = 'Crystal_shadow1_3.png' }
    @{ n = 'UW_CRYSTAL_4'; f = 'Crystal_shadow1_4.png' }
    @{ n = 'UW_TUFT_1';    f = 'Bones_shadow1_2.png' }
    @{ n = 'UW_TUFT_2';    f = 'Bones_shadow1_18.png' }
    @{ n = 'UW_TUFT_3';    f = 'Bones_shadow1_16.png' }
    @{ n = 'UW_TUFT_4';    f = 'Bones_shadow1_5.png' }
    @{ n = 'UW_REED_1';    f = 'Grave_shadow1_1.png' }
    @{ n = 'UW_REED_2';    f = 'Grave_shadow1_2.png' }
    @{ n = 'UW_REED_3';    f = 'Grave_shadow1_3.png' }
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
