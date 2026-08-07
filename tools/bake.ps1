<#
    bake.ps1 - turn authored PNGs into a compiled-in C header.

    Wayfarer ships ZERO external files (contest rule, Handover.md section 1), so no PNG can be
    decoded at runtime. Art enters the executable exactly one way: this script reads the source
    PNGs at BUILD time and emits src/art_data.h, which is committed to the repo and compiled in.
    A clean build never has to run this - only re-run it when the art itself changes.

    Format, and why:

      * Per-sprite palette, exact colours, no quantisation. The Art Bible estimated <=16 entries
        per sprite; MEASURED, the real art runs 7-49 colours with a mean of 18, and 36 of the 93
        sprites exceed 16. So 4 bpp was never an option. 8-bit indices with a per-sprite palette
        keeps every authored colour instead of quantising toward a shared table and hoping the
        loss is invisible on pixel art.

      * Palette index 0 is ALWAYS transparent and is never a real colour. That makes the alpha
        test free at decode time.

      * Trimmed to the opaque bounding box. 71% of the authored canvas area is transparent, so
        trimming is the single biggest byte saving available, and it also makes the anchor honest:
        a sprite padded with empty rows would otherwise float above its own feet.

      * RLE over the index stream. Control byte C:
            C <  0x80  -> RUN     of (C + 1) pixels, next byte is the palette index
            C >= 0x80  -> LITERAL of ((C & 0x7F) + 1) pixels, then that many index bytes
        Runs cap at 128. A run of 2 costs 2 bytes against a literal's 3, so runs win from length
        2 upward; isolated pixels batch into literals.

    Output is written as plain ASCII with no BOM. Handover.md section 7 warns about encoding on
    anything this project generates; numeric arrays give no reason to risk it.
#>

[CmdletBinding()]
param(
    # Resolved in the body, not here: $PSScriptRoot is not yet populated while param defaults
    # are being evaluated under `powershell -File`.
    [string] $AssetRoot = "",
    [string] $OutFile   = "",

    # Only these subdirectories are baked. `generated/` is duplicates and sprite sheets.
    #
    # `magical/` holds 40 effect frames; only the 8 the renderer actually calls are baked, and
    # they are named one by one in $FxFrames below rather than by taking the whole folder.
    # Baking a sprite nothing draws is pure byte cost - the same rule that kept the bitmap font
    # at +0 shipping bytes until something called it.
    [string[]] $Categories = @("player", "nature", "buildings")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

# Decision 41. The four colours the bush's magenta base disc is drawn from, measured off the
# delivered PNG: 125 of its 163 magenta pixels sit in the bottom quarter, and no other delivered
# sprite has a base disc at all. Kept in sync BY TEST, not by discipline - src/main.c holds the
# same four and --sprite-test fails if one reaches a baked palette.
#
# If a future art delivery draws the disc in a different colour this list will silently strip
# nothing. That is why the strip count is PRINTED per sprite: a quiet zero is the signal.
$script:KeyMagenta = [System.Collections.Generic.HashSet[string]]::new(
    [string[]]@("116,48,94", "148,65,113", "94,39,81", "105,42,90"))
$script:TotalStripped = 0

# Phase 12 task 6. Which categories get a second, dream-realm palette emitted alongside the
# original. `nature` only: the dream sector has no buildings (place_buildings never samples those
# rows) and the player is the same person on both sides of the portal, so a recoloured character
# would read as a costume change rather than as a change of place.
$script:DreamCategories = @("nature")

# Phase 12 tasks 7 and 11. The effect frames with a caller in the renderer, named individually.
#
# Both fx_portal and fx_well are 16 authored frames, and this takes every SECOND one of each, so
# the baked set is a complete loop at half the rate rather than the first half of one. fx_crystal
# stays OUT: nothing in this phase draws it as a baked sprite - the dream realm's crystal
# decoration and the shard pickup are both procedural (draw_crystal, draw_shard) - and baking a
# sprite with no caller is pure byte cost, the rule that kept the bitmap font at +0 shipping bytes.
$script:FxFrames = @("fx_portal_0",  "fx_portal_2",  "fx_portal_4",  "fx_portal_6",
                     "fx_portal_8",  "fx_portal_10", "fx_portal_12", "fx_portal_14",
                     "fx_well_0",    "fx_well_2",    "fx_well_4",    "fx_well_6",
                      "fx_well_8",    "fx_well_10",   "fx_well_12",   "fx_well_14")

# Phase 13: bake only what has a caller; dungeon/interior sets stay source-only.
# Curated from the supplied packs — 13 dark-fantasy files actually referenced in
# src/main.c (AETHER_*), no castle placeholders.
$script:DarkFantasyFiles = @(
    "walls/wall_piece_06.png", "walls/wall_piece_07.png", "walls/wall_piece_08.png",
    "walls/wall_arch_01.png",
    "buildings/bld_gatehouse_large.png", "buildings/bld_keep_small.png",
    "buildings/bld_tower_round_ruined.png", "buildings/bld_tower_square_ruined.png",
    "buildings/bld_ruin_stone.png", "buildings/bld_chapel_stone.png",
    "props/prop_04.png", "props/prop_05.png", "props/prop_06.png", "props/prop_07.png")

# ------------------------------------------------------------ dream recolour --
# A dream sprite is the SAME pixel stream with a different palette: ArtSprite keeps pal_off
# separate from data_off, so a variant costs one 16-byte record plus its palette - about 70 bytes
# against ~1,700 to re-author the sprite. That is what makes the whole biome nearly free.
#
# The transform is a function of LUMINANCE ALONE, and that is load-bearing rather than lazy. Every
# output channel is monotonically increasing in the input luminance, so the recolour cannot
# reshuffle which of two colours is lighter - and fog_lerp's entire proven contract (--fog-test) is
# that the value hierarchy survives the blend. A hue rotation in RGB space would have no such
# guarantee and could silently invert a canopy ramp.
function ConvertTo-DreamColour {
    param([int] $r, [int] $g, [int] $b)
    $lum = (0.299 * $r + 0.587 * $g + 0.114 * $b)
    $t   = [Math]::Min(1.0, $lum / 200.0)
    $dr  = 0.42 * $lum + 78 * $t     # violet body
    $dg  = 0.30 * $lum + 40 * $t     # green pulled well down; this is what kills the "forest" read
    $db  = 0.72 * $lum + 96 * $t     # blue lifted hardest, so highlights climb toward lavender/cyan
    return @([int][Math]::Min(255, $dr), [int][Math]::Min(255, $dg), [int][Math]::Min(255, $db))
}

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrEmpty($AssetRoot)) { $AssetRoot = Join-Path $root "..\assets" }
if ([string]::IsNullOrEmpty($OutFile))   { $OutFile   = Join-Path $root "..\src\art_data.h" }

# ---------------------------------------------------------------- load a PNG --
# GetPixel is far too slow at this volume (93 sprites x up to 16k px). LockBits gives the whole
# surface as one BGRA byte array in a single marshalled copy.
function Get-PixelData {
    param([string] $Path)

    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $Path
    $rect = New-Object System.Drawing.Rectangle -ArgumentList 0, 0, $bmp.Width, $bmp.Height
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)

    $result = [pscustomobject]@{
        Width  = $bmp.Width
        Height = $bmp.Height
        Stride = $data.Stride
        Bytes  = $bytes      # BGRA, bottom-up or top-down per Stride sign; GDI+ gives top-down here
    }
    $bmp.Dispose()
    return $result
}

# ------------------------------------------------------------ encode one sprite --
function ConvertTo-Sprite {
    param([string] $Path, [string] $Name)

    $img = Get-PixelData -Path $Path
    $w = $img.Width; $h = $img.Height; $stride = $img.Stride; $b = $img.Bytes

    # Decision 41: knock out the authored magenta base disc, BEFORE the bounding box is measured.
    # Order matters. The disc sits under the bush's real base, so removing it afterwards would
    # leave the trimmed box - and therefore the bottom-centre anchor - sitting on a halo that is
    # no longer drawn, and the bush would float. Stripping first lets the anchor land on the art.
    #
    # An explicit colour list, not a "magenta-ish" test. The heuristic version of this flagged the
    # bridge's mauve stone (9C839C), a roof red (A51A35) and three purple-greys on the buildings;
    # those overlap the disc in RGB space and no threshold separates them. src/main.c's
    # art_is_key_magenta carries the same four colours as an independent second copy, and
    # --sprite-test fails if any of them ever reaches a baked palette.
    $stripped = 0
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $o = $row + $x * 4
            if ($b[$o + 3] -lt 128) { continue }
            if ($script:KeyMagenta.Contains("$($b[$o+2]),$($b[$o+1]),$($b[$o])")) {
                $b[$o + 3] = 0
                $stripped++
            }
        }
    }
    if ($stripped -gt 0) {
        Write-Host ("  key colour: stripped {0} px from {1}" -f $stripped, $Name)
        $script:TotalStripped += $stripped
    }

    # Opaque bounding box.
    $minX = $w; $minY = $h; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            if ($b[$row + $x * 4 + 3] -ge 128) {
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) { throw "$Name is fully transparent" }

    $tw = $maxX - $minX + 1
    $th = $maxY - $minY + 1

    # Palette (index 0 reserved for transparent) + index stream over the trimmed box.
    $palMap = @{}
    $pal = New-Object System.Collections.Generic.List[int[]]
    $idx = New-Object byte[] ($tw * $th)

    for ($y = 0; $y -lt $th; $y++) {
        $row = ($y + $minY) * $stride
        for ($x = 0; $x -lt $tw; $x++) {
            $o = $row + ($x + $minX) * 4
            if ($b[$o + 3] -lt 128) { $idx[$y * $tw + $x] = 0; continue }
            $key = "$($b[$o+2]),$($b[$o+1]),$($b[$o])"      # BGRA -> R,G,B
            if (-not $palMap.ContainsKey($key)) {
                if ($pal.Count -ge 254) { throw "$Name exceeds 254 palette entries" }
                $pal.Add(@([int]$b[$o+2], [int]$b[$o+1], [int]$b[$o]))
                $palMap[$key] = $pal.Count            # 1-based; 0 stays transparent
            }
            $idx[$y * $tw + $x] = [byte]$palMap[$key]
        }
    }

    # RLE.
    $out = New-Object System.Collections.Generic.List[byte]
    $n = $idx.Length
    $i = 0
    $lit = New-Object System.Collections.Generic.List[byte]

    function Flush-Literal {
        param($OutList, $LitList)
        while ($LitList.Count -gt 0) {
            $take = [Math]::Min(128, $LitList.Count)
            $OutList.Add([byte](0x80 -bor ($take - 1)))
            for ($k = 0; $k -lt $take; $k++) { $OutList.Add($LitList[$k]) }
            $LitList.RemoveRange(0, $take)
        }
    }

    while ($i -lt $n) {
        $v = $idx[$i]
        $run = 1
        while ($i + $run -lt $n -and $idx[$i + $run] -eq $v -and $run -lt 128) { $run++ }
        if ($run -ge 2) {
            Flush-Literal -OutList $out -LitList $lit
            $out.Add([byte]($run - 1))
            $out.Add($v)
            $i += $run
        } else {
            $lit.Add($v)
            $i++
        }
    }
    Flush-Literal -OutList $out -LitList $lit

    return [pscustomobject]@{
        Name     = $Name
        W        = $tw
        H        = $th
        # Ground-contact anchor: bottom-centre of the OPAQUE box, which is the convention
        # draw_tree/draw_prop already use (cx, by = feet). Deliberate and contractual - see
        # Phase 07 task 2; changing it later means re-baking everything.
        AnchorX  = [int][Math]::Floor($tw / 2)
        AnchorY  = $th
        Pal      = $pal
        Data     = $out
        SrcW     = $w
        SrcH     = $h
    }
}

# ------------------------------------------------------------------- gather --
$sprites = @()
foreach ($cat in $Categories) {
    $dir = Join-Path $AssetRoot $cat
    if (-not (Test-Path $dir)) { Write-Warning "missing category: $dir"; continue }
    foreach ($f in (Get-ChildItem $dir -Filter *.png | Sort-Object Name)) {
        $name = ($f.BaseName -replace '[^A-Za-z0-9]', '_').ToUpperInvariant()
        $sp = ConvertTo-Sprite -Path $f.FullName -Name $name
        $sp | Add-Member -NotePropertyName Category -NotePropertyValue $cat
        $sprites += $sp
    }
}

# The effect frames, by name rather than by folder - see $FxFrames. Order is the animation order,
# so ART_FX_PORTAL_0 .. _14 are contiguous ids and the renderer can index the run.
foreach ($fx in $script:FxFrames) {
    $path = Join-Path (Join-Path $AssetRoot "magical") "$fx.png"
    if (-not (Test-Path $path)) { throw "missing effect frame: $path" }
    $name = ($fx -replace '[^A-Za-z0-9]', '_').ToUpperInvariant()
    $sp = ConvertTo-Sprite -Path $path -Name $name
    $sp | Add-Member -NotePropertyName Category -NotePropertyValue "magical"
    $sprites += $sp
}

# The Aetherhold files are already authored at the project's isometric scale.
# Prefix their C names so they cannot collide with village art identifiers.
foreach ($rel in $script:DarkFantasyFiles) {
    $path = Join-Path (Join-Path $AssetRoot "dark_fantasy") $rel
    if (-not (Test-Path $path)) { throw "missing dark-fantasy asset: $path" }
    $leaf = [System.IO.Path]::GetFileNameWithoutExtension($rel)
    $name = ("AETHER_" + ($leaf -replace '[^A-Za-z0-9]', '_')).ToUpperInvariant()
    $sp = ConvertTo-Sprite -Path $path -Name $name
    $sp | Add-Member -NotePropertyName Category -NotePropertyValue "dark_fantasy"
    $sprites += $sp
}


if ($sprites.Count -eq 0) { throw "no sprites found under $AssetRoot" }

# ------------------------------------------------------------------- emit --
$palBytes  = New-Object System.Collections.Generic.List[byte]
$dataBytes = New-Object System.Collections.Generic.List[byte]
$records   = @()

$dreamCount = 0
foreach ($s in $sprites) {
    $dataOff = $dataBytes.Count
    foreach ($d in $s.Data) { $dataBytes.Add($d) }

    # Base record.
    $palOff = $palBytes.Count / 3
    foreach ($c in $s.Pal) { $palBytes.Add([byte]$c[0]); $palBytes.Add([byte]$c[1]); $palBytes.Add([byte]$c[2]) }
    $records += [pscustomobject]@{
        Name = $s.Name; W = $s.W; H = $s.H; AX = $s.AnchorX; AY = $s.AnchorY
        PalOff = [int]$palOff; PalN = $s.Pal.Count; DataOff = $dataOff; DataLen = $s.Data.Count
    }

    # Dream variant: a second record over the SAME $dataOff/$dataLen, with a recoloured palette.
    # Nothing about the pixels is duplicated, which is the whole point - see ConvertTo-DreamColour.
    if ($script:DreamCategories -contains $s.Category) {
        $palOff = $palBytes.Count / 3
        foreach ($c in $s.Pal) {
            $d = ConvertTo-DreamColour -r $c[0] -g $c[1] -b $c[2]
            $palBytes.Add([byte]$d[0]); $palBytes.Add([byte]$d[1]); $palBytes.Add([byte]$d[2])
        }
        $records += [pscustomobject]@{
            Name = "$($s.Name)_DREAM"; W = $s.W; H = $s.H; AX = $s.AnchorX; AY = $s.AnchorY
            PalOff = [int]$palOff; PalN = $s.Pal.Count; DataOff = $dataOff; DataLen = $s.Data.Count
        }
        $dreamCount++
    }
}

function Format-ByteArray {
    param([System.Collections.Generic.List[byte]] $Bytes, [int] $PerLine = 16)
    $sb = New-Object System.Text.StringBuilder
    for ($i = 0; $i -lt $Bytes.Count; $i++) {
        if ($i % $PerLine -eq 0) { [void]$sb.Append("`n   ") }
        [void]$sb.Append(("{0,3}," -f $Bytes[$i]))
    }
    return $sb.ToString()
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.Append(@"
/* GENERATED BY tools/bake.ps1 - DO NOT EDIT BY HAND.
 *
 * Regenerate with:  pwsh tools/bake.ps1     (or powershell -File tools/bake.ps1)
 *
 * Source art lives in assets/ and NEVER ships - zero external files may be shipped, so this
 * header is the only path from an authored PNG to a pixel on screen. See Phase 07 - Asset Seam.
 *
 * Palette index 0 is transparent in every sprite. RLE control byte:
 *     C <  0x80  -> RUN     of (C + 1) px of the following index
 *     C >= 0x80  -> LITERAL of ((C & 0x7F) + 1) px, then that many index bytes
 */

#ifndef WAYFARER_ART_DATA_H
#define WAYFARER_ART_DATA_H

"@)

[void]$sb.Append("#define ART_SPRITE_COUNT $($records.Count)`n`n")
[void]$sb.Append("enum {`n")
for ($i = 0; $i -lt $records.Count; $i++) {
    [void]$sb.Append("    ART_$($records[$i].Name) = $i,`n")
}
[void]$sb.Append("    ART_NONE = -1`n};`n`n")

[void]$sb.Append("typedef struct {`n")
[void]$sb.Append("    unsigned short w, h;          /* trimmed to the opaque box */`n")
[void]$sb.Append("    unsigned short anchor_x, anchor_y; /* ground-contact point: bottom-centre */`n")
[void]$sb.Append("    unsigned short pal_off, pal_n;     /* entries into ART_PAL */`n")
[void]$sb.Append("    unsigned int   data_off, data_len; /* bytes into ART_DATA */`n")
[void]$sb.Append("} ArtSprite;`n`n")

[void]$sb.Append("static const ArtSprite ART_SPRITES[ART_SPRITE_COUNT] = {`n")
foreach ($r in $records) {
    [void]$sb.Append(("    {{ {0,3},{1,3},{2,3},{3,3},{4,5},{5,3},{6,7},{7,6} }}, /* {8} */`n" -f `
        $r.W, $r.H, $r.AX, $r.AY, $r.PalOff, $r.PalN, $r.DataOff, $r.DataLen, $r.Name))
}
[void]$sb.Append("};`n`n")

[void]$sb.Append("#define ART_PAL_BYTES $($palBytes.Count)`n")
[void]$sb.Append("static const unsigned char ART_PAL[ART_PAL_BYTES] = {")
[void]$sb.Append((Format-ByteArray -Bytes $palBytes))
[void]$sb.Append("`n};`n`n")

[void]$sb.Append("#define ART_DATA_BYTES $($dataBytes.Count)`n")
[void]$sb.Append("static const unsigned char ART_DATA[ART_DATA_BYTES] = {")
[void]$sb.Append((Format-ByteArray -Bytes $dataBytes))
[void]$sb.Append("`n};`n`n")

[void]$sb.Append("#endif /* WAYFARER_ART_DATA_H */`n")

$dir = Split-Path -Parent $OutFile
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
# ASCII, no BOM - see the encoding note at the top of this file.
[System.IO.File]::WriteAllText($OutFile, $sb.ToString(), (New-Object System.Text.ASCIIEncoding))

# Over the SPRITES, not the records: a dream variant shares its predecessor's stream, so counting
# its pixels again would inflate the compression ratio with pixels that were never stored twice.
$rawPx = 0
foreach ($s in $sprites) { $rawPx += $s.W * $s.H }
"BAKE OK    $OutFile"
"sprites    $($records.Count)  ($($sprites.Count) streams + $dreamCount dream palette variants)"
"palette    $($palBytes.Count) bytes  ($($palBytes.Count / 3) entries)"
"rle data   $($dataBytes.Count) bytes  (from $rawPx trimmed px = {0:N1}x)" -f ($rawPx / [double]$dataBytes.Count)
"records    $($records.Count * 16) bytes"
"total      $($palBytes.Count + $dataBytes.Count + $records.Count * 16) bytes of const data"
