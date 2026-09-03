/* Wayfarer (top-down) - the whole game, one translation unit.
 *
 * Deliberately one file: the whole-program optimizer sees everything at once and
 * nothing is lost to translation-unit boundaries. A trade against readability
 * that a 1,440,000-byte budget justifies.
 *
 * We render into a 480x270 32-bit surface and integer-upscale into the window.
 * There is no SDL_Renderer and no texture anywhere: SDL's render subsystem is
 * compiled out of our SDL2 build (see build-sdl2.ps1), so pixels reach the
 * screen through SDL_GetWindowSurface + SDL_UpdateWindowSurface and nothing
 * else. Consequences worth knowing before changing anything here:
 *   - there is no vsync to lean on, so the frame cap at the bottom of main() is
 *     load-bearing rather than polite;
 *   - every drawing primitive writes Uint32 words into surface->pixels itself.
 *
 * Everything under WAYFARER_SELFTEST is verification scaffolding and is compiled
 * out of the shipping build. Release builds must never define it.
 */

#include <SDL.h>
#include "art_data.h"

/* The one non-SDL runtime header in the shipping build. SDL2 has no wall clock
 * that survives a process exit - SDL_GetTicks is since-init - and a save slot
 * list has to be able to say which save is the most recent one. time() is C89
 * and is the whole of what is used from here. */
#include <time.h>

#if WAYFARER_SELFTEST
#include <stdio.h>
#endif

/* Perf instrumentation follows the self-test gate, so it is structurally absent
 * from shipping rather than absent because someone remembered to remove it. */
#ifndef WAYFARER_PERF
#define WAYFARER_PERF WAYFARER_SELFTEST
#endif

/* ---- Geometry -----------------------------------------------------------
 *
 * We rasterise at LOGICAL_W x LOGICAL_H and integer-scale into the window. The
 * scaling is the point, not a shortcut: a 1 px highlight is a hairline at
 * native 1080p and a visible 4 px band when drawn at 480x270 and scaled x4.
 *
 * 480x270 is exactly 1920x1080 / 4, so the common desktop gets a pixel-exact
 * x4 with no letterbox, and it is within two rows of Mockup1.png's 480x272 -
 * i.e. it is the framing the art was drawn for.
 *
 * TILE is 16 because the delivered tileset is a grid of 16x16 tiles and every
 * sprite is authored against it. Unlike the isometric build there is no
 * tile-size knob and no PX() rescaling: art blits 1:1, so pixel constants in
 * this file are literal pixels. */
#define LOGICAL_W 480
#define LOGICAL_H 270
#define TILE      16
#define WIN_SCALE_MAX 6
#define WIN_TITLEBAR  40   /* desktop height to leave for a title bar */

/* Simulation runs fixed-step; rendering is capped separately. */
#define TICK_HZ  60.0f
#define TICK_DT  (1.0f / TICK_HZ)
#define FRAME_HZ 60.0

/* World px per second. 4.5 tiles/s crosses the 480 px view in ~6.7 s.
 * NEEDS A HUMAN: this and WALK_FPS are readability guesses, not measurements. */
#define PLAYER_SPEED 72.0f

/* ---- Perf --------------------------------------------------------------- */
#if WAYFARER_PERF
static Uint32 perf_px;
static Uint32 perf_calls;
#define PERF_COUNT(n) do { perf_px += (Uint32)(n); perf_calls++; } while (0)
#else
#define PERF_COUNT(n) ((void)0)
#endif

/* ---- Fatal diagnostics --------------------------------------------------
 *
 * The shipping binary is linked -mwindows: no console, no stdout, no stderr, no
 * log. Without this, a startup failure on someone else's machine looks like "I
 * double-clicked it and nothing happened" - undiagnosable by them and by us.
 *
 * MessageBoxA is hand-declared rather than reached through <windows.h>: that
 * header drops several hundred macros (min, max, near, far, ...) into a
 * single-translation-unit program, and exactly one function is wanted from it.
 * The fallback is not belt-and-braces: SDL_ShowSimpleMessageBox routes through
 * the video subsystem and returns -1 showing nothing when video is what failed,
 * which is the likeliest failure of all. */
#ifdef _WIN32
__declspec(dllimport) int __stdcall MessageBoxA(void *hWnd, const char *lpText,
                                               const char *lpCaption, unsigned int uType);
#define WF_MB_OK        0x00000000u
#define WF_MB_ICONERROR 0x00000010u
#endif

static int fatal(const char *what, int code)
{
    char msg[512];

    SDL_snprintf(msg, sizeof(msg), "%s\n\nSDL reported: %s", what, SDL_GetError());
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Wayfarer", msg, NULL) != 0) {
#ifdef _WIN32
        MessageBoxA(NULL, msg, "Wayfarer Fatal Error", WF_MB_OK | WF_MB_ICONERROR);
#else
        SDL_Log("Wayfarer Fatal Error: %s", msg);
#endif
    }
    return code;
}

/* ---- Pixel primitives --------------------------------------------------- */

/* Window surfaces are plain memory - never RLE-encoded - so SDL_MUSTLOCK is
 * false for them and no lock/unlock is needed. Caller guarantees 32bpp.
 * Clips, because things at the screen edge are partly off it.
 *
 * Shipping caller since phase 6: fragments and Found Souls have no authored
 * art and are drawn as motes. */
static void fill_rect(SDL_Surface *s, int x, int y, int w, int h, Uint32 colour)
{
    int iy, ix;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0)
        return;

    /* Counted after clipping, so this is real writes rather than requested
     * ones - an off-screen draw must not inflate the number. */
    PERF_COUNT(w * h);

    for (iy = 0; iy < h; iy++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (y + iy) * s->pitch);
        for (ix = 0; ix < w; ix++)
            row[x + ix] = colour;
    }
}

/* ---- Fog ----------------------------------------------------------------
 *
 * fog_lerp is the single path from a surface's true colour to its on-screen
 * colour. Compute Rec.601 luminance, pull each channel toward a light, cool
 * haze while KEEPING FOG_KEEP of the source's own luminance contrast, then lerp
 * from that fogged colour to the true colour by `reveal`.
 *
 * The contrast retention is the load-bearing part, not a refinement: a tree
 * canopy is four shades and a rock has three, so a blend that flattened
 * luminance would turn every prop into a silhouette. And the haze is LIGHT and
 * cool rather than dark - a dark blend was the measured cause of an early
 * "traversal feels suffocating" read in this project's predecessor. Do not
 * re-litigate either without looking at the screen.
 */
#define FOG_TINT_R 60.0f
#define FOG_TINT_G 70.0f
#define FOG_TINT_B 86.0f
#define FOG_KEEP   0.50f   /* fraction of luminance contrast surviving at reveal 0 */

/* The ramp is EASED rather than run linearly into the true colour.
 *
 * Reveal is linear in the thing that produces it - sight is a quadratic taper
 * over distance and restoration is a constant rate - but a linear ramp in the
 * BLEND spends its whole second half within touching distance of full chroma.
 * Ground is what that lands on hardest: this palette's grass is a saturated
 * chartreuse, and at half reveal a linear blend already reads as fully lit
 * grass, so the last fifteen levels have nothing left to give and arriving at a
 * restored region is a saturation pop rather than colour coming back.
 *
 * Weighted toward the quadratic, so early reveal stays hazy and the approach to
 * true colour is gradual. BOTH ENDPOINTS ARE FIXED BY CONSTRUCTION - 0 maps to
 * 0 and 1 maps to 1 - which is what keeps full reveal reproducing the palette
 * exactly, the invariant --fog-test asserts outright. Monotonic on [0,1]: the
 * derivative is FOG_EASE + 2(1-FOG_EASE)r, positive throughout.
 *
 * It lives INSIDE fog_lerp, not in fogpal_build, so the LUT and every direct
 * call agree by construction rather than by being kept in step. */
#define FOG_EASE   0.70f   /* linear share; 1 - this is the quadratic share */

static float fog_ease(float reveal)
{
    if (reveal < 0.0f) reveal = 0.0f;
    if (reveal > 1.0f) reveal = 1.0f;
    return reveal * (FOG_EASE + reveal * (1.0f - FOG_EASE));
}

static Uint32 fog_lerp(SDL_Surface *s, int r, int gr, int b, float reveal)
{
    float lum = 0.299f * (float)r + 0.587f * (float)gr + 0.114f * (float)b;
    float fr = FOG_TINT_R + (lum - FOG_TINT_R) * FOG_KEEP;
    float fg = FOG_TINT_G + (lum - FOG_TINT_G) * FOG_KEEP;
    float fb = FOG_TINT_B + (lum - FOG_TINT_B) * FOG_KEEP;

    reveal = fog_ease(reveal);

    /* ROUNDED, not truncated. A cast to Uint8 throws away up to a full 8-bit
     * step, and it throws it away in one direction - always downward - so the
     * error is a bias rather than noise. That is precisely what pulls two
     * near-equal luminances across an integer boundary in opposite directions
     * and inverts their order, which is the failure --fog-test exists to catch:
     * with truncation the worst inverting gap sat at 1.01, one step above where
     * the check calls a pair separable. Rounding halves the worst-case error
     * and removes the bias with it.
     *
     * Full reveal still reproduces the palette exactly - the blend already
     * lands within half a step of the true value there, so +0.5 truncates back
     * onto it rather than past it. --fog-test asserts that outright. */
    return SDL_MapRGB(s->format,
                      (Uint8)(fr + ((float)r  - fr) * reveal + 0.5f),
                      (Uint8)(fg + ((float)gr - fg) * reveal + 0.5f),
                      (Uint8)(fb + ((float)b  - fb) * reveal + 0.5f));
}

/* The whole point of a single global palette: with a fixed palette, the fogged
 * colour is a pure function of (palette index, reveal level), so the entire
 * blend collapses into a lookup table built once at startup. Every tile and
 * every sprite pixel is then an index read - there is NO per-draw fog
 * arithmetic anywhere in the renderer, where the isometric build paid for one
 * fog_lerp per palette entry per draw call.
 *
 * Heap rather than a file-scope array: 8,448 bytes of static would be 8,448
 * bytes of zeros in .data on PE/COFF under -fdata-sections, i.e. shipped. The
 * pointer costs 8.
 *
 * It cannot be const-folded at compile time because SDL_MapRGB depends on the
 * window's runtime pixel format, and hardcoding ARGB8888 is exactly the
 * format assumption this project fixed rather than inherited. */
#define FOG_LEVELS 32   /* reveal is a Uint8; reveal >> 3 indexes this exactly */
#define FOGPAL_N   (ART_PAL_N + 1)
static Uint32 *fogpal;  /* [FOG_LEVELS][FOGPAL_N]; index 0 of each row unused */

#define FOGPAL(level, idx) fogpal[(level) * FOGPAL_N + (idx)]

static int fogpal_build(SDL_Surface *fb)
{
    int lv, k;

    fogpal = (Uint32 *)SDL_malloc(sizeof(Uint32) * FOG_LEVELS * FOGPAL_N);
    if (!fogpal)
        return 0;
    for (lv = 0; lv < FOG_LEVELS; lv++) {
        /* Level FOG_LEVELS-1 must be exactly 1.0 or full reveal would never
         * reach the true colour, which is the sort of off-by-one that reads as
         * "the art looks slightly washed out" and gets blamed on the art. */
        float rev = (float)lv / (float)(FOG_LEVELS - 1);
        FOGPAL(lv, 0) = 0;  /* transparent: never sampled, never written */
        for (k = 0; k < ART_PAL_N; k++) {
            const unsigned char *c = &ART_PAL[k * 3];
            FOGPAL(lv, k + 1) = fog_lerp(fb, c[0], c[1], c[2], rev);
        }
    }
    return 1;
}

/* ---- Sprites ------------------------------------------------------------
 *
 * Blit a baked RLE sprite with its anchor at (cx, cy). `level` is a fog level
 * in [0, FOG_LEVELS). `fade` draws at half weight against what is already in
 * the framebuffer - which is how a prop standing in front of the player ghosts
 * without a second pass or a z-buffer. `flip` mirrors about the sprite's own
 * box, so the anchor lands on the same ground point either way.
 *
 * Takes a POINTER rather than an id specifically so --decode-test can hand it a
 * malformed local copy: ART_SPRITES lives in .rodata, and casting away const to
 * corrupt it would fault on a read-only page.
 *
 * The four clamps marked SEC are in the SHIPPING build on purpose. Clamp 0 is
 * first and is load-bearing: every other clamp is expressed relative to `n`, so
 * an `n` that is itself out of bounds makes the rest worthless. */
static void draw_sprite_sp(SDL_Surface *fb, const ArtSprite *sp, int cx, int cy,
                           int level, int fade, int flip)
{
    const Uint32 *pal;
    Uint32 rmask, gmask, bmask;
    unsigned int i, n;
    int x0, y0, x, y;

    if (!sp || !fogpal)
        return;
    if (level < 0) level = 0;
    if (level >= FOG_LEVELS) level = FOG_LEVELS - 1;
    pal = &FOGPAL(level, 0);

    rmask = fb->format->Rmask;
    gmask = fb->format->Gmask;
    bmask = fb->format->Bmask;

    x0 = cx - (int)sp->anchor_x;
    y0 = cy - (int)sp->anchor_y;

    /* Whole-sprite reject before touching the stream at all. */
    if (x0 >= fb->w || y0 >= fb->h || x0 + (int)sp->w <= 0 || y0 + (int)sp->h <= 0)
        return;

    i = sp->data_off;
    n = sp->data_off + sp->data_len;
    /* SEC clamp 0. The second test catches unsigned wraparound. */
    if (n > ART_DATA_BYTES || n < sp->data_off)
        n = ART_DATA_BYTES;
    x = 0;
    y = 0;

    while (i < n && y < (int)sp->h) {
        unsigned int c = ART_DATA[i++];
        unsigned int count, k;
        int literal = (c >= 0x80);
        unsigned char idx = 0;

        if (literal) {
            count = (c & 0x7Fu) + 1u;
            /* SEC clamp 2: a LITERAL whose declared count runs past this
             * sprite's slice would read into the next sprite's stream. */
            if (i + count > n)
                count = n - i;
        } else {
            count = c + 1u;
            /* SEC clamp 1: a RUN control byte as the final byte of the slice
             * leaves no index byte to read. */
            if (i >= n)
                break;
            idx = ART_DATA[i++];
        }

        for (k = 0; k < count && y < (int)sp->h; k++) {
            unsigned char v = literal ? ART_DATA[i + k] : idx;
            /* SEC clamp 3: fogpal holds FOGPAL_N entries, so a larger index
             * would read past the row. Index 0 is the guaranteed-transparent
             * entry, so folding a bad index to 0 makes malformed data draw a
             * HOLE - visible, harmless and reportable - rather than confetti. */
            if (v >= FOGPAL_N)
                v = 0;
            if (v) {
                int px = flip ? (x0 + (int)sp->w - 1 - x) : (x0 + x);
                int py = y0 + y;
                if (px >= 0 && py >= 0 && px < fb->w && py < fb->h) {
                    Uint32 *d = (Uint32 *)((Uint8 *)fb->pixels + py * fb->pitch + px * 4);
                    if (fade) {
                        /* Per-channel average in PACKED space. Summing inside a
                         * channel mask cannot carry into its neighbour (the
                         * widest sum is 2x the mask and the >>1 brings it back),
                         * and the final AND drops the half-bit the shift pushed
                         * below the channel. Exactly (src+dst)/2, no unpacking. */
                        Uint32 s = pal[v], o = *d;
                        *d = ((((s & rmask) + (o & rmask)) >> 1) & rmask)
                           | ((((s & gmask) + (o & gmask)) >> 1) & gmask)
                           | ((((s & bmask) + (o & bmask)) >> 1) & bmask);
                    } else {
                        *d = pal[v];
                    }
                }
            }
            if (++x >= (int)sp->w) { x = 0; y++; }
        }
        if (literal)
            i += count;
    }
    PERF_COUNT(sp->w * sp->h);
}

static void draw_sprite(SDL_Surface *fb, int id, int cx, int cy, int level)
{
    if (id < 0 || id >= ART_SPRITE_COUNT)
        return;
    draw_sprite_sp(fb, &ART_SPRITES[id], cx, cy, level, 0, 0);
}

/* Integer nearest-neighbour upscale, logical -> window, centred with the
 * leftover margin cleared. Nearest-neighbour and integer-only on purpose:
 * anything smoother would undo the hard pixel edges this exists to produce.
 *
 * Each source row is expanded once and the result memcpy'd down to the other
 * s-1 rows, so scaling vertically costs a linear copy rather than another pass
 * of per-pixel work. */
static void blit_scale(const SDL_Surface *src, SDL_Surface *dst, int s)
{
    int y, x, k, ox, oy, dw, dh;

    if (s < 1) s = 1;
    dw = src->w * s;
    dh = src->h * s;
    if (dst->w < dw || dst->h < dh)
        return;
    ox = (dst->w - dw) / 2;
    oy = (dst->h - dh) / 2;

    /* Margins only, not the whole surface: fullscreen is rarely an exact
     * multiple, and clearing 2.3M px to paint over almost all of them is waste.
     * Without this the border holds stale pixels. */
    if (oy > 0 || ox > 0) {
        for (y = 0; y < dst->h; y++) {
            Uint32 *row = (Uint32 *)((Uint8 *)dst->pixels + y * dst->pitch);
            if (y < oy || y >= oy + dh) {
                SDL_memset4(row, 0, (size_t)dst->w);
            } else if (ox > 0) {
                SDL_memset4(row, 0, (size_t)ox);
                SDL_memset4(row + ox + dw, 0, (size_t)(dst->w - ox - dw));
            }
        }
    }

    for (y = 0; y < src->h; y++) {
        const Uint32 *sp = (const Uint32 *)((const Uint8 *)src->pixels + y * src->pitch);
        Uint32 *d0 = (Uint32 *)((Uint8 *)dst->pixels + (oy + y * s) * dst->pitch) + ox;
        for (x = 0; x < src->w; x++) {
            Uint32 c = sp[x];
            for (k = 0; k < s; k++)
                d0[x * s + k] = c;
        }
        for (k = 1; k < s; k++)
            SDL_memcpy((Uint8 *)d0 + (size_t)k * dst->pitch, d0,
                       (size_t)dw * sizeof(Uint32));
    }
    PERF_COUNT(dw * dh);
}

/* ---- Presentation ------------------------------------------------------- */

static int pick_scale(void)
{
    SDL_Rect r;
    SDL_DisplayMode dm;
    int sx, sy, s;

    if (SDL_GetDisplayUsableBounds(0, &r) == 0 && r.w > 0 && r.h > 0) {
        sx = r.w / LOGICAL_W;
        sy = (r.h - WIN_TITLEBAR) / LOGICAL_H;
    } else if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        sx = dm.w / LOGICAL_W;
        sy = (dm.h - 80) / LOGICAL_H;
    } else {
        return 1;
    }
    s = sx < sy ? sx : sy;
    if (s < 1) s = 1;
    if (s > WIN_SCALE_MAX) s = WIN_SCALE_MAX;
    return s;
}

/* The logical framebuffer, in the WINDOW's pixel format - required so that a
 * colour packed once with SDL_MapRGB is directly storable in both surfaces.
 * Heap, not static: a static would be .data on PE/COFF under -fdata-sections
 * and ship its own zeros. */
static SDL_Surface *backbuffer_new(const SDL_Surface *win_fb, void **pixels)
{
    SDL_Surface *s;
    void *px = SDL_malloc((size_t)LOGICAL_W * LOGICAL_H * 4);

    *pixels = px;
    if (!px)
        return NULL;
    s = SDL_CreateRGBSurfaceWithFormatFrom(px, LOGICAL_W, LOGICAL_H, 32,
                                           LOGICAL_W * 4, win_fb->format->format);
    if (!s) {
        SDL_free(px);
        *pixels = NULL;
    }
    return s;
}

/* The scale is recomputed from the live surface every frame rather than
 * remembered, so toggling fullscreen - where the window stops being an exact
 * multiple of the logical size - needs no special case. */
static void present(SDL_Window *win, SDL_Surface *fb, SDL_Surface *back)
{
    if (back) {
        int sx = fb->w / back->w;
        int sy = fb->h / back->h;
        int s = sx < sy ? sx : sy;
        blit_scale(back, fb, s < 1 ? 1 : s);
    }
    SDL_UpdateWindowSurface(win);
}

/* ---- Argument parsing --------------------------------------------------- */

/* Ungated as of phase 7: --mute is a shipping flag, so this now has a caller in
 * both builds. It was guarded until it did, because an uncalled static is a
 * warning under -Wall AND dead shipped bytes. */
static int arg_flag(int argc, char **argv, const char *name)
{
    int i;
    for (i = 1; i < argc; i++)
        if (SDL_strcmp(argv[i], name) == 0)
            return 1;
    return 0;
}

static int arg_int(int argc, char **argv, const char *name, int fallback)
{
    int i;
    for (i = 1; i < argc - 1; i++)
        if (SDL_strcmp(argv[i], name) == 0)
            return SDL_atoi(argv[i + 1]);
    return fallback;
}

/* ---- World --------------------------------------------------------------
 *
 * 128x128 tiles = 2048x2048 px, roughly 18 screens. Ground type is the only
 * field phase 2 needs; collision, regions and reveal join it as their phases
 * land. Nothing here is `static` - see the stack guard below.
 */
#define WORLD_W 128
#define WORLD_H 128

enum { GT_GRASS = 0, GT_OLIVE, GT_DIRT, GT_WATER, GT_ROCK, GT_COUNT };

/* Three areas, one shared vocabulary. GT_* and TERRAIN_* stay biome-agnostic -
 * only rendering (render_world's tile tables) and props (prop_art) are
 * selected by biome, so world generation, gating and tile_blocked need no
 * biome branch at all. See LUMIARA_BIOME_PLAN.md for why. */
enum { BIOME_FOREST = 0, BIOME_UNDERWORLD = 1, BIOME_LUMIARA = 2, BIOME_COUNT };

/* ---- Regions and abilities ----------------------------------------------
 *
 * The world is partitioned into at most 32 connected regions (32 because `adj`
 * is a Uint32 bitmask), each carrying a terrain tag that names the ability
 * needed to ENTER it. Exactly three abilities, one bit each - no trees, no
 * levels, no scaling.
 */
#define REGION_COUNT 16
#define REGION_NONE  0xFF

enum { TERRAIN_NORMAL = 0, TERRAIN_WATER, TERRAIN_LEDGE, TERRAIN_DARK, TERRAIN_COUNT };
enum { ABIL_NONE = 0, ABIL_WADE = 1 << 0, ABIL_CLIMB = 1 << 1, ABIL_KINDLE = 1 << 2 };
#define ABIL_ALL (ABIL_WADE | ABIL_CLIMB | ABIL_KINDLE)

static const Uint8 terrain_requires[TERRAIN_COUNT] = {
    ABIL_NONE, ABIL_WADE, ABIL_CLIMB, ABIL_KINDLE
};

typedef struct {
    Uint8  terrain;    /* TERRAIN_* - the gate on entering */
    Uint16 tiles;
    int    seed_tile;  /* representative tile, for debug draw and reveal waves */
    Uint32 adj;        /* adjacency bitmask; caps REGION_COUNT at 32 */
    float  restoration; /* 0..1, eased toward restore_to; drives colour return */
    float  restore_to;
} Region;

/* ---- Fragments and Found Souls ------------------------------------------
 *
 * Restoration state is ONE Uint32 bitmask, which caps total collectibles across
 * the WHOLE GAME - not per area - at 32. Area 1 claims bits 0-9; later areas
 * take 10-31. That ceiling is structural, not a preference: it is what keeps
 * the save file 28 bytes and the completion test a single compare.
 */
#define FRAGMENT_COUNT 7
#define SOUL_COUNT     3
#define ENTITY_COUNT   (FRAGMENT_COUNT + SOUL_COUNT)   /* must stay <= 32 */
#define ENTITY_SPACING 8    /* min Chebyshev tiles between two entities */

typedef struct {
    int   tile;      /* -1 = unplaced */
    Uint8 region;
    Uint8 grants;    /* ABIL_* this fragment restores, or 0 */
    Uint8 is_soul;
    Uint8 restored;
} Entity;

typedef struct {
    Uint8 terr[WORLD_H][WORLD_W];    /* GT_* */
    Uint8 canopy[WORLD_H][WORLD_W];  /* 0-255 tree density; render-only */
    /* COLLISION INPUT. tile_blocked reads this and regions[].terrain, and
     * nothing else - not terr, not canopy, not any sprite identity. That single
     * rule is what makes a completability proof a re-run after a generation
     * change rather than a re-argument. */
    Uint8 solid[WORLD_H][WORLD_W];
    Uint8 region[WORLD_H][WORLD_W];  /* REGION_NONE where solid or unreachable */
    /* RENDER-ONLY, and deliberately a second array rather than a widening of
     * the one above: which region's restoration LIGHTS a tile, as opposed to
     * which region OWNS it for collision and reachability.
     *
     * They differ on exactly the tiles that cannot be walked on. `region` is
     * filled by bfs_open, which never leaves the open tiles, so every pond and
     * every rock - the whole border ring included - is REGION_NONE there, and a
     * REGION_NONE tile gets no restoration contribution at all. That is why a
     * restored area used to keep its ponds and its map edge grey while the
     * grass around them came back to full colour: the water was not fogged, it
     * was never eligible to be un-fogged.
     *
     * Derived, never authored: regions_light rebuilds it from `region` inside
     * regions_relink, so it cannot drift from the partition. Nothing that
     * decides what blocks her may read it - see the rule on the field above. */
    Uint8 litreg[WORLD_H][WORLD_W];
    /* Fog. 0 = unseen and colourless, 255 = fully restored. A Uint8 rather than
     * a float: it indexes the 32-level fog LUT directly as reveal >> 3, and
     * saves 48 KB of working set at 128x128. */
    Uint8 reveal[WORLD_H][WORLD_W];
    Region regions[REGION_COUNT];
    int region_count;
    int spawn_region;
    int spawn_tile;
    Uint8 biome;       /* BIOME_* - render/prop selector only, set by world_gen */
    int   portal_tile; /* Where this area's exit onward stands (unused in
                          * Lumiara - Area 3 is terminal) */
    /* Where this biome's map fragment lies, or -1 once it has been taken.
     * ONE field carries "is it still there", so the renderer, the interact
     * prompt and the interact key cannot disagree about it - and because
     * loading regenerates the world and then replays the deltas, clearing it
     * is exactly the delta a found map replays as. See world_place_map. */
    int   map_tile;
} World;

/* BFS working set. One struct so a caller allocates it once; far too big for a
 * stack frame at 128x128, so callers heap it. */
typedef struct {
    Uint8 seen[WORLD_W * WORLD_H];
    int   queue[WORLD_W * WORLD_H];
    int   dist[WORLD_W * WORLD_H];
    Uint8 owner[WORLD_W * WORLD_H];
} Scratch;

/* ---- Player -------------------------------------------------------------
 * Four authored facings. Diagonals resolve to the HORIZONTAL one because
 * left/right are the three-quarter views the artist drew, and they read better
 * in motion than a head-on up/down pose does. */
enum { FACE4_DOWN = 0, FACE4_RIGHT, FACE4_UP, FACE4_LEFT, FACE4_COUNT };

#define WALK_FRAMES 8
#define WALK_FPS   10.0f   /* NEEDS A HUMAN: nobody has watched this run yet */
#define IDLE_FPS    6.0f

typedef struct {
    float x, y;    /* ground-contact point, in world px - the sprite anchor */
    Uint8 abilities;  /* ABIL_* mask; the entire ability system */
    Uint8 facing;
    /* Walk phase in seconds. Advances only under movement intent and is NEVER
     * reset: that single choice is what holds the standing pose on the frame
     * the stride stopped on, instead of snapping back to a contact pose. */
    float anim;
    int   moving;
} Player;

static int facing4_from_intent(float sx, float sy)
{
    if (sx > 0.0f) return FACE4_RIGHT;
    if (sx < 0.0f) return FACE4_LEFT;
    if (sy < 0.0f) return FACE4_UP;
    if (sy > 0.0f) return FACE4_DOWN;
    return -1;   /* no intent: hold the last facing */
}

/* A pure function of the player plus, when idling, the world clock. The walk
 * frame deliberately does NOT read the clock: driving it from a world timer
 * would animate the character on the spot. */
static int player_sprite(const Player *p, float clock)
{
    int f = p->facing < FACE4_COUNT ? p->facing : FACE4_DOWN;
    int k;

    if (p->moving) {
        k = (int)(p->anim * WALK_FPS) % WALK_FRAMES;
        if (k < 0) k += WALK_FRAMES;
        return ART_CH_WALK_DOWN_0 + f * WALK_FRAMES + k;
    }
    k = (int)(clock * IDLE_FPS) % WALK_FRAMES;
    if (k < 0) k += WALK_FRAMES;
    return ART_CH_IDLE_DOWN_0 + f * WALK_FRAMES + k;
}

/* -fdata-sections on PE/COFF emits zero-initialised statics into .data, which
 * SHIPS. A world-sized static is therefore its own weight in literal zeros in
 * the executable - it cost this project's predecessor 39,648 bytes once. World
 * lives on the stack or the heap, and this typedef fails the BUILD rather than
 * the run if it ever grows past what a stack frame can hold. */
typedef char wayfarer_stack_guard[(sizeof(World) < 700 * 1024) ? 1 : -1];

/* Per-tile decoration hash: a SplitMix64 finaliser over (seed, tx, ty).
 *
 * Deliberately stateless, and deliberately NOT drawn from any generator RNG
 * stream. Which fill variant a tile picks therefore cannot perturb terrain
 * generation or entity placement, so every seeded result stays valid by
 * construction rather than by tracing the call graph to prove it. It is also
 * stable regardless of draw order and culling, so nothing crawls or flickers as
 * the camera moves. */
static Uint32 tile_hash(Uint64 seed, int tx, int ty)
{
    Uint64 h = seed * 0x9E3779B97F4A7C15ULL
             + (Uint64)(Uint32)tx * 0xBF58476D1CE4E5B9ULL
             + (Uint64)(Uint32)ty * 0x94D049BB133111EBULL;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27; h *= 0x94D049BB133111EBULL;
    return (Uint32)(h ^ (h >> 31));
}

/* ---- RNG ----------------------------------------------------------------
 *
 * PCG32. Chosen over xorshift because `inc` is a SEQUENCE SELECTOR: each
 * distinct odd value defines a different period-2^64 stream, so the generator's
 * streams are independent BY CONSTRUCTION rather than merely started at
 * different offsets in one shared sequence, where they can silently overlap.
 *
 * Stream ids are APPEND-ONLY. Renumbering one reshuffles every existing seed,
 * and every seeded test result with it. */
typedef struct { Uint64 state, inc; } Rng;

enum { STREAM_TERRAIN = 1, STREAM_ENTITIES = 2, STREAM_AUDIO = 3, STREAM_MAP = 4 };

static Uint32 rng_next(Rng *r)
{
    Uint64 old = r->state;
    Uint32 xorshifted, rot;

    r->state = old * 6364136223846793005ULL + r->inc;
    xorshifted = (Uint32)(((old >> 18) ^ old) >> 27);
    rot = (Uint32)(old >> 59);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

static void rng_seed(Rng *r, Uint64 seed, Uint64 stream)
{
    r->state = 0u;
    r->inc = (stream << 1) | 1u;   /* forced odd: an even inc degrades the period */
    (void)rng_next(r);
    r->state += seed;
    (void)rng_next(r);
}

static float rng_float(Rng *r) { return (float)(rng_next(r) >> 8) * (1.0f / 16777216.0f); }

/* Uniform in [-1, 1). Only the noise waveform uses this - see wave_sample. */
static float rng_bipolar(Rng *r) { return rng_float(r) * 2.0f - 1.0f; }

/* Rejection-sampled rather than a bare modulo: naive % bias would skew every
 * placement toward low indices, which is invisible until a fragment never
 * appears in the far half of the map. */
static Uint32 rng_below(Rng *r, Uint32 n)
{
    Uint32 threshold, v;

    if (n < 2) return 0;
    threshold = (0u - n) % n;
    for (;;) {
        v = rng_next(r);
        if (v >= threshold) return v % n;
    }
}

/* ---- Audio --------------------------------------------------------------
 *
 * A five-layer softsynth that grows as the area is restored: the world starts
 * on a bare drone and ends with the full chord. Layers unlock on restore counts
 * and never lock again, so the music is a readable progress bar you cannot look
 * away from.
 *
 * The whole thing runs on SDL's real-time audio thread against a hard deadline
 * (21.3 ms at 48 kHz / 1024 frames). Inside audio_cb there is no allocation, no
 * lock, no syscall and no unbounded loop. The game thread's ONLY channel to the
 * callback is SDL_atomic_t counters; every other byte of Audio is owned by the
 * callback and written nowhere else. Where a payload is needed alongside a flag
 * (rng_seed_req), the payload is written FIRST and the flag second, so the
 * callback can never observe a flag pointing at a half-written value.
 *
 * Musical output is a pure function of a sample counter, so two runs from the
 * same state render bit-identical audio - which is what --audio-test checks.
 */
#define AUDIO_RATE     48000
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES  1024   /* frames per callback; ~21.3 ms at 48 kHz */
#define TONE_HZ        440.0
#define TONE_AMP       0.20f
#define TWO_PI         6.283185307179586

typedef enum { W_SAW, W_SQUARE, W_SINE, W_NOISE } Wave;

#define NUM_LAYERS 5
enum { LAYER_BASE = 0, LAYER_STRINGS, LAYER_PAD, LAYER_BELLS, LAYER_VOICE };

/* 0.0f is a rest; legato layers hold the last note through it, plucked layers
 * let it ring out. Every table below writes a note only where it CHANGES, so a
 * held note is a run of zeros and the tables read as a score rather than as a
 * sampled signal. */
#define SYNTH_STEPS  16      /* Underworld and Lumiara: 16 x 0.5 s = an 8 s loop */
#define SYNTH_STEP_S 0.5f

/* ---- Fantasy Forest: "8bit-Scene - Do You Remember Me" -------------------
 *
 * TRANSCRIBED, not sampled. The source is a 96 s 48 kHz stereo WAV; at 4 bytes
 * a frame that is 18 MB against a 1.44 MB budget, and everything that fits -
 * 8 kHz, 4-bit ADPCM - throws away every harmonic above 4 kHz and the whole
 * layer-unlock design with it. So the track was ANALYSED and re-authored into
 * the five layers this engine already had, which costs ~2.5 KB of tables and
 * keeps both.
 *
 * What the analysis found (FFT peak-tracking per eighth-note, band-separated,
 * over the first 16 bars - tools are in the commit message, not the repo):
 *
 *   tempo   160 BPM, so an eighth note is 0.1875 s and a bar is 1.5 s
 *   key     Ab major - Ab Bb C Db Eb F G, and every note below is in it
 *   form    a 16-bar loop, 4 bars per chord: Bbm - Ab - Bbm - Cm  (ii-I-ii-iii)
 *   loop    128 eighths = 24.0 s, which is 96 / 4 - the source is four of these
 *
 * and the five bands map onto the five layers exactly as they stood:
 *
 *   BASE     60-190 Hz   the bass line, moving Bb2 - Ab2 - Bb2 - C3/G2
 *   STRINGS  190-400 Hz  the accompaniment, arpeggiating each chord's tones
 *   PAD      -           one sustained root per 4-bar chord (authored from the
 *                        bass, not extracted: the source's pad is masked by the
 *                        arpeggio and a peak tracker only ever found the latter)
 *   BELLS    800-1700 Hz the high sparkle, and SPARSE on purpose - the source
 *                        only plays it on about 40% of steps, and a pluck on
 *                        every eighth for 24 s is a rattle, not a shimmer
 *   VOICE    400-800 Hz  the lead melody
 *
 * Detections outside the key were rejected rather than transcribed: at these
 * amplitudes a peak tracker picks up intermodulation between the square-wave
 * layers, and a B natural in Ab major is that and not a note anybody played.
 *
 * HOW IT SOUNDS against the source is UNVERIFIED, per this file's convention
 * for anything only a real ear can settle. What is verified is structural and
 * checked by --audio-test: the pitches below, the 24.0 s loop, that every layer
 * re-articulates at the loop point, that the mix still never reaches the clamp,
 * and that the callback still makes its deadline at four times the step rate. */
#define FOREST_STEPS  128
#define FOREST_STEP_S 0.1875f    /* an eighth note at 160 BPM */

static const float SYNTH_BASE[FOREST_STEPS] = {   /* bass line */
     116.54f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  0 Bb2                              */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  1                                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  2                                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  3                                  */
     103.83f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  4 Ab2                              */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  5                                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  6                                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,   98.00f, /*  7                          G2 fill */
     116.54f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  8 Bb2                              */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  9                                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 10                                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 11                                  */
     130.81f,    0.0f,    0.0f,    0.0f,   98.00f,    0.0f,    0.0f,    0.0f, /* 12 C3          G2                   */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 13                                  */
     130.81f,    0.0f,    0.0f,    0.0f,    0.0f,   98.00f,    0.0f,    0.0f, /* 14 C3               G2              */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f  /* 15                                  */
};
static const float SYNTH_STRINGS[FOREST_STEPS] = {   /* chord-tone arpeggio */
     233.08f,  349.23f,    0.0f,    0.0f,    0.0f,    0.0f,  233.08f,    0.0f, /*  0 Bb3 F4          Bb3      */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  1                          */
        0.0f,    0.0f,  349.23f,    0.0f,    0.0f,    0.0f,    0.0f,  233.08f, /*  2      F4          Bb3     */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  3                          */
     207.65f,    0.0f,    0.0f,    0.0f,    0.0f,  311.13f,    0.0f,    0.0f, /*  4 Ab3       Eb4            */
     207.65f,    0.0f,  261.63f,    0.0f,  207.65f,    0.0f,    0.0f,    0.0f, /*  5 Ab3 C4    Ab3            */
        0.0f,    0.0f,  311.13f,    0.0f,  207.65f,    0.0f,    0.0f,    0.0f, /*  6      Eb4  Ab3            */
        0.0f,    0.0f,    0.0f,    0.0f,  277.18f,    0.0f,    0.0f,    0.0f, /*  7           Db4            */
        0.0f,    0.0f,  349.23f,    0.0f,    0.0f,    0.0f,    0.0f,  233.08f, /*  8      F4          Bb3     */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  9                          */
        0.0f,    0.0f,  349.23f,    0.0f,    0.0f,    0.0f,  233.08f,    0.0f, /* 10      F4       Bb3        */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 11                          */
     261.63f,    0.0f,    0.0f,    0.0f,  392.00f,    0.0f,    0.0f,    0.0f, /* 12 C4        G4             */
        0.0f,    0.0f,  311.13f,    0.0f,    0.0f,  261.63f,    0.0f,    0.0f, /* 13      Eb4     C4          */
        0.0f,    0.0f,  311.13f,    0.0f,  392.00f,    0.0f,    0.0f,    0.0f, /* 14      Eb4  G4             */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f  /* 15                          */
};
static const float SYNTH_PAD[FOREST_STEPS] = {   /* one sustained root per chord */
     233.08f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  0 Bb3 - Bbm */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  1 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  2 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  3 */
     207.65f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  4 Ab3 - Ab  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  5 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  6 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  7 */
     233.08f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  8 Bb3 - Bbm */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  9 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 10 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 11 */
     261.63f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 12 C4  - Cm  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 13 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 14 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f  /* 15 */
};
static const float SYNTH_BELLS[FOREST_STEPS] = {   /* the high sparkle - sparse */
     932.33f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  0 Bb5                  */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  1                      */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  2                      */
        0.0f,    0.0f,    0.0f,    0.0f, 1396.91f,    0.0f,    0.0f,    0.0f, /*  3         F6           */
    1244.51f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  830.61f, /*  4 Eb6              Ab5 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  932.33f, /*  5                  Bb5 */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  6                      */
        0.0f,    0.0f,    0.0f,    0.0f,  830.61f,    0.0f,    0.0f,    0.0f, /*  7         Ab5          */
        0.0f,    0.0f, 1396.91f,  932.33f,    0.0f,    0.0f,    0.0f,    0.0f, /*  8   F6 Bb5             */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  9                      */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 10                      */
        0.0f,    0.0f,    0.0f,    0.0f, 1396.91f,    0.0f,    0.0f,    0.0f, /* 11         F6           */
    1567.98f, 1046.50f,    0.0f,    0.0f,    0.0f,    0.0f,  830.61f,    0.0f, /* 12 G6 C6        Ab5     */
    1567.98f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, 1396.91f,    0.0f, /* 13 G6           F6      */
        0.0f,    0.0f, 1244.51f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 14      Eb6             */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f  /* 15                      */
};
static const float SYNTH_VOICE[FOREST_STEPS] = {   /* the lead melody */
     466.16f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  523.25f,    0.0f, /*  0 Bb4          C5      */
     554.37f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  1 Db5                  */
     466.16f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  523.25f,    0.0f, /*  2 Bb4          C5      */
     554.37f,    0.0f,    0.0f,    0.0f,  698.46f,    0.0f,    0.0f,    0.0f, /*  3 Db5      F5          */
     415.30f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  554.37f,    0.0f, /*  4 Ab4          Db5     */
     523.25f,    0.0f,    0.0f,    0.0f,    0.0f,  415.30f,  466.16f,    0.0f, /*  5 C5       Ab4 Bb4     */
     415.30f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  6 Ab4                  */
        0.0f,  523.25f,  783.99f,    0.0f,  554.37f,    0.0f,  523.25f,    0.0f, /*  7   C5 G5  Db5 C5      */
     466.16f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  523.25f,    0.0f, /*  8 Bb4          C5      */
     554.37f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /*  9 Db5                  */
     466.16f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,  523.25f,    0.0f, /* 10 Bb4          C5      */
     554.37f,    0.0f,    0.0f,    0.0f,  466.16f,    0.0f,    0.0f,    0.0f, /* 11 Db5      Bb4         */
     523.25f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 12 C5                   */
     783.99f,    0.0f,    0.0f,    0.0f,  523.25f,    0.0f,  698.46f,    0.0f, /* 13 G5       C5  F5      */
     523.25f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f, /* 14 C5                   */
        0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f,    0.0f  /* 15                      */
};

/* Underworld: the drone drops to A1 (below Forest's C2) and the progression is
 * A minor - F minor - C minor - G minor (i-vi-iii-vii, all minor rather than
 * Forest's brighter major-leaning voicings) transposed down an octave from
 * Forest's register. How this actually SOUNDS is unverified, per this
 * project's convention for anything only a real ear can settle - what is
 * verified is that it is a second, structurally distinct table reachable only
 * from BIOME_UNDERWORLD, exercised by --audio-test the same way LAYER_CFG is. */
static const float SYNTH_BASE_UW[1] = { 55.00f };   /* A1 drone */

static const float SYNTH_STRINGS_UW[SYNTH_STEPS] = {
    110.00f, 164.81f, 130.81f, 110.00f,   /* Am: A2, E3, C3, A2   */
     87.31f, 130.81f, 103.83f,  87.31f,   /* Fm: F2, C3, Ab2, F2  */
    130.81f, 196.00f, 155.56f, 130.81f,   /* Cm: C3, G3, Eb3, C3  */
     98.00f, 146.83f, 116.54f,  98.00f    /* Gm: G2, D3, Bb2, G2  */
};
static const float SYNTH_PAD_UW[SYNTH_STEPS] = {
    110.00f, 0.0f, 0.0f, 0.0f,  87.31f, 0.0f, 0.0f, 0.0f,
    130.81f, 0.0f, 0.0f, 0.0f,  98.00f, 0.0f, 0.0f, 0.0f
};
static const float SYNTH_BELLS_UW[SYNTH_STEPS] = {
    0.0f, 0.0f, 220.00f, 0.0f,  0.0f, 174.61f, 0.0f, 0.0f,
    0.0f, 0.0f, 261.63f, 0.0f,  0.0f, 196.00f, 0.0f, 0.0f
};
static const float SYNTH_VOICE_UW[SYNTH_STEPS] = {
    130.81f, 0.0f, 164.81f, 0.0f,  196.00f, 174.61f, 164.81f, 155.56f,
    130.81f, 0.0f, 164.81f, 0.0f,  196.00f, 0.0f, 174.61f, 0.0f
};

/* Lumiara: the drone rises to E4 - above Forest's C2 AND Underworld's A1,
 * rather than continuing the downward octave trend - a dream realm reached at
 * the end of a descent should not sound like the bottom of one. The
 * progression is E major - B major - F#minor - C#minor (I-V-ii-vi), the
 * brightest voicing of the three biomes on purpose: airy major fifths where
 * Forest leans major-ish and Underworld is uniformly minor. Unverified by ear
 * per this project's convention (see the Underworld comment above); verified
 * only as a third structurally distinct table reachable solely from
 * BIOME_LUMIARA. */
static const float SYNTH_BASE_LUM[1] = { 329.63f };   /* E4 drone */

static const float SYNTH_STRINGS_LUM[SYNTH_STEPS] = {
    659.26f, 987.77f, 783.99f, 659.26f,   /* E: E5, B5, G#5, E5   */
    493.88f, 739.99f, 587.33f, 493.88f,   /* B: B4, F#5, D5, B4   */
    369.99f, 554.37f, 440.00f, 369.99f,   /* F#m: F#4, C#5, A4, F#4 */
    277.18f, 415.30f, 329.63f, 277.18f    /* C#m: C#4, G#4, E4, C#4 */
};
static const float SYNTH_PAD_LUM[SYNTH_STEPS] = {
    659.26f, 0.0f, 0.0f, 0.0f,  493.88f, 0.0f, 0.0f, 0.0f,
    369.99f, 0.0f, 0.0f, 0.0f,  277.18f, 0.0f, 0.0f, 0.0f
};
static const float SYNTH_BELLS_LUM[SYNTH_STEPS] = {
    0.0f, 0.0f, 1318.51f, 0.0f,  0.0f, 987.77f, 0.0f, 0.0f,
    0.0f, 0.0f, 1479.98f, 0.0f,  0.0f, 1108.73f, 0.0f, 0.0f
};
static const float SYNTH_VOICE_LUM[SYNTH_STEPS] = {
    329.63f, 0.0f, 415.30f, 0.0f,  493.88f, 440.00f, 415.30f, 391.99f,
    329.63f, 0.0f, 415.30f, 0.0f,  493.88f, 0.0f, 440.00f, 0.0f
};

typedef struct {
    Wave         wave;
    const float *pat;
    int          pat_len;
    float        amp;     /* target amplitude when active              */
    float        attack;  /* swell layers: envelope rise per second     */
    float        decay;   /* pluck layers: 1 / tail-seconds (0 = legato) */
    float        cutoff;  /* one-pole lowpass coefficient, 0..1          */
} LayerCfg;

/* Indexed [BIOME_*][LAYER_*]. Wave/amp/attack/decay/cutoff stay identical
 * across biomes - only pitch content and pattern length change; see
 * SYNTH_*_UW above and the Forest tables. Holding the five amplitudes fixed is
 * what keeps MIX_GAIN's headroom argument true across a re-scored biome: the
 * worst case is still 0.95 before gain, so the clamp still cannot engage. */
static const LayerCfg LAYER_CFG_TABLE[BIOME_COUNT][NUM_LAYERS] = {
    {
        { W_SAW,  SYNTH_BASE,    FOREST_STEPS, 0.30f, 0.50f, 0.0f, 0.12f },
        { W_SAW,  SYNTH_STRINGS, FOREST_STEPS, 0.16f, 0.33f, 0.0f, 0.20f },
        { W_SINE, SYNTH_PAD,     FOREST_STEPS, 0.13f, 0.20f, 0.0f, 0.30f },
        { W_SINE, SYNTH_BELLS,   FOREST_STEPS, 0.16f, 0.0f,  1.5f, 0.60f },
        { W_SINE, SYNTH_VOICE,   FOREST_STEPS, 0.20f, 1.00f, 0.0f, 0.45f }
    },
    {
        { W_SAW,  SYNTH_BASE_UW,    1,  0.30f, 0.50f, 0.0f, 0.12f },
        { W_SAW,  SYNTH_STRINGS_UW, 16, 0.16f, 0.33f, 0.0f, 0.20f },
        { W_SINE, SYNTH_PAD_UW,     16, 0.13f, 0.20f, 0.0f, 0.30f },
        { W_SINE, SYNTH_BELLS_UW,   16, 0.16f, 0.0f,  1.5f, 0.60f },
        { W_SINE, SYNTH_VOICE_UW,   16, 0.20f, 1.00f, 0.0f, 0.45f }
    },
    {
        { W_SAW,  SYNTH_BASE_LUM,    1,  0.30f, 0.50f, 0.0f, 0.12f },
        { W_SAW,  SYNTH_STRINGS_LUM, 16, 0.16f, 0.33f, 0.0f, 0.20f },
        { W_SINE, SYNTH_PAD_LUM,     16, 0.13f, 0.20f, 0.0f, 0.30f },
        { W_SINE, SYNTH_BELLS_LUM,   16, 0.16f, 0.0f,  1.5f, 0.60f },
        { W_SINE, SYNTH_VOICE_LUM,   16, 0.20f, 1.00f, 0.0f, 0.45f }
    }
};

/* The step CLOCK, per biome, because the Forest is now scored at its source
 * track's tempo and the other two are not. Two parallel tables rather than one
 * more field on LayerCfg: the clock is a property of the biome's TRACK, not of
 * any one layer, and putting it on LayerCfg would let five layers of one biome
 * disagree about what a step is.
 *
 * Indexed by Synth.area, which every caller sets from a BIOME_* constant
 * (audio_request_reset's four call sites are the only writers) - so it is in
 * range by construction, the same way LAYER_CFG_TABLE above already assumes. */
static const int   SYNTH_LOOP_AT[BIOME_COUNT]   = {
    FOREST_STEPS, SYNTH_STEPS, SYNTH_STEPS
};
static const float SYNTH_STEP_S_AT[BIOME_COUNT] = {
    FOREST_STEP_S, SYNTH_STEP_S, SYNTH_STEP_S
};

/* Which fragment count unlocks each layer. Spread across FRAGMENT_COUNT rather
 * than the isometric build's 1/2/3, so all seven fragments carry a musical
 * event instead of the last four being silent. Index 0 is unused: LAYER_BASE
 * is on from the first sample. */
static const int LAYER_AT_FRAGS[NUM_LAYERS] = { 0, 1, 3, 5, 0 };

typedef struct {
    float phase;    /* 0..1 oscillator phase            */
    float env;      /* current envelope, 0..amp         */
    float filt;     /* one-pole lowpass state           */
    float cur_freq; /* current note; 0 = uninitialised  */
    int   step;     /* last pattern step rendered       */
} Voice;

/* Three sounds, and only three. SFX_DENY is the one that is not a reward: it
 * fires when she walks into a gate she has no ability for, which is otherwise
 * completely silent AND (until the gate art lands) invisible. */
#define NUM_SFX 3
enum { SFX_CHIME = 0, SFX_SOUL, SFX_DENY };

typedef struct {
    SDL_atomic_t fire;
    int          seen;
    double       phase;
    float        env;
} Sfx;

typedef struct {
    float freq;
    float amp;
    float decay;  /* 1 / tail-seconds */
    Wave  wave;
} SfxCfg;

static const SfxCfg SFX_CFG[NUM_SFX] = {
    { 660.0f,  0.28f, 3.2f, W_SINE },   /* fragment: a bright short chime  */
    { 392.0f,  0.30f, 1.1f, W_SINE },   /* soul: lower, and it rings on    */
    { 110.0f,  0.16f, 9.0f, W_SQUARE }  /* denied: a dull, very short thud */
};

/* The volume scale, shared by the settings screen and the mix below. Ten steps,
 * not a hundred: the menu row shows the number, and a value the player cannot
 * step through in a couple of key presses is one they will not adjust. Defined
 * here rather than beside the other settings because the audio callback is what
 * has to honour it, and the whole file is one translation unit read top down. */
#define VOL_MAX 10

/* Master gain on the game mix - NOT on the diagnostic tone, which --audio-test
 * measures at its stated amplitude.
 *
 * The five layers sum to 0.95 at their maxima and the three SFX to 0.74, so an
 * unattenuated mix reaches 1.69 and spends real time pinned against the output
 * clamp. That is not loudness, it is distortion, and the "no sample left
 * [-1,1]" check could never see it because the clamp is what keeps it in range.
 * 0.55 puts the theoretical worst case at 0.93, so the clamp can never engage -
 * and --audio-test --layers now asserts exactly that, by counting samples that
 * reach it while driving every layer and firing all three SFX every 40 ms.
 *
 * The settings volumes multiply the mix BEFORE this gain and are both in [0,1],
 * so they can only ever attenuate: 0.93 stays the upper bound whatever the
 * player picks, and the clamp stays unreachable without re-deriving anything.
 * That is also why full volume is 1.0 and not, say, 1.2 - a slider that could
 * push the mix into the clamp would be trading the invariant for loudness, and
 * at VOL_MAX the output is bit-identical to what shipped before there were
 * volumes at all. */
#define MIX_GAIN 0.55f

typedef struct {
    int    synth_on;
    int    layers;      /* bitmask of active layers            */
    int    frag_cnt;    /* fragment restores latched           */
    int    voice_cnt;   /* soul restores latched               */
    int    area;        /* BIOME_* - which LAYER_CFG_TABLE row  */
    Uint64 sample;      /* samples rendered since synth start  */
    Voice  v[NUM_LAYERS];
    int    layer_seen;  /* callback-side latch for layer_fire  */
    int    voice_seen;  /* callback-side latch for voice_fire  */
} Synth;

typedef struct {
    int    req_rate;  /* rate we ask for; AUDIO_RATE except under --rate */
    int    rate;      /* rate the device actually gave us               */
    int    channels;
    double phase;     /* test-tone phase in cycles; double so long runs do not drift */
    int    noise;     /* 1 = seeded white noise instead of the tone */
    int    tone;      /* ambient test tone; off in the game, on in --audio-test */
    Rng    rng;

    Sfx          sfx[NUM_SFX];
    SDL_atomic_t layer_fire;  /* bumped once per fragment restore        */
    SDL_atomic_t voice_fire;  /* bumped once per soul restore            */
    /* Settings volumes, 0..VOL_MAX. Atomics because they are the one part of
     * Audio the game thread writes while the callback is running, and they are
     * a single int each, so unlike the reset payload there is nothing to tear:
     * no flag is needed, the value either arrives this callback or the next.
     *
     * SDL_zero leaves these at 0, which is silence, so EVERY construction of an
     * Audio that will actually render must set them - main through
     * cfg_apply_audio before the device opens, audio_selftest to VOL_MAX. */
    SDL_atomic_t music_vol;
    SDL_atomic_t sfx_vol;
    /* Reset and reseed both follow the same discipline: the game thread writes
     * the PAYLOAD first and sets the FLAG second, and the callback reads the
     * payload only after seeing the flag, so it can never act on a half-written
     * value. reset_frags/reset_souls carry the restore counts a loaded game
     * should resume at - a load must not make the player re-earn seven layers. */
    SDL_atomic_t reset_req;
    int          reset_frags;
    int          reset_souls;
    int          reset_area;   /* BIOME_* - latched alongside frags/souls */
    SDL_atomic_t rng_req;
    Uint64       rng_seed_req;
    Synth        synth;
#if WAYFARER_SELFTEST
    Uint64 calls;
    Uint64 frames;
    Uint64 max_ticks;    /* worst-case callback duration, perf counter ticks */
    int    partial_len;  /* callbacks whose len was not a whole frame count  */
    float *cap;          /* capture buffer for offline analysis             */
    int    cap_cap;
    int    cap_len;
#endif
} Audio;

/* One sample of a waveform. W_NOISE draws from the callback-owned Rng and is
 * used by SFX only - never by a music layer, so "the music is a pure function
 * of the sample counter" stays true. */
static float wave_sample(Wave w, float ph, Audio *a)
{
    switch (w) {
    case W_SAW:    return 2.0f * ph - 1.0f;
    case W_SQUARE: return ph < 0.5f ? 1.0f : -1.0f;
    case W_SINE:   return SDL_sinf(ph * (float)TWO_PI);
    case W_NOISE:  return rng_bipolar(&a->rng);
    default:       return 0.0f;
    }
}

static void sfx_fire(Audio *a, int kind) { SDL_AtomicAdd(&a->sfx[kind].fire, 1); }

/* One fragment's worth of musical progress. A function rather than inline code
 * because both the live latch and a load's replay must produce EXACTLY the same
 * layer mask from the same count - two copies of the unlock ladder is precisely
 * how they would drift. Table-driven, so the schedule is one line to change. */
static void synth_add_frag(Synth *s)
{
    int li;
    s->frag_cnt++;
    for (li = 1; li < NUM_LAYERS; li++)
        if (LAYER_AT_FRAGS[li] && s->frag_cnt == LAYER_AT_FRAGS[li])
            s->layers |= 1 << li;
}

static void synth_add_soul(Synth *s)
{
    s->voice_cnt++;
    s->layers |= 1 << LAYER_VOICE;   /* the first soul opens it, and it stays */
}

/* Fold the game thread's atomic counters into callback-owned state. Called once
 * per callback, not once per sample. reset_req exists so the game thread can ask
 * for a zeroed synth without writing callback-owned fields itself - that would
 * be a data race, and the hard deadline rules out taking a lock. */
static void synth_latch(Audio *a)
{
    Synth *s = &a->synth;
    int lf, vf, k;

    if (SDL_AtomicGet(&a->reset_req)) {
        SDL_AtomicSet(&a->reset_req, 0);
        s->layers = 0; s->frag_cnt = 0; s->voice_cnt = 0; s->sample = 0;
        s->area = a->reset_area;
        SDL_memset(s->v, 0, sizeof(s->v));
        /* Re-latch to the CURRENT fire counts, so restores that happened before
         * the reset are not replayed on top of the payload below. */
        s->layer_seen = SDL_AtomicGet(&a->layer_fire);
        s->voice_seen = SDL_AtomicGet(&a->voice_fire);
        for (k = 0; k < a->reset_frags; k++) synth_add_frag(s);
        for (k = 0; k < a->reset_souls; k++) synth_add_soul(s);
    }
    if (SDL_AtomicGet(&a->rng_req)) {
        SDL_AtomicSet(&a->rng_req, 0);
        rng_seed(&a->rng, a->rng_seed_req, STREAM_AUDIO);
    }

    lf = SDL_AtomicGet(&a->layer_fire);
    while (s->layer_seen != lf) { s->layer_seen++; synth_add_frag(s); }
    vf = SDL_AtomicGet(&a->voice_fire);
    while (s->voice_seen != vf) { s->voice_seen++; synth_add_soul(s); }

    if (s->synth_on)
        s->layers |= 1 << LAYER_BASE;
}

/* Render one mixed sample of every active layer. Pure apart from the Voice
 * state it advances, all of which is callback-owned. */
static float synth_step(Audio *a)
{
    Synth *s = &a->synth;
    float mix = 0.0f;
    int li;
    int cur_step = (int)((double)s->sample /
                         ((double)a->rate * SYNTH_STEP_S_AT[s->area]))
                   % SYNTH_LOOP_AT[s->area];

    s->sample++;

    for (li = 0; li < NUM_LAYERS; li++) {
        Voice *v = &s->v[li];
        const LayerCfg *c = &LAYER_CFG_TABLE[s->area][li];
        float out;

        if (!((s->layers >> li) & 1)) continue;

        /* Advance the pattern on a step boundary, or on the first sample ever. */
        if (v->step != cur_step || v->cur_freq <= 0.0f) {
            float nf = c->pat[cur_step % c->pat_len];
            v->step = cur_step;
            if (nf > 0.0f) {
                v->cur_freq = nf;
                if (c->decay > 0.0f) { v->phase = 0.0f; v->env = c->amp; }
            }
        }

        /* Plucks decay from the strike; swells ramp toward their target. */
        if (c->decay > 0.0f) {
            v->env -= c->decay * c->amp / (float)a->rate;
            if (v->env < 0.0f) v->env = 0.0f;
        } else if (v->env < c->amp) {
            v->env += c->attack / (float)a->rate;
            if (v->env > c->amp) v->env = c->amp;
        }
        if (v->env <= 0.0f) continue;

        out = wave_sample(c->wave, v->phase, a);
        v->phase += v->cur_freq / (float)a->rate;
        if (v->phase >= 1.0f) v->phase -= 1.0f;
        v->filt += c->cutoff * (out - v->filt);   /* one-pole lowpass, for warmth */
        out = v->filt;
        if (li == LAYER_VOICE) {
            /* A slow tremolo, which is most of what makes this layer read as a
             * voice rather than one more sine. */
            float trem = 0.7f + 0.3f * SDL_sinf(
                (float)((double)s->sample * 6.0 / a->rate) * (float)TWO_PI);
            out *= trem;
        }
        mix += out * v->env;
    }
    return mix;
}

/* SDL's real-time audio thread. No allocation, no locks, no syscalls, no
 * unbounded work - see the block comment above. */
static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    Audio *a = (Audio *)userdata;
    float *out = (float *)(void *)stream;
    int nfloats = len / (int)sizeof(float);
    int frames = nfloats / a->channels;
    double inc = TONE_HZ / (double)a->rate;
    float mvol, svol;
    int i, c, si;
#if WAYFARER_SELFTEST
    Uint64 t0 = SDL_GetPerformanceCounter();
    if (nfloats % a->channels != 0)
        a->partial_len++;
#endif

    synth_latch(a);
    /* Read ONCE per callback, not once per sample: an atomic read per sample
     * would be 1024 of them for a value that cannot meaningfully change inside
     * 21 ms, and a volume that moved mid-buffer would zip. A change therefore
     * lands on a buffer boundary, which at 21.3 ms is inaudible as a step. */
    mvol = (float)SDL_AtomicGet(&a->music_vol) / (float)VOL_MAX;
    svol = (float)SDL_AtomicGet(&a->sfx_vol)   / (float)VOL_MAX;
    if (mvol < 0.0f) mvol = 0.0f;
    if (mvol > 1.0f) mvol = 1.0f;
    if (svol < 0.0f) svol = 0.0f;
    if (svol > 1.0f) svol = 1.0f;
    for (si = 0; si < NUM_SFX; si++) {
        int fired = SDL_AtomicGet(&a->sfx[si].fire);
        if (fired != a->sfx[si].seen) {
            a->sfx[si].seen = fired;
            a->sfx[si].env = 1.0f;
            a->sfx[si].phase = 0.0;
        }
    }

    for (i = 0; i < frames; i++) {
        float v = 0.0f, mus = 0.0f, sfxm = 0.0f;
        if (a->noise) {
            v = rng_bipolar(&a->rng) * TONE_AMP;
        } else if (a->tone) {
            v = SDL_sinf((float)(a->phase * TWO_PI)) * TONE_AMP;
            a->phase += inc;
            if (a->phase >= 1.0) a->phase -= 1.0;
        } else if (a->synth.synth_on) {
            mus = synth_step(a);
        }
        for (si = 0; si < NUM_SFX; si++) {
            if (a->sfx[si].env > 0.0f) {
                const SfxCfg *sc = &SFX_CFG[si];
                sfxm += wave_sample(sc->wave, (float)a->sfx[si].phase, a)
                        * a->sfx[si].env * sc->amp;
                a->sfx[si].phase += (double)sc->freq / (double)a->rate;
                if (a->sfx[si].phase >= 1.0) a->sfx[si].phase -= 1.0;
                a->sfx[si].env -= sc->decay / (float)a->rate;
                if (a->sfx[si].env < 0.0f) a->sfx[si].env = 0.0f;
            }
        }
        /* The two volumes are held apart all the way to here rather than being
         * folded into one number, because they are two controls: turning the
         * music down must not take the chime with it. */
        v += (mus * mvol + sfxm * svol) * MIX_GAIN;
        /* Kept as a safety net even though MIX_GAIN makes it unreachable: it is
         * the last line between an arithmetic bug in here and the speakers. */
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        for (c = 0; c < a->channels; c++)
            out[i * a->channels + c] = v;
    }

    /* SDL always asks for whole frames, but one unwritten byte would play back
     * as garbage. Cheap insurance against a real bug elsewhere in here. */
    for (i = frames * a->channels; i < nfloats; i++)
        out[i] = 0.0f;

#if WAYFARER_SELFTEST
    if (a->cap && a->cap_len < a->cap_cap) {
        int n = nfloats;
        if (n > a->cap_cap - a->cap_len) n = a->cap_cap - a->cap_len;
        SDL_memcpy(a->cap + a->cap_len, out, (size_t)n * sizeof(float));
        a->cap_len += n;
    }
    a->calls++;
    a->frames += (Uint64)frames;
    {
        Uint64 dt = SDL_GetPerformanceCounter() - t0;
        if (dt > a->max_ticks) a->max_ticks = dt;
    }
#endif
}

static SDL_AudioDeviceID audio_open(Audio *a, SDL_AudioSpec *have)
{
    SDL_AudioSpec want;
    SDL_AudioDeviceID dev;

    SDL_zero(want);
    want.freq     = a->req_rate ? a->req_rate : AUDIO_RATE;
    want.format   = AUDIO_F32SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples  = AUDIO_SAMPLES;
    want.callback = audio_cb;
    want.userdata = a;

    /* Allow a frequency change and nothing else. Taking the device's native
     * rate costs one divide; letting SDL resample would link its converter and
     * add latency. F32 is WASAPI's native format, so in practice no conversion
     * happens at all. */
    dev = SDL_OpenAudioDevice(NULL, 0, &want, have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev)
        return 0;
    a->rate = have->freq;
    a->channels = have->channels;
    return dev;
}

/* ---- Value noise --------------------------------------------------------
 * Bilinear over a coarse lattice, smoothstepped on both axes so the field has
 * no lattice-aligned creases. Lattices are caller-owned stack arrays. */
static void land_lattice(Rng *r, float *lat, int n)
{
    int i;
    for (i = 0; i < n; i++) lat[i] = rng_float(r);
}

static float land_noise(const float *lat, int lw, int lh, float fx, float fy)
{
    float gx = fx * (float)(lw - 1), gy = fy * (float)(lh - 1);
    int x0 = (int)gx, y0 = (int)gy, x1, y1;
    float tx = gx - (float)x0, ty = gy - (float)y0, a, b;

    if (x0 < 0) { x0 = 0; tx = 0.0f; }
    if (y0 < 0) { y0 = 0; ty = 0.0f; }
    if (x0 > lw - 2) { x0 = lw - 2; tx = 1.0f; }
    if (y0 > lh - 2) { y0 = lh - 2; ty = 1.0f; }
    x1 = x0 + 1; y1 = y0 + 1;

    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    a = lat[y0 * lw + x0] + (lat[y0 * lw + x1] - lat[y0 * lw + x0]) * tx;
    b = lat[y1 * lw + x0] + (lat[y1 * lw + x1] - lat[y1 * lw + x0]) * tx;
    return a + (b - a) * ty;
}

/* ---- Tile tables --------------------------------------------------------
 *
 * Which authored cell means what. These live here rather than in the bake
 * because they are a RENDERING decision: the bake emits ART_TILE_C<col>_R<row>
 * mechanically, and a wrong entry below is visible on screen, where a wrong
 * entry in a 92-line generated name table would not be.
 *
 * The 3x3 edge sets are BLOB autotiles: index [row*3+col] where row is chosen
 * by whether the north/south neighbour differs and col by east/west. There are
 * no inner-corner tiles in the delivered sheet, so a diagonal-only difference
 * draws as interior - which the art's ragged organic edges hide, and which is
 * why these edges read as foliage rather than as a tilemap. */
/* BASE fills must be fully opaque - they are the bottom layer, and anything
 * they fail to cover is a hole straight through to whatever the frame last
 * held. Only these four grass cells qualify: measured, not assumed.
 *
 * The block at cols 3-4 LOOKS like six more fill variants and is not - each
 * carries 6-14 transparent pixels, so using them as a base left ~1% of the
 * screen unpainted. They are detail overlays and are listed separately below.
 * --tile-test now asserts the opacity of every base table, so this cannot
 * regress quietly. */
static const short tile_grass_base[4] = {
    ART_TILE_C1_R1, ART_TILE_C5_R0, ART_TILE_C5_R1, ART_TILE_C5_R2
};
/* Near-opaque decorated grass - pebbles, tufts, sprouts - drawn OVER a base. */
static const short tile_grass_detail[6] = {
    ART_TILE_C3_R0, ART_TILE_C4_R0,
    ART_TILE_C3_R1, ART_TILE_C4_R1,
    ART_TILE_C3_R2, ART_TILE_C4_R2
};
static const short tile_dirt_fill[6] = {
    ART_TILE_C6_R0, ART_TILE_C7_R0,
    ART_TILE_C6_R1, ART_TILE_C7_R1,
    ART_TILE_C6_R2, ART_TILE_C7_R2
};
/* OPEN WATER, and nothing else. The pond interior is water and lilies; every
 * other thing the sheet draws on water belongs to a rim, not to a fill.
 *
 * The sheet's bottom-right 2x2 block is the only pure-water art in it, and it
 * is the whole of what this table may name. Cols 6-7 of rows 10-12 read as six
 * more open-water variants and are NOT - they are the ROCK-IN-WATER motifs that
 * belong to the pond rim, and each carries two kinds of contamination that a
 * base fill has no way to hide. Per-cell census of the 256 px, against the
 * sheet's own authored ground and stone colours:
 *
 *     cell    land px   rock px          cell    land px   rock px
 *     c6r10      1        11             c6r13      0         0
 *     c7r10      1        17             c7r13      0         0
 *     c6r11      1        21             c6r14      0         0
 *     c7r11      1        26             c7r14      0         0
 *     c6r12      2        38
 *     c7r12      2        37
 *
 * The rock pixels are boulders sitting in open water three tiles from any bank.
 * The land pixels are worse and are the reported bug: they are the CORNERS
 * where the source cell's own grass/dirt surround survives the crop, so every
 * such tile painted a grey-green notch into the middle of a pond - the same
 * failure as a sprite carrying its authoring background, just one tile at a
 * time. --tile-test now measures both, and rejects the six cells by name.
 *
 * Ten slots for four cells, so the mix is chosen rather than merely uniform:
 * eight open-water and two lily, holding the lily rate at the 2-in-10 the ten
 * distinct cells used to give. A pond where a third of the tiles carry a pad
 * reads as a lily farm. */
static const short tile_water_fill[10] = {
    ART_TILE_C6_R13, ART_TILE_C6_R14, ART_TILE_C6_R13, ART_TILE_C7_R13,
    ART_TILE_C6_R14, ART_TILE_C6_R13, ART_TILE_C6_R14, ART_TILE_C7_R14,
    ART_TILE_C6_R13, ART_TILE_C6_R14
};
/* Grass over dirt: transparent outside, so it paints a ragged grass edge onto
 * whatever is beneath. That is why dirt is laid down first for a grass cell
 * that touches dirt - the overlay supplies the boundary, not the base. */
static const short tile_grass_edge[9] = {
    ART_TILE_C0_R0, ART_TILE_C1_R0, ART_TILE_C2_R0,
    ART_TILE_C0_R1, ART_TILE_C1_R1, ART_TILE_C2_R1,
    ART_TILE_C0_R2, ART_TILE_C1_R2, ART_TILE_C2_R2
};
static const short tile_olive_edge[9] = {   /* the darker patches, over grass */
    ART_TILE_C0_R3, ART_TILE_C1_R3, ART_TILE_C2_R3,
    ART_TILE_C0_R4, ART_TILE_C1_R4, ART_TILE_C2_R4,
    ART_TILE_C0_R5, ART_TILE_C1_R5, ART_TILE_C2_R5
};
/* The pond rim, grass context. Row 9 is a cap drawn on the LAND tile above the
 * water; rows 10-12 are the rim proper. Verified against Mockup1, whose pond
 * uses exactly these columns. */
static const short tile_water_cap[3] = {
    ART_TILE_C0_R9, ART_TILE_C1_R9, ART_TILE_C2_R9
};
static const short tile_water_edge[9] = {
    ART_TILE_C0_R10, ART_TILE_C1_R10, ART_TILE_C2_R10,
    ART_TILE_C0_R11, ART_TILE_C1_R11, ART_TILE_C2_R11,
    ART_TILE_C0_R12, ART_TILE_C1_R12, ART_TILE_C2_R12
};
/* Rock outcrop: a 3x3 ring with a HOLLOW centre - the source cell is empty, so
 * the interior shows whatever ground was laid under it. */
static const short tile_rock_ring[9] = {
    ART_TILE_C3_R3, ART_TILE_C4_R3, ART_TILE_C5_R3,
    ART_TILE_C3_R4, ART_NONE,       ART_TILE_C5_R4,
    ART_TILE_C3_R5, ART_TILE_C4_R5, ART_TILE_C5_R5
};
/* ...and what to lay under that hollow: stone rubble, near-opaque, so an
 * outcrop's interior is stone rather than whatever ground was laid first.
 *
 * The sheet draws this rubble TWICE - once standing in grass at rows 3-4 and
 * once in dirt at rows 6-7 - and about a third of each cell is that ground,
 * not stone: 84 green px of 248 opaque at c6r4, 80 brown px of 244 at c6r7.
 * That third is not a detail. Rows 6-7 were used here while tile_rock_ring
 * above uses the GRASS ring from rows 3-5, so every outcrop laid ~80 px of
 * brown per cell underneath a green-margined rim, on green grass - the
 * hard-edged brown RECTANGLE the ring comment used to describe as a reason to
 * avoid a dirt BASE. The base was never the whole of it; the rubble carried
 * its own dirt in.
 *
 * Rows 3-4 are the matching pair for the ring already in use, and the choice
 * is not arbitrary: gt_leafy already counts GT_ROCK as leafy ground, which is
 * the codebase saying an outcrop stands in grass. The grass_edge pass then
 * draws the real grass/dirt boundary onto any rock cell that touches dirt, so
 * an outcrop at the edge of a path still transitions properly without this
 * table needing a second context. They are also slightly MORE opaque than the
 * cells they replace - 8 and 10 transparent px against 12 and 11 - so the
 * underlay's own job is unaffected. --tile-test asserts that opacity, and now
 * also asserts that the rubble and the ring agree on their ground. */
static const short tile_rock_fill[2] = { ART_TILE_C6_R4, ART_TILE_C7_R4 };

/* The map's edge, which is a CLIFF and not an outcrop - see world_border.
 *
 * The two side-rim cells of the same grass-context block, and they are the
 * right ones for a measured reason rather than a felt one. Counting the sheet's
 * own stone colours over each candidate cell, out of 256:
 *
 *     cell            opaque   dark   light   grass    role
 *     c6r4 / c7r4      248/246  101/113  63/51  84/82   the outcrop BODY
 *     c4r5 (S rim)     243       60      99     84
 *     c3r4 (W rim)     139       68      31     40
 *     c5r4 (E rim)     139      101       0     38
 *     c4r3 (N rim)      78        6       0     72
 *
 * where "dark" is the cool stone (76,48,52 / 49,19,33 / 69,77,89) and "light"
 * the warm highlight (125,85,85 / 175,144,116). The body cells are half again
 * as wide and carry two to three times the pale highlight, which is exactly
 * what made the border read as a broad beige band instead of a dark ridge -
 * measured against the reference render, whose edge column is 63% grass with a
 * thin cool seam through it and almost no light stone at all.
 *
 * c5r4 is the ONE cell that is both properties at once: 101 dark px, zero pale,
 * and 139 of 256 covered - comfortably over SOLID_MIN_OBSTACLE_PX, so the edge
 * stays visibly solid and --tile-test's invisible-wall check is not being
 * traded away for a thinner line.
 *
 * Both slots hold it, and that is a finding rather than a shortcut. The first
 * version of this table paired c5r4 with the W rim c3r4 for variety, and the
 * pale-highlight check rejected c3r4 on its 31 pale pixels - the same defect
 * this table exists to fix, at a fifth the dose, which is exactly the sort of
 * thing an eye approves and a measurement does not. The N rim is out on the
 * other property: 78 px is under the coverage floor and 72 of them are grass,
 * so a top border drawn from it would be a wall made mostly of meadow. A second
 * dark cell can take slot 1 whenever the sheet gains one; until then the edge
 * of the world reads as one continuous cliff face, which is what it is. */
static const short tile_rock_cliff[2] = { ART_TILE_C5_R4, ART_TILE_C5_R4 };

/* ---- Underworld tile tables -----------------------------------------------
 *
 * Same shapes as the Forest tables above, on purpose: render_world's five
 * passes and every hardcoded `% 4u`/`% 6u`/blob-slice index stay unchanged,
 * only WHICH table they read changes (see TileSet below). Curated cells from
 * tools/bake.ps1 (see its "Underworld tiles" block for the exact source
 * coordinates and the opacity/colour reasoning).
 *
 * Deliberately simpler than Forest's: ground_base/dirt_fill/water_fill are
 * flat hash-selected variants (no separate detail-overlay table - the one
 * caller of tile_grass_detail below is guarded to Forest only), and
 * grass_edge/olive_edge reuse a single flat floor tile for all nine blob
 * slots rather than a curated organic edge - Underworld's ground/dirt
 * boundary reads as blockier than Forest's, not broken. water_edge and
 * rock_ring get the real 3x3 mapping (a load-bearing hazard/wall boundary is
 * worth the curation; a cosmetic ground seam is not) and, since neither
 * table's centre slice (index 4) is ever reached in practice - water_edge is
 * only drawn when NOT fully interior, and rock_ring's ART_NONE hollow makes
 * this explicit for rock - they share one motif: Water_coasts.png's one
 * clean "hole" shape, whose natural green algae rim doubles as the acid
 * hazard colour. */
static const short tile_uw_ground_base[4] = {
    ART_UW_FLOOR_A, ART_UW_FLOOR_B, ART_UW_FLOOR_C, ART_UW_FLOOR_D
};
static const short tile_uw_dirt_fill[6] = {
    ART_UW_RUBBLE_A, ART_UW_RUBBLE_B, ART_UW_RUBBLE_C,
    ART_UW_RUBBLE_D, ART_UW_RUBBLE_A, ART_UW_RUBBLE_B
};
static const short tile_uw_water_fill[10] = {   /* toxic-tinted at bake time */
    ART_UW_ACIDFILL_A, ART_UW_ACIDFILL_B, ART_UW_ACIDFILL_C, ART_UW_ACIDFILL_D,
    ART_UW_ACIDFILL_A, ART_UW_ACIDFILL_B, ART_UW_ACIDFILL_C, ART_UW_ACIDFILL_D,
    ART_UW_ACIDFILL_A, ART_UW_ACIDFILL_B
};
static const short tile_uw_grass_edge[9] = {
    ART_UW_FLOOR_A, ART_UW_FLOOR_A, ART_UW_FLOOR_A,
    ART_UW_FLOOR_A, ART_UW_FLOOR_A, ART_UW_FLOOR_A,
    ART_UW_FLOOR_A, ART_UW_FLOOR_A, ART_UW_FLOOR_A
};
static const short tile_uw_olive_edge[9] = {
    ART_UW_FLOOR_B, ART_UW_FLOOR_B, ART_UW_FLOOR_B,
    ART_UW_FLOOR_B, ART_UW_FLOOR_B, ART_UW_FLOOR_B,
    ART_UW_FLOOR_B, ART_UW_FLOOR_B, ART_UW_FLOOR_B
};
static const short tile_uw_water_cap[3] = {
    ART_UW_ACID_N, ART_UW_ACID_NE, ART_UW_ACID_NW
};
static const short tile_uw_water_edge[9] = {
    ART_UW_ACID_NW, ART_UW_ACID_N, ART_UW_ACID_NE,
    ART_UW_ACID_W,  ART_UW_ACID_N, ART_UW_ACID_E,
    ART_UW_ACID_SW, ART_UW_ACID_S, ART_UW_ACID_SE
};
static const short tile_uw_rock_ring[9] = {
    ART_UW_ROCK_NW, ART_UW_ROCK_N, ART_UW_ROCK_NE,
    ART_UW_ROCK_W,  ART_NONE,      ART_UW_ROCK_E,
    ART_UW_ROCK_SW, ART_UW_ROCK_S, ART_UW_ROCK_SE
};
static const short tile_uw_rock_fill[2] = { ART_UW_ROCKWALL_A, ART_UW_ROCKWALL_B };
static const short tile_uw_rock_cliff[2] = { ART_UW_ROCKWALL_A, ART_UW_ROCKWALL_B };

/* ---- Lumiara tile tables ---------------------------------------------------
 *
 * Same shapes as Forest/Underworld above, same reason: render_world stays
 * untouched, only WHICH table tileset_for hands back changes. Curated cells
 * from tools/bake.ps1's "Lumiara tiles" block, which explains the two things
 * that make this table simpler to read than it looks:
 *
 * 1. GT_DIRT reads as "Cobblestone Path" and GT_ROCK reads as "Void Chasm" -
 *    there is no cobblestone-specific edge field in TileSet (the plan named
 *    one; it does not exist because render_world has no pass that would use
 *    it) and no new GT_* value. Cobblestone rides the existing dirt_fill slot
 *    exactly the way Underworld's rubble does, and the Void Chasm rides
 *    rock_ring/rock_fill exactly the way Underworld's toxic rock wall does -
 *    both are pre-existing hazard/path roles being re-skinned, not new
 *    mechanics.
 * 2. The three source sheets are painterly, not authored as directional blob
 *    pieces (see bake.ps1), so grass_edge/olive_edge/water_edge/rock_ring
 *    below reuse a small handful of curated cells across every blob_slice
 *    slot rather than a real per-direction mapping - the same simplification
 *    Underworld's own tables already document, and for the same reason: a
 *    cosmetic boundary reading blockier than a true 3x3 wrap is a look, not a
 *    bug; the tile actually being solid where it draws solid is what matters.
 */
static const short tile_lum_ground_base[4] = {
    ART_LUM_GRASS_A, ART_LUM_GRASS_B, ART_LUM_GRASS_C, ART_LUM_GRASS_D
};
static const short tile_lum_dirt_fill[6] = {
    ART_LUM_COBBLE_A, ART_LUM_COBBLE_B, ART_LUM_COBBLE_A,
    ART_LUM_COBBLE_B, ART_LUM_COBBLE_A, ART_LUM_COBBLE_B
};
static const short tile_lum_water_fill[10] = {
    ART_LUM_WATER_A, ART_LUM_WATER_B, ART_LUM_WATER_C, ART_LUM_WATER_D,
    ART_LUM_WATER_A, ART_LUM_WATER_B, ART_LUM_WATER_C, ART_LUM_WATER_D,
    ART_LUM_WATER_A, ART_LUM_WATER_B
};
static const short tile_lum_grass_edge[9] = {
    ART_LUM_GRASSEDGE, ART_LUM_GRASSEDGE, ART_LUM_GRASSEDGE,
    ART_LUM_GRASSEDGE, ART_LUM_GRASSEDGE, ART_LUM_GRASSEDGE,
    ART_LUM_GRASSEDGE, ART_LUM_GRASSEDGE, ART_LUM_GRASSEDGE
};
static const short tile_lum_olive_edge[9] = {
    ART_LUM_OLIVEEDGE, ART_LUM_OLIVEEDGE, ART_LUM_OLIVEEDGE,
    ART_LUM_OLIVEEDGE, ART_LUM_OLIVEEDGE, ART_LUM_OLIVEEDGE,
    ART_LUM_OLIVEEDGE, ART_LUM_OLIVEEDGE, ART_LUM_OLIVEEDGE
};
static const short tile_lum_water_cap[3] = {
    ART_LUM_WBORDER_N, ART_LUM_WBORDER_NE, ART_LUM_WBORDER_NW
};
static const short tile_lum_water_edge[9] = {
    ART_LUM_WBORDER_NW, ART_LUM_WBORDER_N, ART_LUM_WBORDER_NE,
    ART_LUM_WBORDER_NW, ART_LUM_WBORDER_N, ART_LUM_WBORDER_NE,
    ART_LUM_WBORDER_NW, ART_LUM_WBORDER_N, ART_LUM_WBORDER_NE
};
static const short tile_lum_rock_ring[9] = {
    ART_LUM_VOIDBORDER_NW, ART_LUM_VOIDBORDER_N, ART_LUM_VOIDBORDER_NE,
    ART_LUM_VOIDBORDER_NW, ART_NONE,              ART_LUM_VOIDBORDER_NE,
    ART_LUM_VOIDBORDER_NW, ART_LUM_VOIDBORDER_N, ART_LUM_VOIDBORDER_NE
};
static const short tile_lum_rock_fill[2] = { ART_LUM_VOID_A, ART_LUM_VOID_B };

/* Selects which set of tables render_world (and the tile-opacity self-tests)
 * read from, indexed by World.biome. One indirection point instead of a
 * biome branch at every one of the ~11 call sites below. */
typedef struct {
    const short *grass_base;
    const short *dirt_fill;
    const short *water_fill;
    const short *grass_edge;
    const short *olive_edge;
    const short *water_cap;
    const short *water_edge;
    const short *rock_ring;
    const short *rock_fill;
    /* What the map's edge ring is drawn from, as distinct from an outcrop.
     * Underworld and Lumiara point theirs back at rock_fill: both of those
     * fills measure 0 transparent px, so their edges already read as unbroken
     * wall and there is nothing for a separate cliff table to improve. Forest
     * is the biome whose outcrop art is a rim around a hollow, and therefore
     * the only one where "edge of the world" and "lump of stone" need to be
     * drawn differently. */
    const short *rock_cliff;
} TileSet;

static const TileSet TILESET_FOREST = {
    tile_grass_base, tile_dirt_fill, tile_water_fill, tile_grass_edge, tile_olive_edge,
    tile_water_cap, tile_water_edge, tile_rock_ring, tile_rock_fill, tile_rock_cliff
};
static const TileSet TILESET_UNDERWORLD = {
    tile_uw_ground_base, tile_uw_dirt_fill, tile_uw_water_fill, tile_uw_grass_edge, tile_uw_olive_edge,
    tile_uw_water_cap, tile_uw_water_edge, tile_uw_rock_ring, tile_uw_rock_fill, tile_uw_rock_cliff
};
static const TileSet TILESET_LUMIARA = {
    tile_lum_ground_base, tile_lum_dirt_fill, tile_lum_water_fill, tile_lum_grass_edge, tile_lum_olive_edge,
    tile_lum_water_cap, tile_lum_water_edge, tile_lum_rock_ring, tile_lum_rock_fill, tile_lum_rock_fill
};

static const TileSet *tileset_for(Uint8 biome)
{
    if (biome == BIOME_FOREST) return &TILESET_FOREST;
    if (biome == BIOME_UNDERWORLD) return &TILESET_UNDERWORLD;
    return &TILESET_LUMIARA;
}

static int terr_at(const World *w, int tx, int ty)
{
    /* Outside the world reads as grass so edge tiles do not draw a boundary
     * against nothing; the camera clamp keeps it off screen anyway. */
    if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
        return GT_GRASS;
    return w->terr[ty][tx];
}

/* The blob index for a cell, given a predicate "is this neighbour the same
 * material as me". Row from north/south, column from west/east. */
static int blob_slice(int n_same, int s_same, int e_same, int w_same)
{
    int row = n_same ? (s_same ? 1 : 2) : 0;
    int col = w_same ? (e_same ? 1 : 2) : 0;
    return row * 3 + col;
}

/* Grass and olive are both "leafy ground": olive is a darker patch painted on
 * grass, not a different substrate, so a grass/olive boundary must NOT draw a
 * dirt edge. This predicate is the one place that decision lives. */
static int gt_leafy(int t) { return t == GT_GRASS || t == GT_OLIVE || t == GT_ROCK; }

/* The forced one-tile ring at the edge of the map (see world_stub).
 *
 * It is GT_ROCK like an outcrop and it is drawn nothing like one, which is why
 * this is a predicate rather than a terrain test. An outcrop is a lump of stone
 * standing IN the meadow and is drawn as a rim around a hollow; the border is
 * the edge of the world, a continuous cliff face with nothing behind it. Given
 * outcrop art it came out as a wide band of pale rubble - the "wrong rock" - so
 * the two are drawn from different tables, and only this says which is which. */
static int world_border(int tx, int ty)
{
    return tx == 0 || ty == 0 || tx == WORLD_W - 1 || ty == WORLD_H - 1;
}

/* Is this cell surrounded on all four axes by its own material?
 *
 * That is exactly blob_slice's CENTRE index, and the centre is the one slice
 * where both the pond rim and the rock ring draw nothing - so it is also the
 * one slice whose cell is still showing whatever base was laid under it. Both
 * the ground pass and prop placement need that distinction, and asking it here
 * keeps them from drifting into two different definitions of "interior". */
static int blob_interior(const World *w, int tx, int ty, int t)
{
    return terr_at(w, tx - 1, ty) == t && terr_at(w, tx + 1, ty) == t &&
           terr_at(w, tx, ty - 1) == t && terr_at(w, tx, ty + 1) == t;
}


/* ---- Collision ----------------------------------------------------------
 *
 * THE invariant of this codebase, stated once: tile_blocked reads `solid`,
 * `regions[].terrain` and the ability mask. Nothing else. Ground type, canopy
 * density, sprite identity, trails and fog are all render-only, so changing any
 * of them cannot change what is walkable, and every reachability proof stays
 * valid across a rendering change without being re-argued.
 */
static int solid_at(const World *w, int tx, int ty)
{
    /* Outside the world is wall, so nothing can escape the grid. */
    if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
        return 1;
    return w->solid[ty][tx];
}

static int tile_blocked(const World *w, Uint8 abilities, int tx, int ty)
{
    Uint8 reg;

    if (solid_at(w, tx, ty))
        return 1;
    reg = w->region[ty][tx];
    if (reg == REGION_NONE)
        return 0;
    return (terrain_requires[w->regions[reg].terrain] & ~abilities) != 0;
}

/* The player's collision box is her FEET, not her sprite: 10x8 px at the ground
 * point, against a 13x25 px sprite. A character whose whole silhouette collided
 * could not stand under a canopy or against a trunk, which is most of what this
 * forest is made of. */
#define FOOT_W 10
#define FOOT_H  8

/* The box is CENTRED on the ground point, not hung above it, and that is the
 * load-bearing detail.
 *
 * Anchoring it as [cy-FOOT_H, cy) - "her body is above her feet" - is the
 * intuitive reading and is wrong, because the pathfinder models the player as a
 * POINT on a tile while collision gives her an extent. A box hung above the
 * ground point occupies the top half of its tile, so walking west along a row
 * of open tiles it permanently intrudes into the row above; where that row is a
 * wall, the player freezes against a tile the pathfinder correctly calls open.
 * That is exactly how it presented: an autopilot stuck at (1093.22, 1080.00),
 * 0 of 10 collectibles, on every seed.
 *
 * Centred, the box lies inside one tile whenever the ground point is in the
 * middle FOOT_H px of it, and straddles two only when genuinely near the edge.
 * Half-open on both sides so a box edge exactly on a tile boundary belongs to
 * the tile it is inside, not the one it merely touches. */
static void foot_box(float cx, float cy, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = (int)SDL_floorf((cx - FOOT_W * 0.5f + 0.001f) / TILE);
    *x1 = (int)SDL_floorf((cx + FOOT_W * 0.5f - 0.001f) / TILE);
    *y0 = (int)SDL_floorf((cy - FOOT_H * 0.5f + 0.001f) / TILE);
    *y1 = (int)SDL_floorf((cy + FOOT_H * 0.5f - 0.001f) / TILE);
}

static int player_blocked(const World *w, Uint8 abilities, float cx, float cy)
{
    int x0, y0, x1, y1, tx, ty;

    foot_box(cx, cy, &x0, &y0, &x1, &y1);
    for (ty = y0; ty <= y1; ty++)
        for (tx = x0; tx <= x1; tx++)
            if (tile_blocked(w, abilities, tx, ty))
                return 1;
    return 0;
}

/* Did an ABILITY GATE refuse this position, rather than a wall? Returns the
 * ability bits she is missing, or 0 if the position is open, or is solid and
 * always will be.
 *
 * The distinction is invisible from a standstill: a pond she cannot wade and a
 * boulder she can never move stop her identically. One is a lock she will open
 * later and the other is scenery, and nothing in the build says which. Until
 * the gate art lands this is the only thing that does - it drives the deny
 * sound and the "you need X" toast, both of which are pure feedback and touch
 * no collision state, so THE invariant above is untouched. */
static Uint8 gate_refusal(const World *w, Uint8 abilities, float cx, float cy)
{
    int x0, y0, x1, y1, tx, ty;
    Uint8 missing = 0;

    if (!player_blocked(w, abilities, cx, cy)) return 0;
    if (player_blocked(w, ABIL_ALL, cx, cy))   return 0;  /* a wall, not a gate */

    foot_box(cx, cy, &x0, &y0, &x1, &y1);
    for (ty = y0; ty <= y1; ty++)
        for (tx = x0; tx <= x1; tx++) {
            Uint8 reg;
            if (solid_at(w, tx, ty)) continue;
            reg = w->region[ty][tx];
            if (reg == REGION_NONE) continue;
            missing |= (Uint8)(terrain_requires[w->regions[reg].terrain] & ~abilities);
        }
    return missing;
}

/* Did a HARD WALL refuse this position, and is it one the Fantasy Forest has a
 * sentence for?
 *
 * gate_refusal above answers for the LOCKS - a region tagged TERRAIN_WATER she
 * cannot wade yet. It deliberately returns 0 for anything solid, because a
 * boulder is scenery and no ability will ever move it. That left the two walls
 * the player meets most often completely mute: a pond, which world_gen makes
 * solid outright (GT_WATER, never a region at all), and the map's border ring.
 * Both stop her dead against something she can plainly see, and the game said
 * nothing - the same complaint that produced gate_refusal, one layer down.
 *
 * Only those two. An ordinary rock outcrop stays silent on purpose: it is the
 * common case, it is unmistakably a rock, and reporting it would fire on every
 * trunk-lined path in the area.
 *
 * FOREST ONLY, and asked of w->biome rather than Game.area so this stays a
 * property of the world the way world_border and portal drawing already are.
 * The Underworld and Lumiara have their own materials and their own vocabulary;
 * "the water is too deep" is a sentence about a forest pond.
 *
 * Pure feedback, exactly like gate_refusal: it reads solid[][] and terr[][] and
 * writes nothing, so THE invariant at the top of this section is untouched and
 * no reachability proof has to be re-argued.
 *
 * Mutually exclusive with gate_refusal BY CONSTRUCTION, not by convention: that
 * one returns early when player_blocked(ABIL_ALL) is true, this one returns
 * early when it is false. Exactly one of them can speak for a given position,
 * so the two can never double-report - and --move-test asserts it. */
static Uint8 wall_refusal(const World *w, float cx, float cy)
{
    int x0, y0, x1, y1, tx, ty;
    Uint8 hit = 0;

    if (w->biome != BIOME_FOREST)             return 0;
    if (!player_blocked(w, ABIL_ALL, cx, cy)) return 0;   /* open, or a gate */

    foot_box(cx, cy, &x0, &y0, &x1, &y1);
    for (ty = y0; ty <= y1; ty++)
        for (tx = x0; tx <= x1; tx++) {
            if (!solid_at(w, tx, ty)) continue;
            /* Off-grid counts as the edge of the world. The border ring makes
             * that unreachable in a generated world, but foot_box is arithmetic
             * on a float and this is the one branch that would index terr[][]
             * out of bounds if it ever were reachable. */
            if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H ||
                world_border(tx, ty))
                hit |= ABIL_CLIMB;
            else if (w->terr[ty][tx] == GT_WATER)
                hit |= ABIL_WADE;
        }
    return hit;
}

/* The one question the movement code asks: what, if anything, refused this
 * position, expressed as the ability that would have opened it. A gate first,
 * because a gate is a lock she can still open and is the more useful thing to
 * be told; a wall only when there is no gate to report. One function so the
 * self-test drives exactly what the game runs. */
static Uint8 refusal_at(const World *w, Uint8 abilities, float cx, float cy)
{
    Uint8 miss = gate_refusal(w, abilities, cx, cy);
    return miss ? miss : wall_refusal(w, cx, cy);
}

/* Swept AABB in sub-pixel steps, one axis at a time.
 *
 * Stepping rather than test-and-revert means a fast player cannot tunnel
 * through a one-tile wall. Resolving the two axes INDEPENDENTLY and
 * sequentially is what gives wall-sliding for free: blocked in x, the y call
 * still runs, so walking into a trunk diagonally slides along it. */
static void move_axis(const World *w, Player *p, float dx, float dy)
{
    float remaining = (dx != 0.0f) ? dx : dy;

    while (SDL_fabsf(remaining) > 0.0001f) {
        float step = remaining;
        float nx, ny;
        if (step >  0.5f) step =  0.5f;
        if (step < -0.5f) step = -0.5f;
        remaining -= step;

        nx = p->x + (dx != 0.0f ? step : 0.0f);
        ny = p->y + (dy != 0.0f ? step : 0.0f);
        if (player_blocked(w, p->abilities, nx, ny))
            return;
        p->x = nx;
        p->y = ny;
    }
}

/* ---- The region graph ---------------------------------------------------
 *
 * One adjacency function, asked by every traversal, so walk-reachability and
 * graph-reachability cannot come apart by consulting different neighbours. */
static int tile_neighbours(const World *w, int idx, int *out)
{
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    int x = idx % WORLD_W, y = idx / WORLD_W, d, n = 0;

    for (d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
        if (w->solid[ny][nx]) continue;
        out[n++] = ny * WORLD_W + nx;
    }
    return n;
}

/* Flood the open component containing `start`; returns its tile count. */
static int flood_open(const World *w, int start, Uint8 *seen, int *queue)
{
    int head = 0, tail = 0, count = 0;

    SDL_memset(seen, 0, (size_t)WORLD_W * WORLD_H);
    if (start < 0 || w->solid[start / WORLD_W][start % WORLD_W])
        return 0;
    seen[start] = 1;
    queue[tail++] = start;
    while (head < tail) {
        int nb[4], n, i, idx = queue[head++];
        count++;
        n = tile_neighbours(w, idx, nb);
        for (i = 0; i < n; i++)
            if (!seen[nb[i]]) { seen[nb[i]] = 1; queue[tail++] = nb[i]; }
    }
    return count;
}

/* Multi-source BFS over open tiles. Fills hop distance (-1 unreachable) and,
 * optionally, which source owns each tile. Because every source expands in
 * lockstep, each owner set is connected BY CONSTRUCTION. */
static void bfs_open(const World *w, const int *sources, int nsrc,
                     int *dist, Uint8 *owner, int *queue)
{
    int head = 0, tail = 0, i;

    for (i = 0; i < WORLD_W * WORLD_H; i++) {
        dist[i] = -1;
        if (owner) owner[i] = REGION_NONE;
    }
    for (i = 0; i < nsrc; i++) {
        int s = sources[i];
        if (s < 0 || dist[s] >= 0) continue;
        dist[s] = 0;
        if (owner) owner[s] = (Uint8)i;
        queue[tail++] = s;
    }
    while (head < tail) {
        int nb[4], n, k, idx = queue[head++];
        n = tile_neighbours(w, idx, nb);
        for (k = 0; k < n; k++) {
            if (dist[nb[k]] >= 0) continue;
            dist[nb[k]] = dist[idx] + 1;
            if (owner) owner[nb[k]] = owner[idx];
            queue[tail++] = nb[k];
        }
    }
}

/* Single-source BFS over tiles she can actually WALK, honouring the ability
 * gates - as opposed to bfs_open above, which ignores them and answers about
 * the open component. Fills hop distance, -1 for "cannot get there from here
 * with what she is holding".
 *
 * Shipping code, not verification scaffolding: the map screen's trails are
 * walked back down this field, and --play-test's headless playthrough drives
 * the same function. Two distance fields - one for the route the map draws and
 * one for the route the test proves walkable - would only need to disagree
 * once for the map to promise a way through a gate.
 *
 * Asks tile_blocked, which reads solid[][] and regions[].terrain and nothing
 * else, so what the map routes through is exactly what stops her. */
static void bfs_gated(const World *w, Uint8 abilities, int start,
                      int *dist, int *queue)
{
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    int head = 0, tail = 0, i;

    for (i = 0; i < WORLD_W * WORLD_H; i++) dist[i] = -1;
    if (start < 0) return;
    if (tile_blocked(w, abilities, start % WORLD_W, start / WORLD_W)) return;
    dist[start] = 0;
    queue[tail++] = start;
    while (head < tail) {
        int idx = queue[head++], x = idx % WORLD_W, y = idx / WORLD_W, d;
        for (d = 0; d < 4; d++) {
            int nx = x + dxs[d], ny = y + dys[d], ni;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
            ni = ny * WORLD_W + nx;
            if (dist[ni] >= 0) continue;
            if (tile_blocked(w, abilities, nx, ny)) continue;
            dist[ni] = dist[idx] + 1;
            queue[tail++] = ni;
        }
    }
}

/* Which region's restoration lights each tile - see World.litreg.
 *
 * One multi-source BFS seeded from every tile that already HAS a region,
 * expanding into the REGION_NONE ones, so each of those takes the label of the
 * nearest owned tile. A pond's middle gets the region of its nearest bank; a
 * border-ring tile gets the region of the land just inside it.
 *
 * Nearest-owned rather than "any neighbour that has one", because a pond is
 * wider than one tile: the middle of the pond in the reported screenshot is
 * four tiles from the nearest bank, so a neighbour test would have lit its rim
 * and left its centre grey - a worse artifact than the uniform grey it
 * replaced. A BFS costs one pass and is right at any size.
 *
 * Tiles that already have a region keep it, unchanged: this only ever fills in
 * the gaps, so on the walkable map litreg and region are the same array. */
static void regions_light(World *w, Scratch *sc)
{
    int *dist = sc->dist, *queue = sc->queue;
    int head = 0, tail = 0, i;

    for (i = 0; i < WORLD_W * WORLD_H; i++) {
        Uint8 r = w->region[i / WORLD_W][i % WORLD_W];
        w->litreg[i / WORLD_W][i % WORLD_W] = r;
        dist[i] = (r == REGION_NONE) ? -1 : 0;
        if (r != REGION_NONE)
            queue[tail++] = i;
    }
    while (head < tail) {
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };
        int idx = queue[head++];
        int x = idx % WORLD_W, y = idx / WORLD_W, d;
        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d], n;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
            n = ny * WORLD_W + nx;
            if (dist[n] >= 0) continue;
            /* Unlike tile_neighbours this walks THROUGH solid tiles - reaching
             * them is the entire purpose. Nothing here writes solid[][] or
             * region[][], so collision cannot be affected by it. */
            dist[n] = dist[idx] + 1;
            w->litreg[ny][nx] = w->litreg[y][x];
            queue[tail++] = n;
        }
    }
}

/* Derive each region's tile count and the adjacency graph from region[][].
 *
 * Split out of regions_build because the gate ridges below cut tiles OUT of the
 * partition after it is built, and the graph has to be re-derived from what is
 * left. Two copies of this loop would be two definitions of "which regions
 * touch", and the whole gating proof rests on that answer being one thing.
 *
 * The lighting labels are rebuilt here for exactly that reason: this is the one
 * place the partition is re-derived, so hanging litreg off it is what keeps the
 * two from ever describing different maps. */
static void regions_relink(World *w, Scratch *sc)
{
    int x, y, i;

    for (i = 0; i < REGION_COUNT; i++) {
        w->regions[i].tiles = 0;
        w->regions[i].adj = 0;
    }
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            Uint8 o = w->region[y][x];
            if (o != REGION_NONE) w->regions[o].tiles++;
        }
    /* Adjacency, recorded BOTH ways so the graph is symmetric by construction
     * rather than by remembering to add the reverse edge. */
    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            Uint8 a = w->region[y][x];
            if (a == REGION_NONE) continue;
            if (x + 1 < WORLD_W) {
                Uint8 b = w->region[y][x + 1];
                if (b != REGION_NONE && b != a) {
                    w->regions[a].adj |= 1u << b;
                    w->regions[b].adj |= 1u << a;
                }
            }
            if (y + 1 < WORLD_H) {
                Uint8 b = w->region[y + 1][x];
                if (b != REGION_NONE && b != a) {
                    w->regions[a].adj |= 1u << b;
                    w->regions[b].adj |= 1u << a;
                }
            }
        }
    }
    regions_light(w, sc);
}

/* Farthest-point sampling on PATH distance, not straight-line distance: the
 * next region seed is the open tile hardest to reach from every seed so far.
 * On a map whose walkable space winds between ponds and outcrops, straight-line
 * sampling would put two seeds either side of a rock ridge and call them
 * neighbours. */
static void regions_build(World *w, Scratch *sc)
{
    int sources[REGION_COUNT];
    int nsrc = 0, i, x, y;

    for (i = 0; i < REGION_COUNT; i++) {
        w->regions[i].terrain = TERRAIN_NORMAL;
        w->regions[i].tiles = 0;
        w->regions[i].seed_tile = -1;
        w->regions[i].adj = 0;
        w->regions[i].restoration = 0.0f;
        w->regions[i].restore_to = 0.0f;
    }
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->region[y][x] = w->litreg[y][x] = REGION_NONE;

    /* A world with no spawn has no partition, so it has no lighting labels
     * either - both arrays are already cleared above, which is what
     * tile_level's REGION_NONE branch expects. */
    if (w->spawn_tile < 0) { w->region_count = 0; w->spawn_region = -1; return; }
    sources[nsrc++] = w->spawn_tile;

    while (nsrc < REGION_COUNT) {
        int best = -1, best_d = 0;
        bfs_open(w, sources, nsrc, sc->dist, NULL, sc->queue);
        for (i = 0; i < WORLD_W * WORLD_H; i++)
            if (sc->dist[i] > best_d) { best_d = sc->dist[i]; best = i; }
        if (best < 0) break;          /* component fully covered */
        sources[nsrc++] = best;
    }

    bfs_open(w, sources, nsrc, sc->dist, sc->owner, sc->queue);
    w->region_count = nsrc;
    for (i = 0; i < nsrc; i++)
        w->regions[i].seed_tile = sources[i];
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->region[y][x] = sc->owner[y * WORLD_W + x];
    regions_relink(w, sc);
    w->spawn_region = w->region[w->spawn_tile / WORLD_W][w->spawn_tile % WORLD_W];
}

/* Hop distance from the spawn region through the region graph, IGNORING gates -
 * used only to decide where gated terrain is allowed to go. */
static void regions_depth(const World *w, int *depth)
{
    int queue[REGION_COUNT], head = 0, tail = 0, i;

    for (i = 0; i < REGION_COUNT; i++) depth[i] = -1;
    if (w->spawn_region < 0 || w->spawn_region >= w->region_count) return;
    depth[w->spawn_region] = 0;
    queue[tail++] = w->spawn_region;
    while (head < tail) {
        int r = queue[head++];
        for (i = 0; i < w->region_count; i++) {
            if (!(w->regions[r].adj & (1u << i))) continue;
            if (depth[i] >= 0) continue;
            depth[i] = depth[r] + 1;
            queue[tail++] = i;
        }
    }
}

/* Gates go deeper into the map, never on the spawn or its neighbours.
 *
 * The depth<=1 exemption is load-bearing rather than polite: a gate you arrive
 * INSIDE is not a gate, it is a wall behind you. */
static void regions_assign_terrain(World *w, Rng *rng, const int *depth)
{
    int i, max_depth = 0;

    for (i = 0; i < w->region_count; i++)
        if (depth[i] > max_depth) max_depth = depth[i];
    for (i = 0; i < w->region_count; i++) {
        w->regions[i].terrain = TERRAIN_NORMAL;
        if (i == w->spawn_region || depth[i] <= 1 || max_depth == 0)
            continue;
        {
            float t = (float)depth[i] / (float)max_depth;
            if (rng_float(rng) < t * 0.75f)
                w->regions[i].terrain =
                    (Uint8)(TERRAIN_WATER + (int)(rng_float(rng) * 3.0f) % 3);
        }
    }
}

/* ---- Gate ridges --------------------------------------------------------
 *
 * THE BUG THIS EXISTS TO FIX. A terrain tag gates a WHOLE REGION, and the
 * partition is a multi-source BFS Voronoi over open ground - so the line where
 * a gate begins is a Voronoi cell edge, which is to say an arbitrary line
 * through whatever happened to be there. Walking across a flat green clearing
 * she stopped dead against nothing at all, and the game said "too steep to
 * climb". Nothing was drawn there because nothing WAS there: `terr` knew about
 * grass and `regions[].terrain` knew about a ledge, and the two had never been
 * introduced.
 *
 * The fix is to give that line a body. Rock is laid along the frontier wherever
 * the requirement changes, leaving one pass open where the two regions meet, so
 * the gate stops being a line on flat ground and becomes a cliff with a way up
 * it. She can then SEE the boundary before she reaches it, and the refusal
 * arrives at a narrow rocky pass where "too steep to climb" is a sentence about
 * something visible.
 *
 * This is generation, not rendering: it writes `terr` and `solid`, which is
 * what makes it show up on screen at all. tile_blocked still reads only
 * solid[][], regions[].terrain and the ability mask - the collision invariant
 * is untouched, and every reachability proof stays a re-run rather than a
 * re-argument. What DOES change is that carving can sever things, so nothing
 * here is trusted: the caller verifies and rolls back.
 */
/* How far from its anchor the pass stays open, in tiles.
 *
 * ONE, giving a gap about three tiles across where the frontier runs straight -
 * 48 px against a 10 px foot box, comfortable to walk and readable as a way
 * through rather than a slot. The figure is measured, not chosen: carving down
 * to a single tile of gap took so much of each frontier that it severed
 * regions, and the verifier below rolled the ridges back often enough to cost
 * two seeds in twenty their gating entirely. */
#define GATE_PASS_R 1

/* How far she can be expected to look for the reason she was stopped. Two tiles
 * is 32 px on a 480 px frame, against a view thirty tiles wide - a barrier
 * further off than that is not what is in front of her. Both the repair below
 * and --gating-test's measurement are written against this one number, so the
 * bar the generator meets and the bar the test checks cannot drift apart. */
#define GATE_LOOK 2

/* Repair sweeps. Walling a crossing removes it, and cannot create a new one -
 * carving only ever takes open tiles away - so this converges immediately in
 * practice. Bounded anyway: an unbounded loop in world generation is a hang, and
 * a hang is worse than a wall in the wrong place. */
#define GATE_REPAIR_SWEEPS 4

static Uint8 region_requires(const World *w, Uint8 r)
{
    if (r == REGION_NONE || (int)r >= w->region_count)
        return 0;
    return terrain_requires[w->regions[r].terrain];
}

/* Which side of an a|b boundary the rock belongs on, or -1 if the two regions
 * gate identically and there is no boundary to draw.
 *
 * The LARGER side, which is a robustness choice and not an aesthetic one - on
 * screen the ridge sits on the line either way and there is nothing to tell
 * apart. Carving peels a region's outer ring away along the boundary, and a
 * region only a tile or two thick there comes apart when it loses it; the
 * verifier then rejects the attempt and the whole world is generated again.
 * Taking the bigger of the two puts that strain on whichever region can best
 * absorb it. Measured over fifty seeds: carving the gated side regardless of
 * size cost eight of them their gating and drove the worst seed into all 64
 * attempts, where this holds the same worlds at one relaxed seed.
 *
 * Ties go to the higher index - arbitrary, but it has to be decided the same
 * way every time, or the counting pass and the carving pass below would
 * disagree about which tiles belong to which frontier. */
static int gate_carve_side(const World *w, Uint8 a, Uint8 b)
{
    Uint8 ra = region_requires(w, a), rb = region_requires(w, b);

    if (ra == rb) return -1;
    if (w->regions[a].tiles != w->regions[b].tiles)
        return w->regions[a].tiles > w->regions[b].tiles ? (int)a : (int)b;
    return a > b ? (int)a : (int)b;
}

/* If this tile is on the rock side of a gate frontier, which region it faces -
 * the LOWEST such, so a tile touching two gated neighbours still belongs to
 * exactly one frontier and its position along that frontier is well defined. */
static int gate_frontier_pair(const World *w, int x, int y)
{
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    Uint8 a = w->region[y][x];
    int d, best = -1;

    if (a == REGION_NONE) return -1;
    for (d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        Uint8 b;
        if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
        b = w->region[ny][nx];
        if (b == REGION_NONE || b == a) continue;
        if (gate_carve_side(w, a, b) != (int)a) continue;
        if (best < 0 || (int)b < best) best = (int)b;
    }
    return best;
}

/* Lay the ridges. Caller must regions_relink() afterwards - the partition has
 * lost tiles and the graph no longer describes it.
 *
 * Separate passes rather than one, because the frontier must be classified
 * against the map as it stood BEFORE any of it was cut: a tile carved early
 * becomes REGION_NONE, which changes what its neighbours look like, and a
 * single mutating pass would count a frontier of one length and then walk a
 * different one - putting the pass in the wrong place, or nowhere. */
static void gate_ridges(World *w, Uint64 seed)
{
    int   total[REGION_COUNT][REGION_COUNT];
    int   pick[REGION_COUNT][REGION_COUNT];
    int   seen[REGION_COUNT][REGION_COUNT];
    short ax[REGION_COUNT][REGION_COUNT], ay[REGION_COUNT][REGION_COUNT];
    Uint8 carve[WORLD_H][(WORLD_W + 7) / 8];
    int x, y, i, j;

    for (i = 0; i < REGION_COUNT; i++)
        for (j = 0; j < REGION_COUNT; j++) {
            total[i][j] = 0;
            seen[i][j] = 0;
            ax[i][j] = ay[i][j] = -1;
        }
    SDL_memset(carve, 0, sizeof carve);

    /* 1. How long is each frontier. */
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            int b = gate_frontier_pair(w, x, y);
            if (b >= 0) total[w->region[y][x]][b]++;
        }

    /* 2. Which tile along it the pass is anchored on. From tile_hash rather
     * than the generator RNG, so moving a pass cannot shift entity placement
     * downstream.
     *
     * Held back from the ENDS of the frontier, which is not tidiness. A
     * frontier ends where the gated region tapers out, so a pass anchored there
     * has ridge on one side and open ground on the other - she walks up the
     * outside of the wall, reaches the point where it stops, and is refused by
     * a tile with nothing next to it. That is the same invisible wall in
     * miniature, and it was what the last unexplained crossings turned out to
     * be. Anchored inside the run, a pass has ridge on both sides by
     * construction. The clamp keeps the index on the frontier when it is too
     * short to hold a margin at all. */
    for (i = 0; i < REGION_COUNT; i++)
        for (j = 0; j < REGION_COUNT; j++) {
            int n = total[i][j];
            if (n <= 0)                  pick[i][j] = -1;
            else if (n <= 2 * GATE_PASS_R) pick[i][j] = n / 2;
            else pick[i][j] = GATE_PASS_R
                            + (int)(tile_hash(seed ^ 0x6A7EULL, i, j)
                                    % (Uint32)(n - 2 * GATE_PASS_R));
        }

    /* 3. Find that tile's COORDINATES. */
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            int a, b = gate_frontier_pair(w, x, y);
            if (b < 0) continue;
            a = w->region[y][x];
            if (seen[a][b]++ == pick[a][b]) {
                ax[a][b] = (short)x;
                ay[a][b] = (short)y;
            }
        }

    /* 4. Carve everything but the tiles AROUND that anchor.
     *
     * A spatial neighbourhood, not a window over the scan order, and that is
     * the whole of the difference between a pass and two holes. The scan is
     * row-major, so consecutive positions along a frontier are only ever
     * neighbours while the frontier runs down a column; where it turns and
     * picks up again further along a row, the next position is somewhere else
     * entirely. An ordinal window straddling one of those turns left a tile of
     * gap at each end of the jump, each with no carved rock beside it and
     * nothing to say why she was stopped - which is exactly what the earlier
     * version measured as its last two unexplained crossings. */
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            int a, b = gate_frontier_pair(w, x, y);
            int dx, dy;
            if (b < 0) continue;
            a = w->region[y][x];
            dx = x - ax[a][b];
            dy = y - ay[a][b];
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            /* The pass. Leaving it open is what keeps regions a and b ADJACENT
             * in the graph, and the whole completability proof is a statement
             * about that graph - a frontier walled end to end would silently
             * delete an edge the placement had already relied on. */
            if (ax[a][b] >= 0 && dx <= GATE_PASS_R && dy <= GATE_PASS_R)
                continue;
            carve[y][x >> 3] |= (Uint8)(1u << (x & 7));
        }

    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            if (carve[y][x >> 3] & (1u << (x & 7))) {
                w->terr[y][x] = GT_ROCK;
                w->solid[y][x] = 1;
                w->region[y][x] = REGION_NONE;
            }
}

/* Wall off any crossing the ridges left unexplained.
 *
 * Passes belonging to two DIFFERENT region pairs can come out next to each
 * other. Where three regions meet along one stretch, each pair earns its own
 * gap, and two gaps side by side merge into an opening far too wide to read as
 * a pass - she walks through the middle of it with the nearest rock several
 * tiles away on either side, which is the original bug again at a smaller
 * scale. Placing passes so that can never happen is a constraint problem across
 * the whole map; measuring the frontier afterwards and walling what is left
 * over is not. The property wanted is "no crossing without a visible reason",
 * so this enforces exactly that and nothing more.
 *
 * It walls the GATED side, turning a crossing into an ordinary rock face. That
 * can shut a pass completely and delete a graph edge with it, which is why -
 * like every other part of this step - the caller verifies afterwards and rolls
 * the whole thing back rather than trusting it. */
static void gate_repair(World *w)
{
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    Uint8 carve[WORLD_H][(WORLD_W + 7) / 8];
    int sweep, x, y, d, ox, oy;

    for (sweep = 0; sweep < GATE_REPAIR_SWEEPS; sweep++) {
        int any = 0;
        SDL_memset(carve, 0, sizeof carve);
        for (y = 0; y < WORLD_H; y++) {
            for (x = 0; x < WORLD_W; x++) {
                Uint8 a = w->region[y][x];
                int seen_solid = 0;
                if (w->solid[y][x] || a == REGION_NONE) continue;
                for (oy = -GATE_LOOK; oy <= GATE_LOOK && !seen_solid; oy++)
                    for (ox = -GATE_LOOK; ox <= GATE_LOOK; ox++)
                        if (solid_at(w, x + ox, y + oy)) { seen_solid = 1; break; }
                if (seen_solid) continue;
                for (d = 0; d < 4; d++) {
                    int nx = x + dx[d], ny = y + dy[d];
                    if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
                    if (w->solid[ny][nx]) continue;
                    if (!(region_requires(w, w->region[ny][nx]) & ~region_requires(w, a)))
                        continue;
                    carve[ny][nx >> 3] |= (Uint8)(1u << (nx & 7));
                    any = 1;
                }
            }
        }
        if (!any) return;
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                if (carve[y][x >> 3] & (1u << (x & 7))) {
                    w->terr[y][x] = GT_ROCK;
                    w->solid[y][x] = 1;
                    w->region[y][x] = REGION_NONE;
                }
    }
}

/* Is every region still one connected piece?
 *
 * Carving a frontier can cut a thin region in half, and the graph would not
 * notice: adjacency is about labels touching, so both halves still report the
 * same region id and the same edges. What breaks is the WALK - an entity in the
 * severed half is unreachable while every graph-level proof says it is fine.
 * That is the one failure the existing verification cannot see, so it is
 * checked directly, and it is why the ridges are rolled back rather than
 * trusted. */
static int regions_intact(const World *w, Scratch *sc)
{
    int first[REGION_COUNT];
    int i, x, y;

    for (i = 0; i < REGION_COUNT; i++) first[i] = -1;
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            Uint8 o = w->region[y][x];
            if (o != REGION_NONE && first[o] < 0) first[o] = y * WORLD_W + x;
        }

    for (i = 0; i < w->region_count; i++) {
        int head = 0, tail = 0, count = 0;
        if (w->regions[i].tiles == 0) continue;
        if (first[i] < 0) return 0;
        SDL_memset(sc->seen, 0, (size_t)WORLD_W * WORLD_H);
        sc->seen[first[i]] = 1;
        sc->queue[tail++] = first[i];
        while (head < tail) {
            int nb[4], n, k, idx = sc->queue[head++];
            count++;
            n = tile_neighbours(w, idx, nb);
            for (k = 0; k < n; k++) {
                if (sc->seen[nb[k]]) continue;
                if (w->region[nb[k] / WORLD_W][nb[k] % WORLD_W] != i) continue;
                sc->seen[nb[k]] = 1;
                sc->queue[tail++] = nb[k];
            }
        }
        if (count != (int)w->regions[i].tiles) return 0;
    }
    return 1;
}

/* Which regions are reachable holding `abilities`, per the GRAPH. */
static Uint32 regions_reachable(const World *w, Uint8 abilities)
{
    Uint32 visited;
    int queue[REGION_COUNT], head = 0, tail = 0, i;
    int start = w->spawn_region;

    if (start < 0 || start >= w->region_count) return 0;
    if (terrain_requires[w->regions[start].terrain] & ~abilities) return 0;

    visited = 1u << start;
    queue[tail++] = start;
    while (head < tail) {
        int r = queue[head++];
        for (i = 0; i < w->region_count; i++) {
            if (!(w->regions[r].adj & (1u << i))) continue;
            if (visited & (1u << i)) continue;
            if (terrain_requires[w->regions[i].terrain] & ~abilities) continue;
            visited |= 1u << i;
            queue[tail++] = i;
        }
    }
    return visited;
}

/* Which regions are reachable holding `abilities`, per an actual WALK using the
 * real tile_blocked. Comparing this against regions_reachable is the check that
 * the model and the game agree - see --gating-test.
 *
 * Self-test only: the shipping game never needs to ask, and leaving it ungated
 * cost a -Wunused-function warning that nothing enforced. It is enforced now -
 * build.ps1 passes -Werror. */
#if WAYFARER_SELFTEST
static Uint32 walk_regions(const World *w, Uint8 abilities, int start_tile,
                           Uint8 *seen, int *queue)
{
    int head = 0, tail = 0;
    Uint32 mask = 0;

    SDL_memset(seen, 0, (size_t)WORLD_W * WORLD_H);
    if (start_tile < 0) return 0;
    if (tile_blocked(w, abilities, start_tile % WORLD_W, start_tile / WORLD_W))
        return 0;
    seen[start_tile] = 1;
    queue[tail++] = start_tile;
    while (head < tail) {
        int idx = queue[head++];
        int x = idx % WORLD_W, y = idx / WORLD_W, d;
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };
        Uint8 r = w->region[y][x];
        if (r != REGION_NONE) mask |= 1u << r;
        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d], ni;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
            ni = ny * WORLD_W + nx;
            if (seen[ni]) continue;
            if (tile_blocked(w, abilities, nx, ny)) continue;
            seen[ni] = 1;
            queue[tail++] = ni;
        }
    }
    return mask;
}
#endif /* WAYFARER_SELFTEST */

/* ---- Placement and the completability proof ------------------------------
 *
 * Reservoir sampling: one pass over the region's tiles, uniform, no temporary
 * list and no second scan to count first.
 */
static int pick_tile_in_region(const World *w, Rng *rng, int region)
{
    int chosen = -1, seen = 0, x, y;

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            if (w->region[y][x] != region) continue;
            if (w->solid[y][x]) continue;
            seen++;
            if (rng_below(rng, (Uint32)seen) == 0) chosen = y * WORLD_W + x;
        }
    }
    return chosen;
}

static int entity_min_dist(const Entity *ents, int upto, int tile)
{
    int best = 1 << 30, i;
    int x = tile % WORLD_W, y = tile / WORLD_W;

    for (i = 0; i < upto; i++) {
        int dx, dy, d;
        if (ents[i].tile < 0) continue;
        dx = (ents[i].tile % WORLD_W) - x;
        dy = (ents[i].tile / WORLD_W) - y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        d = dx > dy ? dx : dy;          /* Chebyshev */
        if (d < best) best = d;
    }
    return best;
}

/* Place all ENTITY_COUNT collectibles.
 *
 * The three ability grants go first, and each is placed inside what is
 * reachable BEFORE it is granted - `held` accumulates as we go. That ordering
 * is the whole reason a world can be finished: a Wade fragment sealed behind a
 * Wade gate is a dead world, and no amount of later verification can rescue it,
 * only reject it. */
static void place_entities(const World *w, Rng *rng, Entity *ents)
{
    static const Uint8 grant_order[3] = { ABIL_WADE, ABIL_CLIMB, ABIL_KINDLE };
    Uint8 held = 0;
    int i;

    for (i = 0; i < ENTITY_COUNT; i++) {
        ents[i].tile = -1;
        ents[i].region = REGION_NONE;
        ents[i].grants = 0;
        ents[i].is_soul = (Uint8)(i >= FRAGMENT_COUNT);
        ents[i].restored = 0;
    }
    if (w->region_count <= 0) return;

    for (i = 0; i < ENTITY_COUNT; i++) {
        Uint32 reach = regions_reachable(w, (i < 3) ? held : ABIL_ALL);
        int pool[REGION_COUNT], np = 0, r, tries;
        if (reach == 0) reach = 1u << (w->spawn_region >= 0 ? w->spawn_region : 0);
        for (r = 0; r < w->region_count; r++)
            if ((reach & (1u << r)) && w->regions[r].tiles > 0) pool[np++] = r;
        if (np == 0) continue;

        /* A few tries to keep entities apart, then take what we get: spacing is
         * a preference, placement is a requirement. */
        for (tries = 0; tries < 6; tries++) {
            int rr = pool[rng_below(rng, (Uint32)np)];
            int t = pick_tile_in_region(w, rng, rr);
            if (t < 0) continue;
            ents[i].tile = t;
            ents[i].region = (Uint8)rr;
            if (entity_min_dist(ents, i, t) >= ENTITY_SPACING) break;
        }
        if (i < 3) {
            ents[i].grants = grant_order[i];
            held |= grant_order[i];
        }
    }
}

/* A simulated playthrough, iterated to a fixed point: restore everything
 * currently reachable, bank the abilities that grants, see whether the frontier
 * opened. If the loop stalls with entities left, the world has a dead end.
 *
 * Because it re-derives reachability each pass, it implicitly checks EVERY
 * ability tier the player could hold, without enumerating them. */
static int world_solvable(const World *w, const Entity *ents, int *out_restored)
{
    Uint8 abilities = 0;
    Uint32 restored = 0;
    int count = 0, progressed = 1;

    while (progressed) {
        Uint32 reach = regions_reachable(w, abilities);
        int i;
        progressed = 0;
        for (i = 0; i < ENTITY_COUNT; i++) {
            if (restored & (1u << i)) continue;
            if (ents[i].tile < 0) continue;
            if (ents[i].region >= w->region_count) continue;
            if (!(reach & (1u << ents[i].region))) continue;
            restored |= 1u << i;
            abilities |= ents[i].grants;
            count++;
            progressed = 1;
        }
    }
    if (out_restored) *out_restored = count;
    return count == ENTITY_COUNT;
}


/* ---- Fog ----------------------------------------------------------------
 *
 * Two contributions, and the screen takes the STRONGER of them: sight (walking
 * reveals shape, capped well below full) and restoration (permanent,
 * region-wide, brings colour back). A restored region stays lit after you leave
 * it, because restoration is permanent and sight is not a memory of colour.
 */
#define REVEAL_TILES 9      /* sight radius in tiles */
#define REVEAL_RATE  650.0f /* reveal units (0-255) per second */
#define SIGHT_MAX    128    /* sight alone never exceeds half */
#define RESTORE_RATE 0.9f   /* region restoration per second once triggered */

static void reveal_around(World *w, float px, float py, float dt)
{
    int cx = (int)(px / TILE), cy = (int)(py / TILE), r = REVEAL_TILES;
    int tx, ty;

    for (ty = cy - r; ty <= cy + r; ty++) {
        if (ty < 0 || ty >= WORLD_H) continue;
        for (tx = cx - r; tx <= cx + r; tx++) {
            int d2, target, cur;
            if (tx < 0 || tx >= WORLD_W) continue;
            d2 = (tx - cx) * (tx - cx) + (ty - cy) * (ty - cy);
            if (d2 > r * r) continue;
            /* Quadratic taper to nothing at the rim, so the lit disc has a soft
             * edge rather than a visible circle. */
            target = SIGHT_MAX - (SIGHT_MAX * d2) / (r * r);
            cur = w->reveal[ty][tx];
            if (cur < target) {
                int step = cur + (int)(REVEAL_RATE * dt);
                /* At least one unit per tick, so integer truncation can never
                 * stall the reveal short of its target. */
                if (step <= cur) step = cur + 1;
                w->reveal[ty][tx] = (Uint8)(step > target ? target : step);
            }
        }
    }
}

/* The fog level a tile renders at: 0..FOG_LEVELS-1. */
static int tile_level(const World *w, int tx, int ty)
{
    /* litreg, NOT region: a pond and the map's rock edge are solid, so they are
     * REGION_NONE in the collision partition and would take no restoration at
     * all. Reading the LIGHTING label instead is what lets a restored area
     * bring its water and its cliffs back with the grass, rather than leaving
     * them pinned at the sight ceiling looking permanently fogged. See
     * World.litreg. */
    Uint8 rg = w->litreg[ty][tx];
    int sight = w->reveal[ty][tx];
    int restored = 0;

    if (rg != REGION_NONE)
        restored = (int)(w->regions[rg].restoration * 255.0f);
    if (restored > sight) sight = restored;
    return sight >> 3;
}

/* ---- Restoring ----------------------------------------------------------- */
#define INTERACT_RADIUS 22.0f

static int entity_in_reach(const World *w, const Entity *ents, float px, float py)
{
    int best = -1, i;
    float bestd = INTERACT_RADIUS * INTERACT_RADIUS;

    (void)w;
    for (i = 0; i < ENTITY_COUNT; i++) {
        float ex, ey, dx, dy, d2;
        if (ents[i].tile < 0 || ents[i].restored) continue;
        ex = (float)(ents[i].tile % WORLD_W) * TILE + TILE * 0.5f;
        ey = (float)(ents[i].tile / WORLD_W) * TILE + TILE * 0.5f;
        dx = ex - px; dy = ey - py;
        d2 = dx * dx + dy * dy;
        if (d2 <= bestd) { bestd = d2; best = i; }
    }
    return best;
}

/* The map fragment, on the SAME radius entities use rather than a second
 * number: two interact distances would mean a prompt that appears at one range
 * and a key that works at another. Returns 0 once it has been taken, because
 * taking it is what sets map_tile to -1 - see try_take_map. */
static int map_in_reach(const World *w, float px, float py)
{
    float ex, ey, dx, dy;

    if (w->map_tile < 0)
        return 0;
    ex = (float)(w->map_tile % WORLD_W) * TILE + TILE * 0.5f;
    ey = (float)(w->map_tile / WORLD_W) * TILE + TILE * 0.5f;
    dx = ex - px;
    dy = ey - py;
    return dx * dx + dy * dy <= INTERACT_RADIUS * INTERACT_RADIUS;
}

/* The ONE state transition for restoring something. Both the interact key and
 * (later) save-loading replay go through here, so "restored" means exactly one
 * thing and cannot mean two. */
static void apply_restore(World *w, Entity *ents, Player *p, int i,
                          int *frags, int *souls)
{
    ents[i].restored = 1;
    p->abilities |= ents[i].grants;
    if (ents[i].region < w->region_count)
        w->regions[ents[i].region].restore_to = 1.0f;
    if (ents[i].is_soul) (*souls)++;
    else                 (*frags)++;
}

/* ---- Proto-generator ----------------------------------------------------
 *
 * PHASE 2 SCAFFOLDING, and labelled as such: it exists so the renderer and the
 * autotiler have something real to draw, and phase 4 replaces it with the
 * generator tuned to the mockups' measured coverage (grass 39-43%, olive 9-10%,
 * dirt 4-6.5%, water 11-12%). It is already seeded and replayable, so tuning it
 * later is a change of thresholds rather than a change of architecture.
 *
 * Trails are walked rather than thresholded: the mockups show CONNECTED winding
 * paths, and no noise field produces those - a threshold gives disconnected
 * blotches. This is the one part of the terrain with real structure. */
#define LAT_W  13   /* coarse: where features are */
#define LAT_H  13
#define LAT2_W 25   /* medium */
#define LAT2_H 25
#define LAT3_W 49   /* fine: ~2.6 tiles per cell, so boundaries wobble at tile scale */
#define LAT3_H 49

/* ---- Why rock is a threshold and not a ridge ------------------------------
 *
 * MEASURED AND REJECTED, recorded so the next person does not re-run it.
 *
 * The mockup's stone reads as thin winding seams, one tile across, curving
 * around open grass; a one-sided threshold on a smooth field gives the whole
 * cap of a hill in that field, which is a compact blob. The obvious fix is to
 * make rock a BAND between two nearby contours - a level set has the right
 * shape by construction - and it does not work here. Six (band, octave-mix)
 * combinations were generated and looked at; none produced a seam. Two
 * findings, both from the census a few lines below:
 *
 *     rock field            ground rock%   enclosed%   LONE%
 *     rk > 0.751 (shipped)       8.4          13.9       0.9
 *     0.751 < rk < 0.800         6.1           0.9       8.9
 *
 * A "lone" cell is an outcrop tile with no rock neighbour at all, and the band
 * multiplies them by ten. That is not a cosmetic count: the rock ring has no
 * art for one tile of stone, so it draws the blob's top-left CORNER - 12 px of
 * rim - over a full rubble square, and the cell reads as a solid block with no
 * edge sitting on open grass. It is the same defect the pond's 2x2 rule exists
 * to prevent for water, and the band trades the chunky-blob look for about 710
 * of them per eight worlds.
 *
 * The deeper reason is that the ART cannot draw a seam whatever the terrain
 * does. tile_rock_ring is a 3x3 blob set for a compact outcrop with a hollow
 * middle, and tile_rock_fill covers every rock cell to within ~10 px, so a
 * one-tile-wide ridge renders as a CHAIN OF SOLID BLOCKS no matter how
 * elegantly the field was cut. Getting the mockup's seams needs ridge art -
 * end caps and curved connectors - that this sheet does not contain; it is an
 * art request, not a generation parameter.
 *
 * So the threshold stays, and the census below now reports enclosed and lone
 * so any future attempt at this can be judged in one run instead of six. */

static void world_stub(World *w, Uint64 seed)
{
    float lat_olive[LAT2_W * LAT2_H];
    float lat_water[LAT_W * LAT_H];
    float lat_water2[LAT3_W * LAT3_H];
    float lat_rock[LAT2_W * LAT2_H];
    float lat_rock2[LAT3_W * LAT3_H];
    float lat_olive2[LAT3_W * LAT3_H];
    float lat_canopy[LAT_W * LAT_H];
    Rng rng;
    int x, y, i;

    rng_seed(&rng, seed, STREAM_TERRAIN);
    land_lattice(&rng, lat_olive, LAT2_W * LAT2_H);
    land_lattice(&rng, lat_water, LAT_W * LAT_H);
    land_lattice(&rng, lat_water2, LAT3_W * LAT3_H);
    land_lattice(&rng, lat_rock, LAT2_W * LAT2_H);
    land_lattice(&rng, lat_rock2, LAT3_W * LAT3_H);
    land_lattice(&rng, lat_olive2, LAT3_W * LAT3_H);
    land_lattice(&rng, lat_canopy, LAT_W * LAT_H);

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            float fx = (float)x / (float)(WORLD_W - 1);
            float fy = (float)y / (float)(WORLD_H - 1);
            /* Two octaves, and the fine one is what makes these read as
             * terrain rather than as a contour plot. A single low-frequency
             * field has a SMOOTH boundary, and a smooth boundary quantised to
             * 16 px tiles comes out as long dead-straight runs - seed 5 drew a
             * pond with perfectly flat banks the full height of the screen.
             * The fine octave moves the threshold crossing by a tile or two at
             * tile scale, which is exactly the ragged edge the art expects. */
            float o = 0.72f * land_noise(lat_olive, LAT2_W, LAT2_H, fx, fy)
                    + 0.28f * land_noise(lat_olive2, LAT3_W, LAT3_H, fx, fy);
            float wt = 0.74f * land_noise(lat_water, LAT_W, LAT_H, fx, fy)
                     + 0.26f * land_noise(lat_water2, LAT3_W, LAT3_H, fx, fy);
            float rk = 0.70f * land_noise(lat_rock, LAT2_W, LAT2_H, fx, fy)
                     + 0.30f * land_noise(lat_rock2, LAT3_W, LAT3_H, fx, fy);
            /* Thresholds tuned against the mockups' MEASURED ground cover:
             * grass 39-43%, olive 9-10%, dirt 4-6.5%, water 11-12%, rock ~5%.
             * The order matters - water wins over rock wins over olive - so
             * each threshold is read against what the previous ones left. */
            int t = GT_GRASS;
            /* Re-tuned for the TWO-OCTAVE fields. Averaging two roughly uniform
             * octaves concentrates the result toward 0.5, so thresholds carried
             * over from the single-octave version cut far less: water fell from
             * 13% to 6% and rock from 6.5% to 1.8% on the same numbers. */
            /* Water is set from the RENDERED census, not the ground one: ponds
             * are never covered by canopy while grass often is, so water reads
             * larger on screen than its share of tiles. */
            if (wt < 0.252f)      t = GT_WATER;  /* low-frequency: rounded ponds */
            else if (rk > 0.751f) t = GT_ROCK;   /* outcrops; see the note above */
            else if (o > 0.690f)  t = GT_OLIVE;
            w->terr[y][x] = (Uint8)t;

            /* Canopy density: low frequency, so trees gather into stands with
             * clearings between them rather than scattering evenly. Squared to
             * push the midtones down, which deepens the clearings. */
            {
                float c = land_noise(lat_canopy, LAT_W, LAT_H, fx, fy);
                c = c * c;
                w->canopy[y][x] = (Uint8)(c * 255.0f);
            }
        }
    }

    /* Ponds must be at least 2x2, and this is a RENDERING constraint enforced
     * in generation because it cannot be enforced anywhere else.
     *
     * The pond rim is a 3x3 blob autotile - it has art for a corner, an edge
     * and a middle, and none for "a pond one tile across". A lone water tile
     * therefore draws the blob's TOP-LEFT CORNER over its whole cell, which is
     * an arc of bank with grass showing through the two thirds of the tile the
     * corner leaves transparent: a teal square with a grass notch bitten out of
     * it, scattered across the map. Every water cell that survives here has a
     * water neighbour on two axes, so every slice the autotile can select has
     * art that means what it draws.
     *
     * Marked from a bitset and applied afterwards, so removing one cell cannot
     * change the verdict on the next and erode a real pond from its rim inward.
     * 2 KB of stack, not a world-sized static - see the rule at the top. */
    {
        Uint8 keep[WORLD_H][(WORLD_W + 7) / 8];
        SDL_memset(keep, 0, sizeof keep);
        for (y = 0; y + 1 < WORLD_H; y++) {
            for (x = 0; x + 1 < WORLD_W; x++) {
                if (w->terr[y][x] != GT_WATER || w->terr[y][x + 1] != GT_WATER ||
                    w->terr[y + 1][x] != GT_WATER || w->terr[y + 1][x + 1] != GT_WATER)
                    continue;
                keep[y][x >> 3]           |= (Uint8)(1u << (x & 7));
                keep[y][(x + 1) >> 3]     |= (Uint8)(1u << ((x + 1) & 7));
                keep[y + 1][x >> 3]       |= (Uint8)(1u << (x & 7));
                keep[y + 1][(x + 1) >> 3] |= (Uint8)(1u << ((x + 1) & 7));
            }
        }
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                if (w->terr[y][x] == GT_WATER &&
                    !(keep[y][x >> 3] & (1u << (x & 7))))
                    w->terr[y][x] = GT_GRASS;
    }

    /* Wandering trails between random waypoints. The wander comes from
     * tile_hash rather than the RNG so trail shape cannot shift terrain. */
    for (i = 0; i < 6; i++) {
        int cx = (int)rng_below(&rng, WORLD_W);
        int cy = (int)rng_below(&rng, WORLD_H);
        int tx = (int)rng_below(&rng, WORLD_W);
        int ty = (int)rng_below(&rng, WORLD_H);
        int guard = 0;
        while ((cx != tx || cy != ty) && guard++ < WORLD_W * 4) {
            Uint32 h = tile_hash(seed ^ 0x51ED, cx, cy);
            int r = 1 + (int)((h >> 3) & 1u);
            int ox, oy;
            for (oy = -r; oy <= r; oy++) {
                for (ox = -r; ox <= r; ox++) {
                    int px = cx + ox, py = cy + oy;
                    if (px < 0 || py < 0 || px >= WORLD_W || py >= WORLD_H) continue;
                    if (ox * ox + oy * oy > r * r + 1) continue;
                    if (w->terr[py][px] == GT_WATER) continue;  /* trails do not cross ponds */
                    w->terr[py][px] = GT_DIRT;
                }
            }
            /* Step toward the target, with a hash-driven wobble so the lane
             * meanders instead of reading as a drawn line. */
            if ((h & 3u) == 0) {
                if (cx != tx) cx += (tx > cx) ? 1 : -1;
            } else if ((h & 3u) == 1) {
                if (cy != ty) cy += (ty > cy) ? 1 : -1;
            } else {
                if (cx != tx) cx += (tx > cx) ? 1 : -1;
                if (cy != ty) cy += (ty > cy) ? 1 : -1;
            }
        }
    }

    /* Collision, derived once from the finished ground map. Ponds and rock
     * outcrops block; everything else is walkable. This is the ONLY place
     * `solid` is written, so there is one answer to "what is a wall". */
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->solid[y][x] = (Uint8)(w->terr[y][x] == GT_WATER ||
                                     w->terr[y][x] == GT_ROCK);
    /* A one-tile wall around the map, so nothing can walk off the grid and no
     * traversal has to special-case the border. */
    /* The border ring is forced solid, so it has to LOOK solid. Left as
     * whatever noise put there, it was grass she could see and could not walk
     * onto - the same invisible wall as a thin rock slice, just parked at the
     * edge of the map where it is least expected. Writing terr as well as solid
     * is what keeps the two agreeing. */
    for (x = 0; x < WORLD_W; x++) {
        w->solid[0][x] = 1;              w->terr[0][x] = GT_ROCK;
        w->solid[WORLD_H - 1][x] = 1;    w->terr[WORLD_H - 1][x] = GT_ROCK;
    }
    for (y = 0; y < WORLD_H; y++) {
        w->solid[y][0] = 1;              w->terr[y][0] = GT_ROCK;
        w->solid[y][WORLD_W - 1] = 1;    w->terr[y][WORLD_W - 1] = GT_ROCK;
    }

    /* No partition yet - regions_build is a separate step, and several tests
     * deliberately stop here. Clearing both label arrays makes that a DEFINED
     * state rather than whatever the buffer held: tile_level reads one of them
     * on every tile it draws, and an uninitialised byte there indexes
     * regions[] with a value up to 255 against an array of REGION_COUNT. */
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->region[y][x] = w->litreg[y][x] = REGION_NONE;
}

/* Pick the spawn: the centre-most tile of the LARGEST open component. Largest
 * because a noise map fragments into pockets behind ponds and outcrops, and
 * spawning in a 12-tile pocket is a world with nothing in it. */
static void world_spawn(World *w, Scratch *sc)
{
    int best_start = -1, best_count = 0, x, y, i;

    for (y = 1; y < WORLD_H - 1; y++) {
        for (x = 1; x < WORLD_W - 1; x++) {
            int idx = y * WORLD_W + x, count;
            if (w->solid[y][x]) continue;
            /* Skip tiles already known to be in a counted component. */
            if (best_start >= 0 && sc->seen[idx]) continue;
            count = flood_open(w, idx, sc->seen, sc->queue);
            if (count > best_count) { best_count = count; best_start = idx; }
        }
    }
    if (best_start < 0) { w->spawn_tile = -1; return; }

    /* Re-flood the winner, then take the tile nearest its centroid. */
    (void)flood_open(w, best_start, sc->seen, sc->queue);
    {
        double sx = 0.0, sy = 0.0;
        int n = 0, bestd = 1 << 30;
        for (i = 0; i < WORLD_W * WORLD_H; i++)
            if (sc->seen[i]) { sx += i % WORLD_W; sy += i / WORLD_W; n++; }
        if (n == 0) { w->spawn_tile = best_start; return; }
        sx /= n; sy /= n;
        w->spawn_tile = best_start;
        for (i = 0; i < WORLD_W * WORLD_H; i++) {
            if (!sc->seen[i]) continue;
            {
                int dx = (int)(i % WORLD_W) - (int)sx, dy = (int)(i / WORLD_W) - (int)sy;
                int d = dx * dx + dy * dy;
                if (d < bestd) { bestd = d; w->spawn_tile = i; }
            }
        }
    }
}

/* Where the Area 1 exit stands: the first open tile of the spawn region found
 * by a ring search outward from spawn_tile, never the spawn tile itself (she
 * would otherwise spawn standing on it every game). Deterministic and RNG-free
 * - render-only, so it needs none of world_gen's collision guarantees, only a
 * valid fallback. Called for every biome; unused (never drawn, never checked)
 * in BIOME_LUMIARA - Area 3 is terminal, so its own portal_tile just sits
 * there unused - but always left valid rather than only set "when needed" -
 * see the no-garbage-fields rule this file follows for World. */
static void world_place_portal(World *w)
{
    int sx, sy, r;

    w->portal_tile = w->spawn_tile;
    if (w->spawn_tile < 0)
        return;
    sx = w->spawn_tile % WORLD_W;
    sy = w->spawn_tile / WORLD_W;
    for (r = 3; r <= 8; r++) {
        int dx, dy;
        for (dy = -r; dy <= r; dy++) {
            for (dx = -r; dx <= r; dx++) {
                int tx = sx + dx, ty = sy + dy;
                if (dx * dx + dy * dy < (r - 1) * (r - 1))
                    continue;   /* ring, not disc - do not re-check inner rings */
                if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
                    continue;
                if (w->solid[ty][tx] || w->region[ty][tx] != w->spawn_region)
                    continue;
                w->portal_tile = ty * WORLD_W + tx;
                return;
            }
        }
    }
}

/* ---- The map fragment ---------------------------------------------------
 *
 * One per biome, and deliberately NOT an eleventh entity. Restoration state is
 * a single Uint32 and three areas of ENTITY_COUNT already claim thirty of its
 * bits, so an eleventh collectible per area would not fit in the save at all -
 * that ceiling is the structural rule at the top of this file, not a
 * preference. It is tracked instead by Game.maps, three bits, one per area.
 *
 * It also stands OUTSIDE the completability proof, on purpose: it grants no
 * ability and restores no region, so world_solvable never sees it and a world
 * is finishable whether or not she ever picks it up. Nothing about gating,
 * placement or reachability had to change to admit it.
 *
 * Drawn from its OWN rng stream so that adding it cannot perturb one tile of
 * terrain or one entity position in any existing seed - the same reason
 * tile_hash is stateless - and placed inside what is reachable with NO
 * abilities, so the map is always something she can go and get rather than a
 * reward for having already finished the area she wanted it for.
 */
#define MAP_MIN_SPAWN_DIST 14   /* Chebyshev tiles: far enough to be a find */
#define MAP_MIN_ENT_DIST    3   /* never share a neighbourhood with a mote */

/* The placement rule as ONE predicate, so --map-test rejects bad tiles with the
 * same code that accepted the good one. A checker written separately from the
 * placer only ever proves the two agree about the cases someone thought of.
 *
 * `far` is the spawn-distance clause, separable because it is the one clause
 * placement is allowed to give up on - see world_place_map. */
static int map_tile_ok(const World *w, const Entity *ents, Uint32 reach,
                       int tile, int far)
{
    int x, y, i, dx, dy;

    if (tile < 0 || tile >= WORLD_W * WORLD_H) return 0;
    x = tile % WORLD_W;
    y = tile / WORLD_W;
    if (w->solid[y][x]) return 0;
    if (w->region[y][x] >= w->region_count) return 0;   /* REGION_NONE included */
    if (!(reach & (1u << w->region[y][x]))) return 0;
    if (tile == w->portal_tile) return 0;
    if (tile == w->spawn_tile) return 0;
    for (i = 0; i < ENTITY_COUNT; i++) {
        if (ents[i].tile < 0) continue;
        dx = (ents[i].tile % WORLD_W) - x; if (dx < 0) dx = -dx;
        dy = (ents[i].tile / WORLD_W) - y; if (dy < 0) dy = -dy;
        if ((dx > dy ? dx : dy) < MAP_MIN_ENT_DIST) return 0;
    }
    if (far && w->spawn_tile >= 0) {
        dx = (w->spawn_tile % WORLD_W) - x; if (dx < 0) dx = -dx;
        dy = (w->spawn_tile / WORLD_W) - y; if (dy < 0) dy = -dy;
        if ((dx > dy ? dx : dy) < MAP_MIN_SPAWN_DIST) return 0;
    }
    return 1;
}

/* Reservoir sampling over the whole map, exactly like pick_tile_in_region and
 * for the same reason: one pass, uniform, no temporary list.
 *
 * Run at most twice. The second pass drops the spawn-distance clause, and is
 * what a world too small or too hemmed in to have anything legal that far out
 * falls back to: distance from spawn is a PREFERENCE, and a map fragment that
 * failed to exist would not be. Which pass ran is visible to the test, which
 * demands the far clause wherever a qualifying tile existed at all. */
static void world_place_map(World *w, const Entity *ents, Uint64 seed)
{
    Rng rng;
    Uint32 reach = regions_reachable(w, ABIL_NONE);
    int far, chosen = -1;

    w->map_tile = -1;
    if (reach == 0)                       /* no spawn region: take anything */
        reach = regions_reachable(w, ABIL_ALL);
    rng_seed(&rng, seed, STREAM_MAP);
    for (far = 1; far >= 0 && chosen < 0; far--) {
        int seen = 0, x, y;
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++) {
                if (!map_tile_ok(w, ents, reach, y * WORLD_W + x, far)) continue;
                seen++;
                if (rng_below(&rng, (Uint32)seen) == 0) chosen = y * WORLD_W + x;
            }
    }
    w->map_tile = chosen;
}

/* Carve the ridges for the tag assignment currently on the regions, then place
 * and verify. Returns 1 for a world that is sound, 0 for one to roll back.
 *
 * The ORDER is the point. Ridges are cut before placement, so a collectible can
 * never be placed on a tile the ridge is about to turn to stone - the carved
 * tiles leave the partition first, and place_entities only ever draws from what
 * is still labelled. */
static int gate_try(World *w, Scratch *sc, Rng *rng, Entity *ents, Uint64 seed)
{
    gate_ridges(w, seed);
    gate_repair(w);
    regions_relink(w, sc);
    if (!regions_intact(w, sc))
        return 0;
    place_entities(w, rng, ents);
    return world_solvable(w, ents, NULL);
}

/* The exact inverse of a carve.
 *
 * By REGENERATING rather than journalling the tiles: world_stub is a pure
 * function of the seed, so re-running it restores terr and solid exactly, and
 * the partition that follows from them is the same partition. A rollback list
 * would be a second description of the same thing, sized by a guess, and wrong
 * in exactly the case that matters. The tags are carried across by hand because
 * regions_build clears them, and losing them here would quietly ungate the
 * world instead of retrying it. */
static void gate_rollback(World *w, Scratch *sc, Uint64 seed, const Uint8 *tags)
{
    int i;

    world_stub(w, seed);
    world_spawn(w, sc);
    regions_build(w, sc);
    for (i = 0; i < REGION_COUNT; i++)
        w->regions[i].terrain = tags[i];
}

/* Generate-then-verify, with a fallback ladder that always terminates.
 * Returns attempts used (>0), or -depth when gating had to be relaxed. */
static int world_place_and_verify(World *w, Scratch *sc, Rng *rng, const int *depth,
                                  Entity *ents, Uint64 seed)
{
    Uint8 tags[REGION_COUNT];
    int attempt, d, i;

    for (attempt = 0; attempt < 64; attempt++) {
        regions_assign_terrain(w, rng, depth);
        for (i = 0; i < REGION_COUNT; i++) tags[i] = w->regions[i].terrain;
        if (gate_try(w, sc, rng, ents, seed))
            return attempt + 1;
        gate_rollback(w, sc, seed, tags);
    }
    /* Ungate outward, shallowest first, so as much gating as possible survives.
     * Same shape as an attempt, deliberately: the ladder is the path that runs
     * when a seed is hard, which is precisely when it is least likely to have
     * been exercised, so it must not be a second, differently-written pipeline. */
    for (d = 1; d <= REGION_COUNT; d++) {
        for (i = 0; i < w->region_count; i++)
            if (depth[i] == d) w->regions[i].terrain = TERRAIN_NORMAL;
        for (i = 0; i < REGION_COUNT; i++) tags[i] = w->regions[i].terrain;
        if (gate_try(w, sc, rng, ents, seed))
            return -d;
        gate_rollback(w, sc, seed, tags);
    }
    /* Nothing gated at all, so there is no frontier to draw and no ridge to
     * carve - the world is already honest about having no walls in it. */
    for (i = 0; i < w->region_count; i++) w->regions[i].terrain = TERRAIN_NORMAL;
    place_entities(w, rng, ents);
    return -100;
}

/* The whole generation pipeline, in the one order that works.
 * Returns placement attempts used (>0), or -depth when gating had to be
 * relaxed to make the world finishable - stored so a test can report it. */
static int world_gen(World *w, Scratch *sc, Entity *ents, Uint64 seed, Uint8 biome)
{
    Rng rng;
    int depth[REGION_COUNT];
    int x, y, r;

    /* First line, unconditionally: World is heap-allocated with bare SDL_malloc
     * in several self-test call sites (not calloc, not SDL_zero), so a field
     * set "after the fact" by some callers and not others is a real garbage-read
     * risk, not a style nit. */
    w->biome = biome;
    /* Set here rather than only in world_place_map, so the field is never
     * garbage on any path: the same bare-SDL_malloc World the comment above
     * describes would otherwise let a rolled-back or half-built world be read
     * for a map tile that was never placed. */
    w->map_tile = -1;
    world_stub(w, seed);
    world_spawn(w, sc);
    regions_build(w, sc);
    /* Depth comes from the graph BEFORE any ridge is cut, and stays valid
     * across a rollback because a rollback restores that same graph. Gate
     * ridges only ever remove tiles at a frontier and always leave the pass, so
     * they cannot add or remove an edge - which is what lets one depth array
     * serve every attempt. */
    regions_depth(w, depth);
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->reveal[y][x] = 0;
    rng_seed(&rng, seed, STREAM_ENTITIES);
    r = world_place_and_verify(w, sc, &rng, depth, ents, seed);
    world_place_portal(w);
    /* Last, and after the portal: placement reads both the final entity
     * positions and portal_tile to keep clear of them. */
    world_place_map(w, ents, seed);
    return r;
}

/* ---- Props --------------------------------------------------------------
 *
 * A prop is a pure function of (seed, tile) - nothing is stored per tile, so
 * props cost zero world bytes and cannot drift out of sync with anything.
 * Presence is decided from a canopy DENSITY FIELD rather than a flat rate,
 * because the mockups show trees in stands with clearings between them, and a
 * flat rate scatters them evenly - which reads as an orchard, not a forest.
 */
enum { PROP_NONE = 0, PROP_TREE, PROP_PINE, PROP_BUSH, PROP_LOG, PROP_ROCK,
       PROP_STONE, PROP_MUSHROOM, PROP_TUFT, PROP_REED, PROP_COUNT };

typedef struct { const short *ids; int n; } PropArt;

static const short art_trees[]     = { ART_TREE_A, ART_TREE_B };
static const short art_pines[]     = { ART_PINE_A, ART_PINE_B };
static const short art_bushes[]    = { ART_BUSH_LARGE_A, ART_BUSH_LARGE_B,
                                       ART_BUSH_SMALL_A, ART_BUSH_SMALL_B };
static const short art_logs[]      = { ART_LOG_A, ART_LOG_B };
static const short art_rocks[]     = { ART_ROCK_A, ART_ROCK_B, ART_ROCK_C };
static const short art_stones[]    = { ART_STONE_A, ART_STONE_B, ART_STONE_C, ART_PEBBLES };
/* ---- Ground context ------------------------------------------------------
 *
 * Decorations.png draws its rocks and stones THREE TIMES over: once standing
 * in grass, once in dirt, and once with no ground under them at all. The three
 * share an identical stone body - measured, the pixel histograms of the rock
 * itself are the same to the pixel - and differ ONLY in the skirt of ground
 * around the base. Which one is correct is therefore not a matter of variety
 * at all; it is decided entirely by what the tile under it is.
 *
 * Picking among them by hash, which is what the flat tables above do, put a
 * grass-skirted boulder on bare dirt and a dirt-skirted one on grass, roughly
 * two times in three. That is the same class of bug as a sprite carrying its
 * authoring background into the frame: the art is right and the placement is
 * wrong. Measured skirt pixels, against the sheet's own ground colours:
 *
 *     ROCK_A   37 green    0 brown      STONE_A   19 green    0 brown
 *     ROCK_B    0 green   49 brown      STONE_B    0 green   24 brown
 *     ROCK_C    0 green    0 brown      STONE_C     0 green    0 brown
 *                                       PEBBLES     0 green    0 brown
 *
 * PEBBLES has no skirt either, so it stays available on every ground rather
 * than being reserved to one - it is the only variety left once the skirt is
 * no longer free to vary.
 *
 * GC_BARE is GT_ROCK: outcrop interiors, and the map's border ring. Both are
 * permanently solid and neither has soil in it, so the skirtless variant is the
 * only one that does not paint a patch of ground she can never stand on. */
enum { GC_GREEN = 0, GC_BROWN, GC_BARE, GC_COUNT };

static int ground_class(int t)
{
    if (t == GT_DIRT) return GC_BROWN;
    /* Rock and water both mean "no soil here". Water never carries a prop -
     * prop_at returns PROP_NONE on it - but answering for it anyway keeps this
     * a total function of GT_*, so a later caller cannot fall off the end. */
    if (t == GT_ROCK || t == GT_WATER) return GC_BARE;
    return GC_GREEN;   /* GT_GRASS, GT_OLIVE - olive is grass, see gt_leafy */
}

static const short art_rocks_green[]  = { ART_ROCK_A };
static const short art_rocks_brown[]  = { ART_ROCK_B };
static const short art_rocks_bare[]   = { ART_ROCK_C };
static const short art_stones_green[] = { ART_STONE_A, ART_PEBBLES };
static const short art_stones_brown[] = { ART_STONE_B, ART_PEBBLES };
static const short art_stones_bare[]  = { ART_STONE_C, ART_PEBBLES };
static const short art_mushrooms[] = { ART_MUSHROOM_BIG_A, ART_MUSHROOM_BIG_B, ART_MUSHROOM_BIG_C,
                                       ART_MUSHROOM_MED_A, ART_MUSHROOM_MED_B, ART_MUSHROOM_MED_C,
                                       ART_MUSHROOM_TINY_A, ART_MUSHROOM_TINY_B, ART_MUSHROOM_TINY_C };
static const short art_tufts[]     = { ART_TUFT_A, ART_TUFT_B, ART_TUFT_C, ART_TUFT_D };
static const short art_reeds[]     = { ART_REED_A, ART_REED_B, ART_REED_C, ART_REED_D };

/* Underworld analogues, same PROP_* slots: dead tree, broken tree, thorn
 * plant, dead arm (a horizontal-ish clutter piece, LOG's role), rock,
 * bones/skulls, crystal (the "glowing focal point" MUSHROOM was for),
 * small bones (TUFT), grave (REED's near-hazard-margin role). See
 * tools/bake.ps1's "Underworld decorations" block for the source files. */
static const short art_trees_uw[]     = { ART_UW_TREE_1, ART_UW_TREE_2, ART_UW_TREE_3 };
static const short art_pines_uw[]     = { ART_UW_PINE_1, ART_UW_PINE_2, ART_UW_PINE_3 };
static const short art_bushes_uw[]    = { ART_UW_BUSH_1, ART_UW_BUSH_2, ART_UW_BUSH_3 };
static const short art_logs_uw[]      = { ART_UW_LOG_1, ART_UW_LOG_2, ART_UW_LOG_3, ART_UW_LOG_4 };
static const short art_rocks_uw[]     = { ART_UW_ROCKPROP_1, ART_UW_ROCKPROP_2, ART_UW_ROCKPROP_3 };
static const short art_stones_uw[]    = { ART_UW_STONE_1, ART_UW_STONE_2, ART_UW_STONE_3 };
static const short art_mushrooms_uw[] = { ART_UW_CRYSTAL_1, ART_UW_CRYSTAL_2, ART_UW_CRYSTAL_3, ART_UW_CRYSTAL_4 };
static const short art_tufts_uw[]     = { ART_UW_TUFT_1, ART_UW_TUFT_2, ART_UW_TUFT_3, ART_UW_TUFT_4 };
static const short art_reeds_uw[]     = { ART_UW_REED_1, ART_UW_REED_2, ART_UW_REED_3 };

/* Lumiara analogues, same PROP_* slots. Only one canopy asset was delivered
 * (topdown_dream_tree.png), so TREE and PINE share it rather than one going
 * unused - PROP_PINE is picked on 1/4 of canopy rolls (see prop_at), and a
 * repeated tree is a variety loss, not a missing-art bug. See
 * tools/bake.ps1's "Lumiara decorations" block for the source files. */
static const short art_trees_lum[]     = { ART_LUM_TREE };
static const short art_pines_lum[]     = { ART_LUM_TREE };
static const short art_bushes_lum[]    = { ART_LUM_BUSH };
static const short art_logs_lum[]      = { ART_LUM_BENCH, ART_LUM_ARCHWAY };
static const short art_rocks_lum[]     = { ART_LUM_MONOLITH, ART_LUM_STATUE };
static const short art_stones_lum[]    = { ART_LUM_CHEST, ART_LUM_URN };
static const short art_mushrooms_lum[] = { ART_LUM_MUSHROOM };
static const short art_tufts_lum[]     = { ART_LUM_SIGNPOST, ART_LUM_LANTERN, ART_LUM_BANNER };
static const short art_reeds_lum[]     = { ART_LUM_JELLYFISH, ART_LUM_MANTA, ART_LUM_FOX, ART_LUM_STAG };

#define PA(t) { t, (int)(sizeof t / sizeof *t) }
static const PropArt prop_art[BIOME_COUNT][PROP_COUNT] = {
    {
        { NULL, 0 },      /* PROP_NONE */
        PA(art_trees), PA(art_pines), PA(art_bushes), PA(art_logs), PA(art_rocks),
        PA(art_stones), PA(art_mushrooms), PA(art_tufts), PA(art_reeds)
    },
    {
        { NULL, 0 },
        PA(art_trees_uw), PA(art_pines_uw), PA(art_bushes_uw), PA(art_logs_uw), PA(art_rocks_uw),
        PA(art_stones_uw), PA(art_mushrooms_uw), PA(art_tufts_uw), PA(art_reeds_uw)
    },
    {
        { NULL, 0 },
        PA(art_trees_lum), PA(art_pines_lum), PA(art_bushes_lum), PA(art_logs_lum), PA(art_rocks_lum),
        PA(art_stones_lum), PA(art_mushrooms_lum), PA(art_tufts_lum), PA(art_reeds_lum)
    }
};

/* Two rows, not a sparse PROP_COUNT one: rock and stone are the only slots
 * whose art was authored per ground, and a table that was NULL in eight rows
 * out of ten would be mostly a claim that nothing else needs this. */
static const PropArt prop_art_ground[2][GC_COUNT] = {
    { PA(art_rocks_green),  PA(art_rocks_brown),  PA(art_rocks_bare)  },   /* PROP_ROCK */
    { PA(art_stones_green), PA(art_stones_brown), PA(art_stones_bare) }    /* PROP_STONE */
};
#undef PA

/* The ground-context art for a slot, or NULL where there is none.
 *
 * Forest only. Underworld's rocks stand on cave floor and Lumiara's monoliths
 * on dream cobble - neither sheet was drawn with a ground skirt at all, so
 * there is no second variant to choose and nothing here to decide. Returning
 * NULL rather than a one-entry table says that, instead of dressing "there is
 * only one" up as a choice. */
static const PropArt *prop_ground_art(int biome, int kind, int gc)
{
    if (biome != BIOME_FOREST) return NULL;
    if (kind == PROP_ROCK)  return &prop_art_ground[0][gc];
    if (kind == PROP_STONE) return &prop_art_ground[1][gc];
    return NULL;
}

/* ---- Prop density normalisation -------------------------------------------
 *
 * prop_at's rates are biome-agnostic and were tuned against FOREST's art,
 * where PROP_STONE is a pebble (13.8 x 10.8 px mean box) and PROP_TUFT a grass
 * tuft (14 x 21). Those two slots fire often precisely BECAUSE what they place
 * is tiny. Point a biome with larger art at the same slots and the same rate
 * stops meaning the same thing. Measured mean sprite box per slot, against
 * Forest:
 *
 *     PROP_STONE   Forest  149 px^2   Underworld  737 (4.9x)   Lumiara 1306 (8.8x)
 *     PROP_TUFT    Forest  294        Underworld  432 (1.5x)   Lumiara 1264 (4.3x)
 *     PROP_ROCK    Forest  552        Underworld 2098 (3.8x)   Lumiara 1728 (3.1x)
 *     PROP_LOG     Forest 1225       Underworld 2299 (1.9x)   Lumiara 2391 (2.0x)
 *
 * so Lumiara was placing chests, urns, lantern posts and banners at pebble and
 * grass-tuft frequency, and they piled into each other on screen.
 *
 * The fix keeps ONE set of authored rates and thins the RESULT per biome, by
 * the ratio of footprints, so each biome lands near Forest's prop area per
 * unit of ground - the density that was actually art-directed. Derived from
 * ART_SPRITES at runtime rather than hand-tuned, so swapping a prop PNG for a
 * bigger or smaller one re-balances on the next bake with nothing to remember.
 * That is not hypothetical: the art that prompted this was itself a mid-project
 * swap, and the replacement monolith grew from a 24x38 box to 42x48.
 *
 * Render-only, like every other thing in this file that touches props: it
 * changes which sprites are pushed into the draw list and nothing else, so
 * solid[][], reachability and every completability proof are untouched.
 *
 * Forest is the reference and therefore always keeps 64/64 - its rendering
 * stays bit-identical to before this existed, which is what keeps
 * --mockup-test's pixel census (calibrated on Forest art) meaningful.
 */
#define PROP_ROLL_N   64   /* prop_at's presence roll is 6 bits; this matches it */
#define PROP_KEEP_MIN  6   /* a thinned slot still appears, just rarely */

static Uint8 prop_keep[BIOME_COUNT][PROP_COUNT];
static int   prop_keep_ready;

/* Mean sprite box over one slot's art. The BOX, not the opaque-pixel count:
 * two props read as crowded when their boxes overlap, and w*h is already in
 * ART_SPRITES, so this needs no RLE decode on the render path. */
static int prop_art_box(const PropArt *pa)
{
    long total = 0;
    int i;

    if (!pa->ids || pa->n <= 0)
        return 0;
    for (i = 0; i < pa->n; i++) {
        const ArtSprite *sp = &ART_SPRITES[pa->ids[i]];
        total += (long)sp->w * (long)sp->h;
    }
    return (int)(total / pa->n);
}

static void prop_keep_build(void)
{
    int b, k;

    for (k = 0; k < PROP_COUNT; k++) {
        int ref = prop_art_box(&prop_art[BIOME_FOREST][k]);
        for (b = 0; b < BIOME_COUNT; b++) {
            int box = prop_art_box(&prop_art[b][k]);
            int keep = PROP_ROLL_N;
            /* Only ever THINS. A biome whose art is smaller than Forest's keeps
             * the authored rate rather than being made denser to compensate -
             * the rates are a designed ceiling, not a budget to spend. */
            if (ref > 0 && box > ref)
                keep = (ref * PROP_ROLL_N + box / 2) / box;
            if (keep < PROP_KEEP_MIN) keep = PROP_KEEP_MIN;
            if (keep > PROP_ROLL_N)   keep = PROP_ROLL_N;
            prop_keep[b][k] = (Uint8)keep;
        }
    }
    prop_keep_ready = 1;
}

/* The tallest prop is 98 px, so a prop anchored this many tiles BELOW the
 * visible bottom can still reach into frame. Culling that ignores this clips
 * tree crowns off the top of the screen as you walk north. */
#define PROP_OVERSCAN ((98 / TILE) + 2)

/* Would a tree standing on this tile hide water?
 *
 * A tree sprite is 70x98 px anchored at its foot, so it covers roughly two
 * tiles either side and SIX ROWS ABOVE its own - a pond three tiles north of a
 * trunk is simply not on screen. The reed rule below already keeps trunks off
 * the waterline, but it only looks at the four tiles touching the bank, and the
 * tile that hides a small pond is never one of those. Nothing else in the build
 * says a pond is there and the fog is already withholding the ground, so the
 * canopy must not withhold it a second time.
 *
 * Only the rows a crown actually reaches are asked about, and only when the
 * roll has already come up canopy - so this runs on about a fifth of tiles, not
 * on every one. */
#define CROWN_HALF_W 2
#define CROWN_ROWS   5

static int crown_hides_water(const World *w, int tx, int ty)
{
    int ox, oy;

    for (oy = 1; oy <= CROWN_ROWS; oy++)
        for (ox = -CROWN_HALF_W; ox <= CROWN_HALF_W; ox++)
            if (terr_at(w, tx + ox, ty - oy) == GT_WATER)
                return 1;
    return 0;
}

/* Which prop, if any, stands on this tile. `density` is the canopy field in
 * 0..1 at that tile. */
static int prop_at(const World *w, Uint64 seed, int tx, int ty, float density, Uint32 *out_hash)
{
    Uint32 h = tile_hash(seed, tx, ty);
    int t = w->terr[ty][tx];
    unsigned roll = (h >> 8) & 63u;   /* bits 8-13: presence, decided before shape */

    if (out_hash) *out_hash = h;

    /* Water grows reeds at its margin and nothing in open water. */
    if (t == GT_WATER)
        return PROP_NONE;
    if (t == GT_DIRT)
        return roll < 2u ? PROP_STONE : (roll < 4u ? PROP_TUFT : PROP_NONE);
    if (t == GT_ROCK) {
        /* The ring autotile already draws stone across every RIM cell of an
         * outcrop, so a boulder standing on one doubles the rim - and being
         * 26 px wide on a 16 px cell it overhangs the cell beside it, which is
         * walkable ground. That is a rock she walks through, pressed against a
         * rim she cannot. The hollow CENTRE is where the sheet leaves a hole,
         * so that is where a free-standing boulder belongs; the rate is raised
         * because there are far fewer interior cells than rim ones. */
        if (!blob_interior(w, tx, ty, GT_ROCK))
            return PROP_NONE;
        return roll < 26u ? PROP_ROCK : (roll < 44u ? PROP_STONE : PROP_NONE);
    }

    /* Reeds hug the waterline. Checked before the canopy so a bank always reads
     * as a bank even inside a dense stand. */
    if (terr_at(w, tx - 1, ty) == GT_WATER || terr_at(w, tx + 1, ty) == GT_WATER ||
        terr_at(w, tx, ty - 1) == GT_WATER || terr_at(w, tx, ty + 1) == GT_WATER)
        return roll < 22u ? PROP_REED : (roll < 30u ? PROP_TUFT : PROP_NONE);

    {
        /* Canopy chance rises with the density field; everything else is the
         * understorey and stays roughly flat. */
        /* A tree sprite is 70x98 px - about 4 tiles wide and 6 tall - so the
         * per-tile rate that reads as "forest" is far lower than it looks. At
         * 26/64 the stands closed into unbroken canopy and hid the ground the
         * whole game is about revealing. */
        unsigned canopy = (unsigned)(density * 13.0f);
        if (roll < canopy) {
            /* Downgraded to understorey rather than cleared, so suppressing a
             * crown does not also punch a hole in the stand it stood in. */
            if (crown_hides_water(w, tx, ty))
                return PROP_BUSH;
            return ((h >> 20) & 3u) == 0 ? PROP_PINE : PROP_TREE;
        }
        if (roll < canopy + 4u)  return PROP_BUSH;
        if (roll < canopy + 6u)  return PROP_LOG;
        if (roll < canopy + 9u)  return PROP_MUSHROOM;
        if (roll < canopy + 14u) return PROP_TUFT;
        if (roll < canopy + 16u) return PROP_STONE;
    }
    return PROP_NONE;
}

/* ---- The sorted sprite pass ---------------------------------------------
 *
 * Everything that stands up - props, the player, entities later - goes into one
 * list keyed on its ground-contact y, which is then sorted so nearer things
 * draw last. This replaces the isometric build's diagonal-band walk, and is
 * strictly better here: a band walk orders TILES, so it cannot resolve two
 * sprites within one tile row, and the player moves continuously through those
 * rows. Sorting on feet_y is exact.
 *
 * Capacity is fixed and the list is filled from a bounded tile range, so there
 * is no allocation on the render path. */
/* Measured: the densest camera position across 8 proto-generator seeds fills
 * 368 slots. Sized well above that because entities, and any later increase in
 * prop density, land in the same list - and an overflow shows up as props
 * randomly failing to appear, which is close to undiagnosable from a
 * screenshot. Costs runtime memory only; the list is heap-allocated. */
#define DRAW_MAX 1024

enum { DI_SPRITE = 0, DI_FRAGMENT, DI_SOUL, DI_MAP };

/* How far entity `i`'s mote is lifted off its tile this instant, in px. A slow
 * bob, so a mote reads as alive rather than as scenery; the per-entity phase
 * offset keeps two motes in view from pulsing in lockstep.
 *
 * A function rather than the expression inline because the E prompt hangs off
 * the same point (see prompt_draw) and has to ride the SAME bob - two copies of
 * this would be a keycap that drifts against the mote it is pointing at, at a
 * beat frequency slow enough to look like a rendering bug. */
static int entity_bob(int i, float clock)
{
    return (int)(SDL_sinf(clock * 2.4f + (float)i * 0.7f) * 2.0f + 2.0f);
}

/* The mote's own size in px, and the one place that answers it: props_draw
 * draws the halo from it and the prompt clears the halo using it. */
static int entity_mote_px(int is_soul) { return is_soul ? 7 : 5; }

/* The map fragment is a chart, not a mote: wider than tall, so it reads as a
 * folded sheet against the round pickups at a glance. Same reason those are
 * two sizes - what a thing is has to be legible before you are close enough
 * to press E on it. */
#define MAP_MOTE_W 9
#define MAP_MOTE_H 7

typedef struct {
    int feet_y;     /* sort key: ground-contact y, in screen space */
    int x, y;       /* where to place the anchor */
    short art;
    Uint8 kind;     /* DI_* - markers have no sprite, so they draw procedurally */
    Uint8 fade;
    Uint8 level;    /* fog level at this sprite's own tile, captured at build */
    /* Marks the player's own item so it can be located AFTER the sort has moved
     * it. Identifying her by matching feet_y and sprite id instead would be a
     * lookup that silently returns "not found" - and a not-found player makes
     * every overlapping prop ghost, including ones behind her. */
    Uint8 is_player;
} DrawItem;

typedef struct {
    DrawItem item[DRAW_MAX];
    int n;
    int dropped;    /* counted, not silently ignored - see draw_list_push */
} DrawList;

static void draw_list_push(DrawList *dl, int art, int x, int y, int fade, int level)
{
    DrawItem *it;

    /* A full list silently dropping sprites would look like props randomly
     * failing to render, which is near-impossible to diagnose from a
     * screenshot. Counted so --sort-test can assert it never happens at the
     * real view size. */
    if (dl->n >= DRAW_MAX) { dl->dropped++; return; }
    it = &dl->item[dl->n++];
    it->art = (short)art;
    it->x = x;
    it->y = y;
    it->feet_y = y;
    it->fade = (Uint8)fade;
    it->is_player = 0;
    it->kind = DI_SPRITE;
    it->level = (Uint8)level;
}

static void draw_list_push_marker(DrawList *dl, int kind, int x, int y, int level)
{
    DrawItem *it;

    if (dl->n >= DRAW_MAX) { dl->dropped++; return; }
    it = &dl->item[dl->n++];
    it->art = ART_NONE;
    it->x = x;
    it->y = y;
    it->feet_y = y;
    it->fade = 0;
    it->is_player = 0;
    it->kind = (Uint8)kind;
    it->level = (Uint8)level;
}

/* Insertion sort on feet_y. The list arrives very nearly sorted - it is built
 * by walking tile rows top to bottom, so only the player and same-row props are
 * ever out of order - which is the case insertion sort handles in close to
 * linear time. A comparison sort would be asymptotically better and measurably
 * slower here. STABLE, so equal feet_y keeps build order and the frame cannot
 * flicker between two orderings. */
static void draw_list_sort(DrawList *dl)
{
    int i, j;

    for (i = 1; i < dl->n; i++) {
        DrawItem key = dl->item[i];
        j = i - 1;
        while (j >= 0 && dl->item[j].feet_y > key.feet_y) {
            dl->item[j + 1] = dl->item[j];
            j--;
        }
        dl->item[j + 1] = key;
    }
}

/* Where to DRAW her this frame: between the last two tick positions, by how far
 * past the last tick the frame is. See the long note at the call site in main.
 *
 * A named function rather than three lines inline, so --move-test can drive the
 * real one. A test that restated the lerp would only prove the lerp equals
 * itself, and the property worth checking - that the drawn position advances
 * evenly when the ticks do not - is a property of this being applied, not of
 * the arithmetic. */
static float render_lerp(float prev, float cur, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return prev + (cur - prev) * alpha;
}

/* ---- Camera -------------------------------------------------------------
 * Kept in floats and clamped in floats: at integer precision a sub-pixel
 * remainder truncates to no motion and the ease stalls short of its target. */
static void camera_follow(float px, float py, int view_w, int view_h,
                          int *cam_x, int *cam_y)
{
    float fx = px - (float)view_w * 0.5f;
    float fy = py - (float)view_h * 0.5f;
    float max_x = (float)(WORLD_W * TILE - view_w);
    float max_y = (float)(WORLD_H * TILE - view_h);

    if (fx < 0.0f) fx = 0.0f;
    if (fy < 0.0f) fy = 0.0f;
    if (max_x > 0.0f && fx > max_x) fx = max_x;
    if (max_y > 0.0f && fy > max_y) fy = max_y;
    *cam_x = (int)(fx + 0.5f);
    *cam_y = (int)(fy + 0.5f);
}

/* ---- World rendering ----------------------------------------------------
 *
 * Three passes over a clamped rectangular tile range - no per-tile rejection
 * test, unlike the isometric build's diagonal band walk, because in a top-down
 * grid the visible range IS a rectangle.
 *
 *   ground   one opaque 16x16 tile per cell
 *   edges    transparent blob overlays: grass over dirt, then olive over grass
 *   water    pond rim and cap
 */
static int uw_is_ledge_pass(const World *w, int tx, int ty, int *stair_part)
{
    int r;
    int n_ledge = 0, n_norm = 0, rock_flank = 0;
    static const int dx[4] = { 0, 0, -1, 1 };
    static const int dy[4] = { -1, 1, 0, 0 };
    int d;

    if (w->solid[ty][tx] || w->terr[ty][tx] == GT_ROCK)
        return 0;
    r = w->region[ty][tx];
    if (r == REGION_NONE)
        return 0;

    for (d = 0; d < 4; d++) {
        int nx = tx + dx[d], ny = ty + dy[d];
        int nr;
        if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
        if (w->solid[ny][nx] || w->terr[ny][nx] == GT_ROCK) {
            rock_flank = 1;
            continue;
        }
        nr = w->region[ny][nx];
        if (nr == REGION_NONE) continue;
        if (w->regions[nr].terrain == TERRAIN_LEDGE) n_ledge++;
        else n_norm++;
    }
    if (!rock_flank)
        return 0;
    if (!((w->regions[r].terrain == TERRAIN_LEDGE && n_norm > 0) ||
          (w->regions[r].terrain != TERRAIN_LEDGE && n_ledge > 0)))
        return 0;

    if (stair_part) {
        if (w->regions[r].terrain == TERRAIN_LEDGE)
            *stair_part = (tx & 1) ? ART_UW_STAIRS_TR : ART_UW_STAIRS_TL;
        else
            *stair_part = (tx & 1) ? ART_UW_STAIRS_MR : ART_UW_STAIRS_ML;
    }
    return 1;
}

static void render_world(SDL_Surface *fb, const World *w, Uint64 seed,
                         int cam_x, int cam_y)
{
    const TileSet *ts = tileset_for(w->biome);
    int tx0 = cam_x / TILE, ty0 = cam_y / TILE;
    int tx1 = (cam_x + fb->w) / TILE + 1;
    int ty1 = (cam_y + fb->h) / TILE + 1;
    int tx, ty;

    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > WORLD_W) tx1 = WORLD_W;
    if (ty1 > WORLD_H) ty1 = WORLD_H;

    /* Ground. A leafy cell that touches dirt lays DIRT down, so the grass
     * overlay in the next pass can supply the ragged boundary.
     *
     * GT_ROCK takes the leafy path with everything else gt_leafy names, rather
     * than the dirt one it used to share with GT_DIRT. An outcrop stands IN
     * grass - that is what gt_leafy has always said, and what the grass ring
     * and grass rubble in the tile tables above are drawn for - so laying
     * earth under it contradicted its own art. It matters in Forest only:
     * Underworld's and Lumiara's rock fills measure 0 transparent px, so no
     * base under a rock cell is ever visible in those biomes either way. */
    for (ty = ty0; ty < ty1; ty++) {
        for (tx = tx0; tx < tx1; tx++) {
            int t = w->terr[ty][tx];
            Uint32 h = tile_hash(seed, tx, ty);
            int sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
            int touches_dirt =
                terr_at(w, tx - 1, ty) == GT_DIRT || terr_at(w, tx + 1, ty) == GT_DIRT ||
                terr_at(w, tx, ty - 1) == GT_DIRT || terr_at(w, tx, ty + 1) == GT_DIRT;
            int dirt = ts->dirt_fill[h % 6u];
            int id;

            if (t == GT_DIRT) {
                id = dirt;
            } else if (t == GT_WATER) {
                /* ONLY the interior of a pond gets the opaque water fill.
                 *
                 * The rim overlay two passes below is transparent on its LAND
                 * side - that transparency is what lets a bank read as a ragged
                 * shoreline instead of a cut edge. With the water fill laid
                 * underneath it, what showed through was the fill's own square
                 * corner, so every pond rendered as a teal RECTANGLE with its
                 * bank floating inside it. Lay the land tile on a boundary cell
                 * and let the rim draw the water it covers; the rim's water
                 * side is opaque, so nothing is left unpainted. */
                id = blob_interior(w, tx, ty, GT_WATER)
                     ? ts->water_fill[h % 10u]
                     : (touches_dirt ? dirt : ts->grass_base[h % 4u]);
            } else {
                id = touches_dirt ? dirt : ts->grass_base[h % 4u];
            }
            draw_sprite(fb, id, sx, sy, tile_level(w, tx, ty));

            /* Stone rubble under EVERY rock cell, not just the hollow middle.
             *
             * WHAT BLOCKS HER HAS TO BE WHAT SHE CAN SEE, and the ring art
             * alone does not manage it. Its nine slices are drawn with the mass
             * low in the tile, the way this sheet fakes height, so they cover
             * wildly different amounts of their cell: measured in opaque pixels
             * out of 256, the bottom edge draws 243 and the two TOP CORNERS
             * draw 12 and 18. Collision is the whole square either way. The
             * result was a tile of solid nothing standing on open grass along
             * the top of every outcrop - about a third of all rock cells drew
             * under an eighth of themselves - and walking into one stopped her
             * dead against grass with no toast, because a wall is not a gate
             * and says nothing.
             *
             * Filling underneath fixes the silhouette without touching
             * collision at all: solid[][] is untouched, so every reachability
             * and completability proof stands exactly as it was. The ring still
             * draws on top and still supplies the rim.
             *
             * An OVERLAY, not a base - these cells are authored as the body of
             * a boulder and carry a dozen transparent pixels each, so used as a
             * base they left 57 px of the screen unpainted, which is what
             * --tile-test's coverage check reported. */
            if (t == GT_ROCK)
                draw_sprite(fb, (world_border(tx, ty) ? ts->rock_cliff
                                                      : ts->rock_fill)[(h >> 4) & 1u],
                            sx, sy, tile_level(w, tx, ty));

            /* Scattered ground detail over the opaque base, on interior grass
             * only: a dirt-adjacent cell gets its grass from the edge overlay
             * in the next pass, and a detail tile here would paint grass across
             * the boundary the overlay is about to draw. Forest only - there is
             * no Underworld detail table (see the tile table comment above). */
            if (w->biome == BIOME_FOREST && t == GT_GRASS && id != dirt && ((h >> 8) & 7u) == 0)
                draw_sprite(fb, tile_grass_detail[(h >> 10) % 6u], sx, sy, tile_level(w, tx, ty));
        }
    }

    /* Grass over dirt, then olive over grass. Two separate sweeps rather than
     * one: an olive cell that also touches dirt needs the grass edge underneath
     * its own, and doing both in one pass would order them per-cell instead of
     * per-layer. */
    for (ty = ty0; ty < ty1; ty++) {
        for (tx = tx0; tx < tx1; tx++) {
            int sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
            if (!gt_leafy(w->terr[ty][tx]))
                continue;
            if (terr_at(w, tx - 1, ty) == GT_DIRT || terr_at(w, tx + 1, ty) == GT_DIRT ||
                terr_at(w, tx, ty - 1) == GT_DIRT || terr_at(w, tx, ty + 1) == GT_DIRT) {
                int s = blob_slice(terr_at(w, tx, ty - 1) != GT_DIRT,
                                   terr_at(w, tx, ty + 1) != GT_DIRT,
                                   terr_at(w, tx + 1, ty) != GT_DIRT,
                                   terr_at(w, tx - 1, ty) != GT_DIRT);
                draw_sprite(fb, ts->grass_edge[s], sx, sy, tile_level(w, tx, ty));
            }
        }
    }
    for (ty = ty0; ty < ty1; ty++) {
        for (tx = tx0; tx < tx1; tx++) {
            int sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
            if (w->terr[ty][tx] != GT_OLIVE)
                continue;
            {
                int s = blob_slice(terr_at(w, tx, ty - 1) == GT_OLIVE,
                                   terr_at(w, tx, ty + 1) == GT_OLIVE,
                                   terr_at(w, tx + 1, ty) == GT_OLIVE,
                                   terr_at(w, tx - 1, ty) == GT_OLIVE);
                draw_sprite(fb, ts->olive_edge[s], sx, sy, tile_level(w, tx, ty));
            }
        }
    }

    /* Rock outcrops, as a blob autotile over the ring. The ring's centre slice
     * is ART_NONE, so an outcrop's interior draws nothing and the ground laid
     * underneath shows through - which is exactly how the source sheet is
     * built, and why a rock mass reads as a rim around a hollow rather than as
     * a solid block. */
    for (ty = ty0; ty < ty1; ty++) {
        for (tx = tx0; tx < tx1; tx++) {
            int sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
            if (w->terr[ty][tx] != GT_ROCK)
                continue;
            /* The border ring already drew its cliff and must NOT take an
             * outcrop rim on top of it. The rim it would be handed is the
             * blob's NORTH slice, which is 78 px of which 72 are grass - so a
             * ring pass here would paint meadow back over the top edge of the
             * world. */
            if (world_border(tx, ty))
                continue;
            {
                int s = blob_slice(terr_at(w, tx, ty - 1) == GT_ROCK,
                                   terr_at(w, tx, ty + 1) == GT_ROCK,
                                   terr_at(w, tx + 1, ty) == GT_ROCK,
                                   terr_at(w, tx - 1, ty) == GT_ROCK);
                if (ts->rock_ring[s] != ART_NONE)
                    draw_sprite(fb, ts->rock_ring[s], sx, sy, tile_level(w, tx, ty));
            }
        }
    }

    /* Water rim, and the cap on the land tile immediately north of it. */
    for (ty = ty0; ty < ty1; ty++) {
        for (tx = tx0; tx < tx1; tx++) {
            int sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
            int t = w->terr[ty][tx];
            if (t == GT_WATER) {
                int n = terr_at(w, tx, ty - 1) == GT_WATER;
                int s = terr_at(w, tx, ty + 1) == GT_WATER;
                int e = terr_at(w, tx + 1, ty) == GT_WATER;
                int wst = terr_at(w, tx - 1, ty) == GT_WATER;
                if (!(n && s && e && wst))
                    draw_sprite(fb, ts->water_edge[blob_slice(n, s, e, wst)], sx, sy, tile_level(w, tx, ty));
            } else if ((t == GT_GRASS || t == GT_OLIVE) &&
                       terr_at(w, tx, ty + 1) == GT_WATER) {
                /* The cap sits on LAND and overhangs the water below it, which
                 * is what makes a bank read as a bank rather than a cut edge.
                 *
                 * Restricted to leafy ground because the cap tile is an OPAQUE
                 * grass tile with a lip along its bottom, not an overlay: drawn
                 * on a dirt cell it repainted the whole cell green, so a trail
                 * running along a pond's north shore grew a one-tile grass
                 * stripe out of nothing. The water cell below still draws its
                 * own rim, so dropping the cap costs the bank nothing. */
                int e = terr_at(w, tx + 1, ty + 1) == GT_WATER;
                int wst = terr_at(w, tx - 1, ty + 1) == GT_WATER;
                draw_sprite(fb, ts->water_cap[wst ? (e ? 1 : 2) : 0], sx, sy, tile_level(w, tx, ty));
            }
        }
    }

    /* In Underworld, render stone stairs at the passes leading into high areas (TERRAIN_LEDGE) */
    if (w->biome == BIOME_UNDERWORLD) {
        for (ty = ty0; ty < ty1; ty++) {
            for (tx = tx0; tx < tx1; tx++) {
                int sx = tx * TILE - cam_x, sy = ty * TILE - cam_y;
                int stair_art;
                if (uw_is_ledge_pass(w, tx, ty, &stair_art))
                    draw_sprite(fb, stair_art, sx, sy, tile_level(w, tx, ty));
            }
        }
    }
}


/* Does a sprite standing at (sx, sy) with box (w,h) cover the player?
 *
 * Deliberately pure and taking plain ints, so a truth table can hit it directly
 * instead of inferring from pixels. `after` - the sprite sorts later than the
 * player, i.e. is in front of her - is the load-bearing clause: a prop BEHIND
 * her is drawn first and cannot hide her, so ghosting it would flicker scenery
 * for no reason. Boxes are half-open. */
static int prop_covers_player(int after,
                              int sx0, int sy0, int sx1, int sy1,
                              int px0, int py0, int px1, int py1)
{
    if (!after) return 0;
    if (sx1 <= px0 || sx0 >= px1) return 0;
    if (sy1 <= py0 || sy0 >= py1) return 0;
    return 1;
}

/* Build and sort the list. Separate from drawing it so the tests can measure
 * list contents and ordering without paying for ~200 RLE blits per sample -
 * which is the difference between a sweep that runs in a second and one that
 * times out. */
static void props_build(int view_w, int view_h, const World *w, Uint64 seed,
                        int cam_x, int cam_y, const Entity *ents,
                        const Player *p, float clock, DrawList *dl)
{
    int tx0 = cam_x / TILE, ty0 = cam_y / TILE;
    int tx1 = (cam_x + view_w) / TILE + 1;
    int ty1 = (cam_y + view_h) / TILE + 1 + PROP_OVERSCAN;
    int tx, ty, i, pi = -1;
    int pbox[4];

    dl->n = 0;
    dl->dropped = 0;

    /* Derived from the art itself, so it is built once from ART_SPRITES rather
     * than carried as a hand-maintained table - see prop_keep_build. */
    if (!prop_keep_ready)
        prop_keep_build();

    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > WORLD_W) tx1 = WORLD_W;
    if (ty1 > WORLD_H) ty1 = WORLD_H;

    for (ty = ty0; ty < ty1; ty++) {
        for (tx = tx0; tx < tx1; tx++) {
            Uint32 h;
            int kind = prop_at(w, seed, tx, ty, w->canopy[ty][tx] / 255.0f, &h);
            const PropArt *pa;
            int art;
            if (kind == PROP_NONE)
                continue;
            pa = &prop_art[w->biome][kind];
            if (pa->n <= 0)
                continue;
            /* Keep the map fragment's own neighbourhood clear of scenery, in
             * EVERY biome. Not decoration: measured on seed 3, the chart was
             * drawn and then completely buried by two sprites - a bush anchored
             * on its own tile, which sorts after it because the bob lowers a
             * marker's sort key, and a second one a tile south. A key item that
             * unlocks a whole screen and cannot be seen is not the same failure
             * as one of seven fragments sitting behind a trunk, which is
             * deliberate; this one has no other copy to find.
             *
             * One tile of clearance is enough for the art that exists: a prop
             * two rows south has its feet 32 px below the chart's and the
             * tallest sprite here is ~30 px, so its top lands under the chart
             * rather than over it. Same shape and same reason as the portal
             * exclusion below - and applied here, on the render side, rather
             * than by teaching placement about prop_at: which tiles carry props
             * is a render-side rule with a density thin and a ground-context
             * swap on top of it, and a placer that restated it would be a
             * second copy free to drift. */
            if (w->map_tile >= 0) {
                int mtx = w->map_tile % WORLD_W, mty = w->map_tile / WORLD_W;
                if (tx >= mtx - 1 && tx <= mtx + 1 && ty >= mty - 1 && ty <= mty + 1)
                    continue;
            }
            /* In Underworld, keep the stone stairs and dimensional portal clear of props */
            if (w->biome == BIOME_UNDERWORLD) {
                if (uw_is_ledge_pass(w, tx, ty, NULL))
                    continue;
                if (w->portal_tile >= 0) {
                    int ptx = w->portal_tile % WORLD_W, pty = w->portal_tile / WORLD_W;
                    if (tx >= ptx - 1 && tx <= ptx + 1 && ty >= pty - 1 && ty <= pty + 1)
                        continue;
                }
            }
            /* Bits 14-19 for the density thin: disjoint from the presence roll
             * (bits 8-13) and the variant pick (24+), so "how often" stays
             * independent of both "whether" and "which". Applied here rather
             * than inside prop_at so prop_at keeps meaning "what belongs on
             * this tile" - only whether it is DRAWN is biome-scaled. */
            if (((h >> 14) & 63u) >= (Uint32)prop_keep[w->biome][kind])
                continue;
            /* Ground context decides WHICH rock, once the rolls above have
             * decided whether there is one. Swapped in strictly AFTER the
             * presence roll and the density thin, and never before them, so
             * Forest's placement is bit-identical to what it was - and with it
             * --mockup-test's pixel census, which is calibrated on Forest art.
             * Only the sprite drawn on an already-chosen tile changes. */
            {
                const PropArt *gpa = prop_ground_art(w->biome, kind,
                                                     ground_class(w->terr[ty][tx]));
                if (gpa && gpa->n > 0)
                    pa = gpa;
            }
            /* Bits 24+ for the variant: the low bits already chose presence,
             * and reusing them would correlate which tree with whether a tree. */
            art = pa->ids[(h >> 24) % (Uint32)pa->n];
            draw_list_push(dl, art,
                           tx * TILE + TILE / 2 - cam_x,
                           ty * TILE + TILE - cam_y, 0, tile_level(w, tx, ty));
        }
    }

    /* The exit onward, drawn in whichever area still HAS one - and drawn as
     * the gate it actually is, rather than as one shared sprite:
     *
     *   Forest     -> the GREEN swirl (ART_UW_PORTAL_*), the Area 1 -> Area 2
     *                 gate. Six authored frames, so it animates. The "UW" in
     *                 the name is legacy: this is the generic
     *                 Dimensional_Portal asset, not Underworld-specific art.
     *   Underworld -> the DREAMGATE (ART_LUM_PORTAL), the Area 2 -> Area 3
     *                 gate. ONE authored frame, so it is drawn static - there
     *                 is no cycle to run, and advancing a frame counter over a
     *                 single sprite would be a no-op dressed up as animation.
     *
     * Biome-gated rather than area-gated - World does not know about
     * Game.area and does not need to: portal_tile is computed for every biome
     * (see world_place_portal) but only ever drawn where a gate is meaningful,
     * which is everywhere except Lumiara - Area 3 is terminal. */
    if (w->portal_tile >= 0 && (w->biome == BIOME_FOREST || w->biome == BIOME_UNDERWORLD)) {
        int ptx = w->portal_tile % WORLD_W, pty = w->portal_tile / WORLD_W;
        if (ptx >= tx0 && ptx < tx1 && pty >= ty0 && pty < ty1) {
            int art = ART_LUM_PORTAL;
            if (w->biome == BIOME_FOREST)
                art = ART_UW_PORTAL_A + ((int)(clock * 6.0f) % 6);
            draw_list_push(dl, art,
                           ptx * TILE + TILE / 2 - cam_x,
                           pty * TILE + TILE - cam_y, 0, tile_level(w, ptx, pty));
        }
    }

    /* Unrestored collectibles, in the same list so a fragment behind a trunk is
     * actually hidden by it. Skipped while deep in fog, so the fog cannot be
     * defeated by looking for the motes. */
    if (ents) {
        for (i = 0; i < ENTITY_COUNT; i++) {
            int ex, ey, sx, sy;
            if (ents[i].tile < 0 || ents[i].restored) continue;
            ex = ents[i].tile % WORLD_W;
            ey = ents[i].tile / WORLD_W;
            if (ex < tx0 || ex >= tx1 || ey < ty0 || ey >= ty1) continue;
            if (tile_level(w, ex, ey) < 3) continue;
            sx = ex * TILE + TILE / 2 - cam_x;
            sy = ey * TILE + TILE - cam_y - entity_bob(i, clock);
            draw_list_push_marker(dl, ents[i].is_soul ? DI_SOUL : DI_FRAGMENT, sx, sy,
                                  FOG_LEVELS - 1);
        }
    }

    /* The map fragment, in the same list and under the same fog rule as the
     * collectibles: it is a thing lying in the world, so a trunk in front of it
     * hides it, and being deep in fog hides it too. Bobbing on the phase slot
     * just past the last entity, so it cannot pulse in lockstep with a mote
     * standing near it. */
    if (w->map_tile >= 0) {
        int mx = w->map_tile % WORLD_W, my = w->map_tile / WORLD_W;
        if (mx >= tx0 && mx < tx1 && my >= ty0 && my < ty1 &&
            tile_level(w, mx, my) >= 3)
            draw_list_push_marker(dl, DI_MAP,
                                  mx * TILE + TILE / 2 - cam_x,
                                  my * TILE + TILE - cam_y
                                      - entity_bob(ENTITY_COUNT, clock),
                                  FOG_LEVELS - 1);
    }

    /* The player goes in the same list, so she sorts against props by feet
     * rather than by tile row. */
    {
        int art = player_sprite(p, clock);
        const ArtSprite *sp = &ART_SPRITES[art];
        int x = (int)p->x - cam_x, y = (int)p->y - cam_y;
        pi = dl->n;
        draw_list_push(dl, art, x, y, 0,
                       tile_level(w, (int)p->x / TILE, (int)p->y / TILE));
        if (pi < dl->n) dl->item[pi].is_player = 1;   /* 0 if the push was dropped */
        pbox[0] = x - sp->anchor_x;
        pbox[1] = y - sp->anchor_y;
        pbox[2] = pbox[0] + sp->w;
        pbox[3] = pbox[1] + sp->h;
    }

    draw_list_sort(dl);

    /* Ghost anything that sorts in front of the player and overlaps her, so she
     * is never lost behind a trunk. Done after the sort because "in front of"
     * is a statement about the final order. */
    {
        int p_at = -1;
        for (i = 0; i < dl->n; i++)
            if (dl->item[i].is_player) { p_at = i; break; }
        /* No player in the list means nothing can be in front of her. Skipping
         * outright rather than falling through: with p_at == -1 the test below
         * reads `i > -1`, which is true for every item, so an overflow would
         * ghost half the scenery instead of ghosting nothing. */
        if (p_at >= 0) {
            for (i = 0; i < dl->n; i++) {
                const ArtSprite *sp;
                int x0, y0;
                if (i == p_at) continue;
                /* Markers carry ART_NONE, so indexing ART_SPRITES with their
                 * art id would read ART_SPRITES[-1]. They are small and never
                 * hide the player, so they simply do not ghost. */
                if (dl->item[i].kind != DI_SPRITE) continue;
                sp = &ART_SPRITES[dl->item[i].art];
                x0 = dl->item[i].x - sp->anchor_x;
                y0 = dl->item[i].y - sp->anchor_y;
                dl->item[i].fade = (Uint8)prop_covers_player(i > p_at,
                                        x0, y0, x0 + sp->w, y0 + sp->h,
                                        pbox[0], pbox[1], pbox[2], pbox[3]);
            }
        }
    }
}

static void props_draw(SDL_Surface *fb, const DrawList *dl)
{
    int i;

    for (i = 0; i < dl->n; i++) {
        const DrawItem *it = &dl->item[i];
        if (it->kind == DI_SPRITE) {
            draw_sprite_sp(fb, &ART_SPRITES[it->art], it->x, it->y, it->level, it->fade, 0);
            continue;
        }
        /* Same procedural reasoning as the E keycap: a folded chart costs no
         * art data and retunes in a rebuild. Parchment plate, leather edge, and
         * an ink route bent across it so it is not merely a pale rectangle. */
        if (it->kind == DI_MAP) {
            int x0 = it->x - MAP_MOTE_W / 2, y0 = it->y - MAP_MOTE_H;
            Uint32 edge = SDL_MapRGB(fb->format, 0x5a, 0x3c, 0x22);
            Uint32 page = SDL_MapRGB(fb->format, 0xe8, 0xd6, 0xa6);
            Uint32 ink  = SDL_MapRGB(fb->format, 0x8c, 0x33, 0x1e);
            fill_rect(fb, x0 - 1, y0 - 1, MAP_MOTE_W + 2, MAP_MOTE_H + 2, edge);
            fill_rect(fb, x0, y0, MAP_MOTE_W, MAP_MOTE_H, page);
            fill_rect(fb, x0 + 1, y0 + MAP_MOTE_H - 2, 3, 1, ink);
            fill_rect(fb, x0 + 3, y0 + 2, 1, MAP_MOTE_H - 3, ink);
            fill_rect(fb, x0 + 4, y0 + 2, 4, 1, ink);
            continue;
        }
        /* Fragments and Souls have no authored art, so they are drawn: a warm
         * mote for a memory, a taller pale one for a Soul. Deliberately NOT
         * fogged - a collectible you cannot see is a collectible you cannot
         * find, and the fog is already telling you about the terrain. */
        {
            int soul = (it->kind == DI_SOUL);
            int s = entity_mote_px(soul);
            Uint32 core = soul ? SDL_MapRGB(fb->format, 0xf3, 0xda, 0xda)
                               : SDL_MapRGB(fb->format, 0xf3, 0xda, 0xb0);
            Uint32 halo = soul ? SDL_MapRGB(fb->format, 0x3e, 0x7d, 0x8d)
                               : SDL_MapRGB(fb->format, 0xe0, 0x3b, 0x0e);
            fill_rect(fb, it->x - s / 2 - 1, it->y - s - 1, s + 2, s + 2, halo);
            fill_rect(fb, it->x - s / 2, it->y - s, s, s, core);
        }
    }
}

/* ---- The interact prompt ------------------------------------------------
 *
 * Brought back from the isometric build, where a bobbing E keycap over the
 * thing E would act on was the only affordance the game had. This build lost it
 * in the port and never replaced it: a mote in reach and a mote across the
 * clearing look identical, so the interact key is something you learn by
 * mashing it.
 *
 * PROCEDURAL, not baked, for the same reason as before: it costs no art data,
 * retunes in one rebuild, and a real authored keycap later replaces the body of
 * this one function. Deliberately not the bitmap font either - draw_text is a
 * shipping function here, but a glyph drawn at FONT_SCALE 1 is 5x7 px and would
 * read as a smudge on a 16 px tile. The E is four rectangles, sized so its
 * proportions match the font's at roughly twice the weight.
 *
 * The cap is 11 px on a 16 px tile: measured against the mote it points at
 * (5 px for a fragment, 7 for a Soul) and the player sprite (13x25). Larger and
 * it hides the pickup; smaller and it is a speck. */
#define PROMPT_W    11
#define PROMPT_BOB   2       /* peak vertical travel, px */
#define PROMPT_HZ    1.6f    /* bobs per second */

/* The bob as a PURE function of time, with the clamp INSIDE it, so "the prompt
 * never leaves its anchor by more than PROMPT_BOB" is a property of the
 * function rather than of every caller remembering - and --hud-test can sweep
 * it directly instead of inferring it from pixels. */
static int prompt_bob(float t)
{
    int o = (int)(SDL_sinf(t * PROMPT_HZ * (float)TWO_PI) * (float)PROMPT_BOB);

    if (o >  PROMPT_BOB) o =  PROMPT_BOB;
    if (o < -PROMPT_BOB) o = -PROMPT_BOB;
    return o;
}

/* A keycap whose BOTTOM edge sits at (cx, by), bobbing on `t` - the same
 * "centre, feet" anchoring every other draw routine here takes, so callers pass
 * a point above the thing being pointed at and the cap hangs off it.
 *
 * Never fogged, unlike everything else on screen. A prompt is UI: it reports
 * what the interact key will do, and dimming it would hide the affordance
 * exactly where she is standing and looking. */
static void draw_prompt(SDL_Surface *fb, int cx, int by, float t)
{
    int x0 = cx - PROMPT_W / 2;
    int y0 = by + prompt_bob(t) - PROMPT_W;
    Uint32 edge  = SDL_MapRGB(fb->format, 0xb0, 0xa0, 0xe8);
    Uint32 fill  = SDL_MapRGB(fb->format, 0x24, 0x1e, 0x46);
    Uint32 glyph = SDL_MapRGB(fb->format, 0xff, 0xf4, 0xd8);
    int gx = cx - 2, gy = y0 + 2;

    fill_rect(fb, x0, y0, PROMPT_W, PROMPT_W, edge);            /* border plate */
    fill_rect(fb, x0 + 1, y0 + 1, PROMPT_W - 2, PROMPT_W - 2, fill);
    fill_rect(fb, gx, gy,     1, 7, glyph);                     /* spine  */
    fill_rect(fb, gx, gy,     5, 1, glyph);                     /* top    */
    fill_rect(fb, gx, gy + 3, 4, 1, glyph);                     /* middle */
    fill_rect(fb, gx, gy + 6, 5, 1, glyph);                     /* bottom */
}

/* The prompt over the ONE entity E would act on, or nothing.
 *
 * Driven by entity_in_reach at the player's SIMULATION position - the same call
 * on the same floats try_interact makes - rather than at the interpolated
 * render position. That is the load-bearing detail: a cue drawn from a
 * different position than the key reads would appear a frame early or late at
 * the edge of the radius, promising an interact that does nothing. --hud-test
 * asserts the two agree.
 *
 * The fog condition mirrors props_build's: where the mote is too deep in fog to
 * be drawn, the prompt is not drawn either. A keycap hanging over blank forest
 * is worse than no keycap. */
static void prompt_draw(SDL_Surface *fb, const World *w, const Entity *ents,
                        float px, float py, int cam_x, int cam_y, float clock)
{
    int i = entity_in_reach(w, ents, px, py);
    int ex, ey, sx, sy;

    if (i < 0) {
        /* The map fragment takes the prompt only when no mote is in reach,
         * which is exactly the order the interact key resolves the two in - a
         * keycap pointing at one thing while the key acts on another is worse
         * than no keycap at all. --map-test asserts the two agree. */
        if (!map_in_reach(w, px, py))
            return;
        ex = w->map_tile % WORLD_W;
        ey = w->map_tile / WORLD_W;
        if (tile_level(w, ex, ey) < 3)
            return;
        sx = ex * TILE + TILE / 2 - cam_x;
        sy = ey * TILE + TILE - cam_y - entity_bob(ENTITY_COUNT, clock);
        draw_prompt(fb, sx, sy - MAP_MOTE_H - 3, clock);
        return;
    }
    ex = ents[i].tile % WORLD_W;
    ey = ents[i].tile / WORLD_W;
    if (tile_level(w, ex, ey) < 3)
        return;
    sx = ex * TILE + TILE / 2 - cam_x;
    sy = ey * TILE + TILE - cam_y - entity_bob(i, clock);
    /* Clear of the mote's halo (one px beyond the core on every side) plus two,
     * so the cap floats above it rather than resting on it. */
    draw_prompt(fb, sx, sy - entity_mote_px(ents[i].is_soul) - 3, clock);
}

/* ---- Bitmap font --------------------------------------------------------
 *
 * 5x7, hand-rolled and bit-packed. No SDL_ttf, ever: it would cost more than
 * every sprite in the game combined to draw eleven words.
 *
 * FONT_SCALE is 1 here where the isometric build used 2, because that build
 * rasterised at 960x540 and this one at 480x270 - the glyph occupies the same
 * fraction of the screen either way.
 *
 * FONT_5X7 is FLAT and indexed with an explicit stride rather than declared as
 * [glyph][row], on purpose: font_selftest's negative control corrupts that
 * stride to prove its pixel-count checker actually rejects a misread glyph. */
#define FONT_W      5
#define FONT_H      7
#define FONT_SCALE  1
#define FONT_FIRST  0x20   /* space */
#define FONT_LAST   0x7A   /* lowercase z; covers digits, A-Z, a-z, punctuation */
#define FONT_GLYPHS (FONT_LAST - FONT_FIRST + 1)
#define FONT_STRIDE FONT_H /* rows per glyph - see the note above */
#define FONT_ADV    ((FONT_W + 1) * FONT_SCALE)
#define FONT_LINE   ((FONT_H + 1) * FONT_SCALE)

#define GR(a,b,c,d,e) (Uint8)(((a)<<4)|((b)<<3)|((c)<<2)|((d)<<1)|(e))

static const Uint8 FONT_5X7[FONT_GLYPHS * FONT_H] = {
    /* 0x20 ' ' */ 0,0,0,0,0,0,0,
    /* 0x21 '!' */ GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), 0, GR(0,0,1,0,0),
    /* 0x22-0x26 unused */ 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0,
    /* 0x27 apostrophe */ GR(0,0,1,0,0), GR(0,0,1,0,0), 0,0,0,0,0,
    /* 0x28-0x2B unused */ 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0,
    /* 0x2C ',' */ 0,0,0,0,0, GR(0,0,1,0,0), GR(0,1,0,0,0),
    /* 0x2D '-' */ 0,0,0, GR(0,1,1,1,0), 0,0,0,
    /* 0x2E '.' */ 0,0,0,0,0,0, GR(0,0,1,0,0),
    /* 0x2F '/' */ GR(0,0,0,0,1), GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,1,0,0,0), GR(1,0,0,0,0), 0, 0,
    /* 0x30 '0' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x31 '1' */ GR(0,0,1,0,0), GR(0,1,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,1,1,1,0),
    /* 0x32 '2' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(0,0,0,0,1), GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,1,0,0,0), GR(1,1,1,1,1),
    /* 0x33 '3' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(0,0,0,0,1), GR(0,0,1,1,0), GR(0,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x34 '4' */ GR(0,0,0,1,0), GR(0,0,1,1,0), GR(0,1,0,1,0), GR(1,0,0,1,0), GR(1,1,1,1,1), GR(0,0,0,1,0), GR(0,0,0,1,0),
    /* 0x35 '5' */ GR(1,1,1,1,1), GR(1,0,0,0,0), GR(1,1,1,1,0), GR(0,0,0,0,1), GR(0,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x36 '6' */ GR(0,0,1,1,0), GR(0,1,0,0,0), GR(1,0,0,0,0), GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x37 '7' */ GR(1,1,1,1,1), GR(0,0,0,0,1), GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0),
    /* 0x38 '8' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x39 '9' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,1), GR(0,0,0,0,1), GR(0,0,0,1,0), GR(0,1,1,0,0),
    /* 0x3A ':' */ 0,0, GR(0,0,1,0,0), 0, GR(0,0,1,0,0), 0,0,
    /* 0x3B unused */ 0,0,0,0,0,0,0,
    /* 0x3C '<' */ 0, GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,1,0,0,0), GR(0,0,1,0,0), GR(0,0,0,1,0), 0,
    /* 0x3D '=' */ 0,0, GR(1,1,1,1,1), 0, GR(1,1,1,1,1), 0,0,
    /* 0x3E '>' */ 0, GR(0,1,0,0,0), GR(0,0,1,0,0), GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,1,0,0,0), 0,
    /* 0x3F '?' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(0,0,0,0,1), GR(0,0,0,1,0), GR(0,0,1,0,0), 0, GR(0,0,1,0,0),
    /* 0x40 unused */ 0,0,0,0,0,0,0,
    /* 0x41 'A' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x42 'B' */ GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,0),
    /* 0x43 'C' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x44 'D' */ GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,0),
    /* 0x45 'E' */ GR(1,1,1,1,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,1,1,1,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,1,1,1,1),
    /* 0x46 'F' */ GR(1,1,1,1,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,1,1,1,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0),
    /* 0x47 'G' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,0), GR(1,0,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x48 'H' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x49 'I' */ GR(0,1,1,1,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,1,1,1,0),
    /* 0x4A 'J' */ GR(0,0,1,1,1), GR(0,0,0,1,0), GR(0,0,0,1,0), GR(0,0,0,1,0), GR(0,0,0,1,0), GR(1,0,0,1,0), GR(0,1,1,0,0),
    /* 0x4B 'K' */ GR(1,0,0,0,1), GR(1,0,0,1,0), GR(1,0,1,0,0), GR(1,1,0,0,0), GR(1,0,1,0,0), GR(1,0,0,1,0), GR(1,0,0,0,1),
    /* 0x4C 'L' */ GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,1,1,1,1),
    /* 0x4D 'M' */ GR(1,0,0,0,1), GR(1,1,0,1,1), GR(1,0,1,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x4E 'N' */ GR(1,0,0,0,1), GR(1,1,0,0,1), GR(1,0,1,0,1), GR(1,0,0,1,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x4F 'O' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x50 'P' */ GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0),
    /* 0x51 'Q' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,1,0,1), GR(1,0,0,1,0), GR(0,1,1,0,1),
    /* 0x52 'R' */ GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,0), GR(1,0,1,0,0), GR(1,0,0,1,0), GR(1,0,0,0,1),
    /* 0x53 'S' */ GR(0,1,1,1,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(0,1,1,1,0), GR(0,0,0,0,1), GR(0,0,0,0,1), GR(1,1,1,1,0),
    /* 0x54 'T' */ GR(1,1,1,1,1), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0),
    /* 0x55 'U' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x56 'V' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,0,1,0), GR(0,0,1,0,0),
    /* 0x57 'W' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,1,0,1), GR(1,0,1,0,1), GR(1,1,0,1,1), GR(1,0,0,0,1),
    /* 0x58 'X' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,0,1,0), GR(0,0,1,0,0), GR(0,1,0,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x59 'Y' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,0,1,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0),
    /* 0x5A 'Z' */ GR(1,1,1,1,1), GR(0,0,0,0,1), GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,1,0,0,0), GR(1,0,0,0,0), GR(1,1,1,1,1),
    /* 0x5B-0x60 unused */ 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0,
    /* 0x61 'a' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(0,0,0,0,1), GR(0,1,1,1,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x62 'b' */ GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,1,1,0), GR(1,1,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x63 'c' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x64 'd' */ GR(0,0,0,0,1), GR(0,0,0,0,1), GR(0,1,1,0,1), GR(1,0,0,1,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x65 'e' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,1), GR(1,0,0,0,0), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x66 'f' */ GR(0,0,1,1,0), GR(0,1,0,0,1), GR(0,1,0,0,0), GR(1,1,1,0,0), GR(0,1,0,0,0), GR(0,1,0,0,0), GR(0,1,0,0,0),
    /* 0x67 'g' */ GR(0,1,1,1,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,1), GR(0,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x68 'h' */ GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,1,1,0), GR(1,1,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x69 'i' */ GR(0,0,1,0,0), 0, GR(0,1,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,1,1,1,0),
    /* 0x6A 'j' */ GR(0,0,0,1,0), 0, GR(0,0,0,1,0), GR(0,0,0,1,0), GR(0,0,0,1,0), GR(1,0,0,1,0), GR(0,1,1,0,0),
    /* 0x6B 'k' */ GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,1,0), GR(1,0,1,0,0), GR(1,1,0,0,0), GR(1,0,1,0,0), GR(1,0,0,1,0),
    /* 0x6C 'l' */ GR(0,1,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,0,1,0,0), GR(0,1,1,1,0),
    /* 0x6D 'm' */ GR(1,1,0,1,1), GR(1,0,1,0,1), GR(1,0,1,0,1), GR(1,0,1,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x6E 'n' */ GR(1,0,1,1,0), GR(1,1,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1),
    /* 0x6F 'o' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x70 'p' */ GR(1,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,1,1,1,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0),
    /* 0x71 'q' */ GR(0,1,1,1,0), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,0,1), GR(0,0,0,1,1), GR(0,0,0,0,1),
    /* 0x72 'r' */ GR(1,0,1,1,0), GR(1,1,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(1,0,0,0,0),
    /* 0x73 's' */ GR(0,1,1,1,1), GR(1,0,0,0,0), GR(1,0,0,0,0), GR(0,1,1,1,0), GR(0,0,0,0,1), GR(0,0,0,0,1), GR(1,1,1,1,0),
    /* 0x74 't' */ GR(0,1,0,0,0), GR(0,1,0,0,0), GR(1,1,1,0,0), GR(0,1,0,0,0), GR(0,1,0,0,0), GR(0,1,0,0,1), GR(0,0,1,1,0),
    /* 0x75 'u' */ 0, 0, GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,1,1), GR(0,1,1,0,1),
    /* 0x76 'v' */ 0, 0, GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,0,1,0), GR(0,0,1,0,0),
    /* 0x77 'w' */ 0, 0, GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,1,0,1), GR(1,0,1,0,1), GR(0,1,0,1,0),
    /* 0x78 'x' */ 0, 0, GR(1,0,0,0,1), GR(0,1,0,1,0), GR(0,0,1,0,0), GR(0,1,0,1,0), GR(1,0,0,0,1),
    /* 0x79 'y' */ GR(1,0,0,0,1), GR(1,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,1), GR(0,0,0,0,1), GR(1,0,0,0,1), GR(0,1,1,1,0),
    /* 0x7A 'z' */ 0, 0, GR(1,1,1,1,1), GR(0,0,0,1,0), GR(0,0,1,0,0), GR(0,1,0,0,0), GR(1,1,1,1,1)
};

#undef GR

/* idx*stride+row is the deliberately-exposed seam: every caller outside
 * font_selftest passes FONT_STRIDE; the test corrupts it to prove the
 * pixel-count checker actually notices a misread glyph.
 *
 * `scale` is separate from that seam and is the ordinary drawing knob: every
 * pre-menu caller passes FONT_SCALE and is unchanged, and the menu title asks
 * for 4. A scaled glyph is the same bits with a bigger fill_rect per lit pixel
 * rather than a second glyph table, so there is still exactly one font. */
static void draw_glyph(SDL_Surface *fb, int x, int y, int ch, Uint32 colour,
                       int stride, int scale)
{
    int idx = ch - FONT_FIRST;
    int row, col;

    if (idx < 0 || idx >= FONT_GLYPHS)
        return;
    if (scale < 1)
        return;
    for (row = 0; row < FONT_H; row++) {
        Uint8 bits = FONT_5X7[idx * stride + row];
        for (col = 0; col < FONT_W; col++)
            if (bits & (1u << (FONT_W - 1 - col)))
                fill_rect(fb, x + col * scale, y + row * scale,
                          scale, scale, colour);
    }
}

/* Advance and line height are FONT_ADV/FONT_LINE scaled by the same factor, so
 * a scaled run keeps the one-pixel gap the 5x7 table was drawn to have. */
#define FONT_ADV_AT(sc)  ((FONT_W + 1) * (sc))
#define FONT_LINE_AT(sc) ((FONT_H + 1) * (sc))

static void draw_text_scaled(SDL_Surface *fb, int x, int y, const char *str,
                             Uint32 colour, int scale)
{
    int cx = x;
    for (; *str; str++) {
        if (*str == '\n') { cx = x; y += FONT_LINE_AT(scale); continue; }
        draw_glyph(fb, cx, y, (unsigned char)*str, colour, FONT_STRIDE, scale);
        cx += FONT_ADV_AT(scale);
    }
}

static void draw_text(SDL_Surface *fb, int x, int y, const char *str, Uint32 colour)
{
    draw_text_scaled(fb, x, y, str, colour, FONT_SCALE);
}

/* One font pixel down-right in black, then the real colour on top - legible
 * over arbitrary terrain without needing a backing panel behind it. */
static void draw_text_shadow(SDL_Surface *fb, int x, int y, const char *str, Uint32 colour)
{
    draw_text(fb, x + FONT_SCALE, y + FONT_SCALE, str, 0);
    draw_text(fb, x, y, str, colour);
}

static int text_w_scaled(const char *s, int scale)
{
    return (int)SDL_strlen(s) * FONT_ADV_AT(scale);
}

static int text_w(const char *s) { return text_w_scaled(s, FONT_SCALE); }

/* ---- The game state -----------------------------------------------------
 *
 * One aggregate, so save/load can snapshot and compare it wholesale and the
 * HUD has a single thing to read. Deliberately introduced only now: until
 * there was something to serialise, loose locals in main() were honest about
 * how little state there was. ~82 KB, so it is heap-allocated - see the
 * no-world-sized-statics rule at the top of this file.
 */
typedef struct {
    World  w;
    Player p;
    Entity ents[ENTITY_COUNT];
    Uint64 seed;
    int    frags_restored;
    int    souls_restored;
    Uint8  area;      /* 1, 2 or 3 - which area ents[]/frags/souls describe now */
    Uint32 restored;  /* persistent across an area switch: bits 0-9 area 1,
                        * bits 10-19 area 2, bits 20-29 area 3. ents[] only
                        * ever holds the ACTIVE area's 10 entities, so this is
                        * what carries the other areas' progress while they
                        * are not loaded. */
    /* Which areas' map fragments she has found: bit 0 area 1, bit 1 area 2,
     * bit 2 area 3. Persistent across an area switch for the same reason
     * `restored` is - only one area's World is ever loaded, so a per-World
     * flag would forget the Forest map the moment she stepped through the
     * portal and remember it again if she somehow came back. Three bits rather
     * than three more bits of `restored`: `restored` is thirty bits deep
     * already, and a map is not a memory - it grants nothing, restores
     * nothing, and area_complete must not count it. */
    Uint8  maps;
} Game;

/* Area 2 and Area 3's worlds are each a deterministic salt of the root seed,
 * not a second random source - same seed, same salt, same world, every
 * time. Distinct 64-bit constants (not the plan doc's 0xDEADBEEF/0xCAFEBABE -
 * those are documentation, not what the code ever used): a shared salt would
 * make Area 3 a re-skin of Area 2's exact layout instead of its own world. */
#define AREA2_SEED_SALT 0x9E3779B97F4A7C15ULL
#define AREA3_SEED_SALT 0xFF51AFD7ED558CCDULL

/* Generate the world for ONE area and stand the player in it. The ONLY path
 * from a seed to a playable state for either area - a fresh start, a load and
 * the portal transition all come through here, so nothing can drift from what
 * generation produces. Does not touch g->restored: callers that need the new
 * area's local entities replayed from it do so afterward, the same way
 * game_load already replays a loaded mask - this is that same pattern with an
 * area argument, not a second one.
 *
 * Abilities reset per area (SDL_zero(g->p) below), not carried over: Area 2's
 * own first three entities re-grant Wade/Climb/Kindle exactly the way Area 1's
 * do (place_entities is unchanged and area-agnostic), and threading an
 * initial-held value into that proven gating/reachability code for a purely
 * narrative payoff was judged not worth the risk. */
static void game_init_area(Game *g, Scratch *sc, Uint64 seed, Uint8 area)
{
    Uint64 area_seed = (area == 1) ? seed
                      : (area == 2) ? (seed ^ AREA2_SEED_SALT)
                      : (seed ^ AREA3_SEED_SALT);
    Uint8  biome      = (area == 1) ? BIOME_FOREST
                       : (area == 2) ? BIOME_UNDERWORLD
                       : BIOME_LUMIARA;

    SDL_zero(g->p);
    g->seed = seed;
    g->area = area;
    g->frags_restored = 0;
    g->souls_restored = 0;
    (void)world_gen(&g->w, sc, g->ents, area_seed, biome);
    if (g->w.spawn_tile >= 0) {
        g->p.x = (float)(g->w.spawn_tile % WORLD_W) * TILE + TILE * 0.5f;
        g->p.y = (float)(g->w.spawn_tile / WORLD_W) * TILE + TILE * 0.5f;
    } else {
        g->p.x = (float)(WORLD_W * TILE) * 0.5f;
        g->p.y = (float)(WORLD_H * TILE) * 0.5f;
    }
    g->p.facing = FACE4_DOWN;
}

/* Fresh Area 1 game: nothing banked yet. */
static void game_init(Game *g, Scratch *sc, Uint64 seed)
{
    g->restored = 0;
    g->maps = 0;
    game_init_area(g, sc, seed, 1);
}

/* Whether the CURRENT area's map has been found. g->area is always 1-3, so the
 * shift is always in range. */
static int game_has_map(const Game *g)
{
    return (g->maps & (Uint8)(1u << (g->area - 1))) != 0;
}

static void game_restore(Game *g, int i)
{
    apply_restore(&g->w, g->ents, &g->p, i,
                  &g->frags_restored, &g->souls_restored);
}

/* Area 1 is finished when everything in it is remembered. Deliberately NOT a
 * stored flag: a derived predicate cannot get out of step with the thing it
 * describes, and it needs no byte in the save file. */
static int area_complete(const Game *g)
{
    return g->frags_restored >= FRAGMENT_COUNT && g->souls_restored >= SOUL_COUNT;
}

/* ---- Save and load ------------------------------------------------------
 *
 * A save is a seed plus the deltas play has made on top of it: where she
 * stands, what she carries, and what is remembered. Loading REGENERATES the
 * world from the seed and replays the deltas - there is no second construction
 * path that could drift from what generation produces.
 *
 * Flat, fixed-size, versioned, every multi-byte field written little-endian by
 * hand, so neither struct padding nor host endianness can leak into the file.
 *
 * Not saved, on purpose: the per-tile fog (16 KB, rebuilt on load as one
 * instant of standing at the saved position - exactly what reveal_around
 * converges to there), the eased restoration floats (snapped to their targets;
 * a mid-ease value is animation, not progress), and render-only state.
 */
#define SAVE_MAGIC_0  'W'
#define SAVE_MAGIC_1  'F'
/* Bumped from 1 when byte 21 stopped being reserved and became the map-fragment
 * mask. A v1 file happens to carry a zero there, which would even READ
 * correctly as "no maps found" - the bump is not because it would be
 * misinterpreted but because the format changed, and a version field that only
 * moves when a change would otherwise corrupt something is a version field
 * nobody can reason about. v1 saves are rejected, not migrated: there is one
 * construction path from a file to a game, and a migration would be a second. */
#define SAVE_VERSION  3
#define SAVE_SIZE     36
/* Six slots, probed by name. SDL2 has no directory-listing API at all, so a
 * fixed set of filenames is the only way to enumerate saves without dropping to
 * platform-specific code - and a fixed set is what the rest of this file is
 * made of anyway. Six is what fits the 270 px screen as one page with no
 * scrolling, which is worth more than a longer list nobody can see the end of.
 * Numbered from 1 in the filename and on screen, from 0 in the array. */
#define SAVE_SLOTS    6
#define SAVE_PATH_MAX 32

/* Bytes 28-35: when this save was written, as seconds from time().
 *
 * Stored because "continue" has to mean the most recent save and nothing else
 * in the file can order two saves against each other. Deliberately NOT used for
 * anything but ordering: a clock that is wrong, or that went backwards between
 * two saves, puts them in a surprising order and does nothing worse - no save
 * is rejected for its timestamp beyond being zero, which is what a failed
 * time() looks like and would silently sort as "oldest forever". */
static void save_slot_path(char *dst, size_t cap, int slot)
{
    SDL_snprintf(dst, cap, "wayfarer%d.sav", slot + 1);
}

/* Byte 21: three legal bits, one per area. */
#define SAVE_MAPS_BITS 0x07u

/* Byte 24-27's restored mask now spans 30 bits: 0-9 Area 1's ENTITY_COUNT
 * entities, 10-19 Area 2's, 20-29 Area 3's. All three are always in scope
 * regardless of which area is currently active, because ents[] only ever
 * holds the ACTIVE area's 10 - see Game.restored. */
#define SAVE_AREA1_BITS  ((Uint32)((1u << ENTITY_COUNT) - 1u))
#define SAVE_AREA2_SHIFT ENTITY_COUNT
#define SAVE_AREA3_SHIFT (2 * ENTITY_COUNT)
#define SAVE_ALL_BITS    ((Uint32)((1u << (3 * ENTITY_COUNT)) - 1u))

static void save_put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)(v);       p[1] = (Uint8)(v >> 8);
    p[2] = (Uint8)(v >> 16); p[3] = (Uint8)(v >> 24);
}

static void save_put64(Uint8 *p, Uint64 v)
{
    save_put32(p, (Uint32)v);
    save_put32(p + 4, (Uint32)(v >> 32));
}

static Uint32 save_get32(const Uint8 *p)
{
    return (Uint32)p[0] | ((Uint32)p[1] << 8)
         | ((Uint32)p[2] << 16) | ((Uint32)p[3] << 24);
}

static Uint64 save_get64(const Uint8 *p)
{
    return (Uint64)save_get32(p) | ((Uint64)save_get32(p + 4) << 32);
}

static float save_getf32(const Uint8 *p)
{
    Uint32 u = save_get32(p);
    float f;
    SDL_memcpy(&f, &u, sizeof(f));
    return f;
}

static int game_save(const Game *g, const char *path)
{
    Uint8 buf[SAVE_SIZE];
    /* g->restored already carries the INACTIVE areas' bits (folded in by
     * game_transition_to_area when it switched away); only the ACTIVE area's
     * slice needs deriving fresh from the live ents[] here, at its own bit
     * offset, same as game_save always has. keep_mask is everything EXCEPT
     * the active area's own slice - not "the other area", now that there are
     * two others - so the active slice can be safely OR'd back in fresh. */
    Uint32 active = 0, restored, fx, fy;
    Uint32 shift = (g->area == 1) ? 0 : (g->area == 2) ? SAVE_AREA2_SHIFT : SAVE_AREA3_SHIFT;
    Uint32 keep_mask = SAVE_ALL_BITS & ~(SAVE_AREA1_BITS << shift);
    SDL_RWops *rw;
    int i;

    for (i = 0; i < ENTITY_COUNT; i++)
        if (g->ents[i].restored)
            active |= 1u << i;
    restored = (g->restored & keep_mask) | (active << shift);

    buf[0] = SAVE_MAGIC_0;
    buf[1] = SAVE_MAGIC_1;
    buf[2] = SAVE_VERSION;
    buf[3] = g->area;
    save_put64(buf + 4, g->seed);
    SDL_memcpy(&fx, &g->p.x, sizeof(fx));
    SDL_memcpy(&fy, &g->p.y, sizeof(fy));
    save_put32(buf + 12, fx);
    save_put32(buf + 16, fy);
    buf[20] = g->p.abilities;
    buf[21] = g->maps;
    buf[22] = 0;
    buf[23] = 0;
    save_put32(buf + 24, restored);
    /* 0 is what a failed time() looks like, and a 0 here would sort this save
     * as older than every other one forever. 1 is a lie about WHEN, but it is
     * only ever compared, never displayed, so the lie costs an ordering nobody
     * can rely on anyway on a machine whose clock does not work. */
    {
        time_t now = time(NULL);
        Uint64 stamp = (now == (time_t)-1) ? 1u : (Uint64)now;
        if (stamp == 0) stamp = 1u;
        save_put64(buf + 28, stamp);
    }

    rw = SDL_RWFromFile(path, "wb");
    if (!rw)
        return -1;
    i = (SDL_RWwrite(rw, buf, 1, SAVE_SIZE) == SAVE_SIZE) ? 0 : -1;
    if (SDL_RWclose(rw) != 0)
        i = -1;
    return i;
}

/* Everything about a save file that can be judged from its 28 bytes alone,
 * without regenerating anything. Split out of game_load so that "is there a
 * save worth offering?" and "load it" ask the SAME question - a menu row lit by
 * a bare file-exists check would offer a corrupt or a v1 file and then fail on
 * it, which reads as the game being broken rather than as there being no save.
 * This is not a second construction path: it constructs nothing, and game_load
 * still re-derives every field it uses from the same buffer.
 * Returns 0 when the header is sound, -1 otherwise. */
static int save_header_ok(const Uint8 *buf)
{
    Uint8  area, maps, abilities;
    Uint32 restored;
    float  px, py;

    if (buf[0] != SAVE_MAGIC_0 || buf[1] != SAVE_MAGIC_1) return -1;
    if (buf[2] != SAVE_VERSION)                           return -1;
    if (buf[3] != 1 && buf[3] != 2 && buf[3] != 3)        return -1;
    if (buf[22] != 0 || buf[23] != 0)                     return -1;
    area = buf[3];

    /* Same shape of check as the restored mask's below, and it falls out of the
     * same live invariant: a map fragment lies in ONE area, and she cannot have
     * picked up an area's map without having been in that area, which
     * progression makes impossible for any area past the one she is in. */
    maps = buf[21];
    if (maps & (Uint8)~(Uint8)SAVE_MAPS_BITS)             return -1;
    if (maps & (Uint8)~(Uint8)((1u << area) - 1u))        return -1;

    restored = save_get32(buf + 24);
    if (restored & ~SAVE_ALL_BITS) return -1;
    /* Free integrity check that falls straight out of the gameplay invariant
     * the portal enforces live: you cannot BE in Area N unless every earlier
     * area is 100% restored (that is what unlocks each portal), and you
     * cannot have left an area with spurious LATER-area progress already on
     * the books - progress can only ever be ahead of where you currently are
     * by exactly the areas you have already finished and left. */
    if (area == 1 && (restored & ((SAVE_AREA1_BITS << SAVE_AREA2_SHIFT) |
                                   (SAVE_AREA1_BITS << SAVE_AREA3_SHIFT)))) return -1;
    if (area == 2 && ((restored & SAVE_AREA1_BITS) != SAVE_AREA1_BITS ||
                       (restored & (SAVE_AREA1_BITS << SAVE_AREA3_SHIFT)))) return -1;
    if (area == 3 && (restored & (SAVE_AREA1_BITS | (SAVE_AREA1_BITS << SAVE_AREA2_SHIFT)))
                   != (SAVE_AREA1_BITS | (SAVE_AREA1_BITS << SAVE_AREA2_SHIFT)))  return -1;

    abilities = buf[20];
    if (abilities & (Uint8)~(Uint8)ABIL_ALL) return -1;

    px = save_getf32(buf + 12);
    py = save_getf32(buf + 16);
    /* Written as !(x >= 0) rather than (x < 0) so it also rejects NaN, which
     * compares false against everything and would otherwise sail through. */
    if (!(px >= 0.0f) || !(py >= 0.0f) ||
        !(px < (float)WORLD_W * TILE) || !(py < (float)WORLD_H * TILE))
        return -1;

    /* The only thing asked of the timestamp: that it is not zero. Any real
     * value is accepted, however implausible - see the note at SAVE_SLOTS. */
    if (save_get64(buf + 28) == 0)
        return -1;

    return 0;
}

/* What the menu knows about one slot WITHOUT regenerating its world.
 *
 * Everything here is read straight out of the 36-byte header, which is what
 * keeps a six-slot list cheap enough to build: six small reads when the menu
 * opens, and none at all while it is on screen.
 *
 * `done` is the count of restored entities in the save's OWN area, not a
 * fragments-and-souls split. Which entity is a soul is a property of the
 * generated world, not of the mask, so telling the two apart from a header
 * alone is impossible - and storing the split in the file would be storing a
 * derived value that nothing could check the mask against. A single honest
 * count beats two numbers that might disagree with the save they describe. */
typedef struct {
    int    used;
    Uint8  area;    /* 1..3    */
    int    done;    /* 0..ENTITY_COUNT restored in that area */
    Uint64 stamp;
} SaveSlot;

static int save_popcount32(Uint32 v)
{
    int n = 0;
    while (v) { v &= v - 1u; n++; }
    return n;
}

/* Read every slot's header into `out`. Called when the menu opens and after
 * anything that writes a save - never per frame: the rows are rebuilt at 60 Hz
 * while the menu is up, and six file reads a frame to answer a question that
 * cannot change without one of those events would be a syscall storm in the
 * render path. Returns how many slots are in use. */
static int save_scan(SaveSlot *out)
{
    char path[SAVE_PATH_MAX];
    Uint8 buf[SAVE_SIZE];
    int i, used = 0;

    SDL_memset(out, 0, sizeof(SaveSlot) * SAVE_SLOTS);
    for (i = 0; i < SAVE_SLOTS; i++) {
        SDL_RWops *rw;
        Uint32 restored, shift;

        save_slot_path(path, sizeof(path), i);
        rw = SDL_RWFromFile(path, "rb");
        if (!rw)
            continue;
        if (SDL_RWread(rw, buf, 1, SAVE_SIZE) != (size_t)SAVE_SIZE) {
            SDL_RWclose(rw);
            continue;
        }
        SDL_RWclose(rw);
        /* The SAME judgement game_load makes, so a slot is listed as usable
         * exactly when it is - a row lit by a file that then refuses to load
         * reads as the game being broken rather than as an empty slot. */
        if (save_header_ok(buf) != 0)
            continue;

        out[i].used  = 1;
        out[i].area  = buf[3];
        out[i].stamp = save_get64(buf + 28);
        restored = save_get32(buf + 24);
        shift = (Uint32)(out[i].area - 1) * (Uint32)ENTITY_COUNT;
        out[i].done = save_popcount32((restored >> shift) & SAVE_AREA1_BITS);
        used++;
    }
    return used;
}

/* The most recently written slot, or -1 if there are none. This is the whole
 * of what "continue" means. Ties go to the lower slot, which only happens when
 * two saves share a second. */
static int save_newest(const SaveSlot *slots)
{
    int i, best = -1;

    for (i = 0; i < SAVE_SLOTS; i++) {
        if (!slots[i].used)
            continue;
        if (best < 0 || slots[i].stamp > slots[best].stamp)
            best = i;
    }
    return best;
}

/* The lowest slot with no save in it, or -1 when every slot is taken. A new
 * game takes this one; when there is none, the player is asked which save to
 * replace rather than one being chosen for them. */
static int save_first_free(const SaveSlot *slots)
{
    int i;

    for (i = 0; i < SAVE_SLOTS; i++)
        if (!slots[i].used)
            return i;
    return -1;
}

/* Regenerate from the saved seed, then replay the deltas. Returns 0 on success.
 *
 * This is the first boundary in the project where outside input reaches the
 * program, and it is validated accordingly. EVERY malformed input - missing
 * file, short read, bad magic, unknown version, wrong area, nonzero reserved
 * bytes, mask bits past the entity array, illegal ability bits, an ability set
 * the restored mask does not account for, a non-finite or out-of-bounds or
 * wall-trapped position - is rejected BEFORE the live game is touched. The
 * world is regenerated into scratch, checked against the regenerated solid map,
 * and only then copied over. A corrupt file can never leave the player half
 * loaded, and a failed load leaves *g bit-for-bit as it was.
 *
 * Byte 20 (abilities) is strictly redundant: abilities are exactly the OR of
 * grants over the restored entities, so it is stored as a CHECKSUM on the
 * restored mask and rejected when the two disagree. */
static int game_load(Game *g, Scratch *sc, const char *path, Uint64 *seed_out)
{
    Uint8 buf[SAVE_SIZE];
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    Game *tmp;
    Uint64 seed;
    Uint32 restored, active, shift;
    Uint8 area;
    float px, py;
    Uint8 abilities, maps;
    int i;

    if (!rw)
        return -1;
    if (SDL_RWread(rw, buf, 1, SAVE_SIZE) != SAVE_SIZE) {
        SDL_RWclose(rw);
        return -1;
    }
    SDL_RWclose(rw);

    if (save_header_ok(buf) != 0)
        return -1;

    /* Re-derived from the same buffer the header check just judged, rather
     * than handed back through out-params: one reader, one set of offsets. */
    area      = buf[3];
    maps      = buf[21];
    restored  = save_get32(buf + 24);
    abilities = buf[20];
    seed      = save_get64(buf + 4);
    px        = save_getf32(buf + 12);
    py        = save_getf32(buf + 16);

    /* File-level validation done. Regenerate into scratch: Game is far too big
     * to keep two of on the stack, and the live game must stay untouched until
     * the position has been checked against the regenerated solid map. */
    tmp = (Game *)SDL_malloc(sizeof(Game));
    if (!tmp)
        return -1;
    game_init_area(tmp, sc, seed, area);

    shift = (area == 1) ? 0 : (area == 2) ? SAVE_AREA2_SHIFT : SAVE_AREA3_SHIFT;
    active = (restored >> shift) & SAVE_AREA1_BITS;
    for (i = 0; i < ENTITY_COUNT; i++)
        if (active & (1u << i))
            game_restore(tmp, i);
    if (tmp->p.abilities != abilities) {   /* the checksum described above */
        SDL_free(tmp);
        return -1;
    }
    if (player_blocked(&tmp->w, abilities, px, py)) {
        SDL_free(tmp);                     /* a position generation never produced */
        return -1;
    }
    tmp->p.x = px;
    tmp->p.y = py;
    tmp->restored = restored;              /* the full 20-bit mask, both areas */
    /* Replayed as a delta on the regenerated world, exactly like a restored
     * entity: world_gen has just laid this area's map fragment back down, and
     * having found it means it is not lying there any more. */
    tmp->maps = maps;
    if (maps & (Uint8)(1u << (area - 1)))
        tmp->w.map_tile = -1;

    /* Snap the eased floats to their targets, then rebuild the fog as one
     * instant of standing where she stands. reveal_around is incremental, so it
     * is given a dt large enough to reach the taper in a single call - the same
     * values a player who stood still would have converged to. */
    for (i = 0; i < tmp->w.region_count; i++)
        tmp->w.regions[i].restoration = tmp->w.regions[i].restore_to;
    reveal_around(&tmp->w, px, py, 1.0f);

    SDL_memcpy(g, tmp, sizeof(Game));      /* every check passed: commit */
    SDL_free(tmp);
    *seed_out = seed;
    return 0;
}

/* ---- Settings -----------------------------------------------------------
 *
 * The first state in this project that is neither world nor progress: what the
 * PLAYER prefers, which survives a new game and has nothing to do with a seed.
 *
 * Its own file, not spare bytes in the save. Save bytes 22-23 are reserved and
 * must be zero, so spending them would force a SAVE_VERSION bump, and a bump
 * rejects every save already on disk (v1 files are refused, not migrated) - a
 * volume slider is not worth deleting someone's game for. It also would not
 * work: settings must be readable BEFORE a world exists, to size the window.
 *
 * Same discipline as the save all the same - flat, fixed-size, versioned, every
 * field hand-packed little-endian, validated completely before anything live is
 * touched. It is one byte per field here, so "little-endian" costs nothing to
 * honour and the format does not become a special case the day a field grows.
 *
 *   byte 0-1  'W','C'
 *        2    CFG_VERSION
 *        3    music volume  0..VOL_MAX
 *        4    sound volume  0..VOL_MAX
 *        5    fullscreen    0 or 1
 *        6    window scale  1..WIN_SCALE_MAX
 *        7    reserved, must be zero
 */
#define CFG_MAGIC_0   'W'
#define CFG_MAGIC_1   'C'
#define CFG_VERSION   1
#define CFG_SIZE      8
#define CFG_FILENAME  "wayfarer.cfg"

typedef struct {
    int music;       /* 0..VOL_MAX */
    int sfx;         /* 0..VOL_MAX */
    int fullscreen;  /* 0 or 1     */
    int scale;       /* 1..WIN_SCALE_MAX; 0 = "pick one for me", never stored */
} Settings;

static void cfg_defaults(Settings *c)
{
    c->music      = VOL_MAX;
    c->sfx        = VOL_MAX;
    c->fullscreen = 0;
    c->scale      = 0;
}

static int cfg_save(const Settings *c, const char *path)
{
    Uint8 buf[CFG_SIZE];
    SDL_RWops *rw;
    int r;

    buf[0] = CFG_MAGIC_0;
    buf[1] = CFG_MAGIC_1;
    buf[2] = CFG_VERSION;
    buf[3] = (Uint8)c->music;
    buf[4] = (Uint8)c->sfx;
    buf[5] = (Uint8)(c->fullscreen ? 1 : 0);
    /* A scale of 0 means pick_scale chose it, which is a property of the
     * display and not a preference - stored as the scale actually in use so a
     * reload reproduces the window the player last saw. */
    buf[6] = (Uint8)(c->scale >= 1 && c->scale <= WIN_SCALE_MAX ? c->scale : 1);
    buf[7] = 0;

    rw = SDL_RWFromFile(path, "wb");
    if (!rw)
        return -1;
    r = (SDL_RWwrite(rw, buf, 1, CFG_SIZE) == CFG_SIZE) ? 0 : -1;
    if (SDL_RWclose(rw) != 0)
        r = -1;
    return r;
}

/* Validated into a local and copied out only once every field has passed, so a
 * malformed file leaves *c exactly as it was - the same rule the save obeys,
 * for the same reason: half-applied settings are worse than none. A missing
 * file is not an error, it is a first run; the caller keeps its defaults. */
static int cfg_load(Settings *c, const char *path)
{
    Uint8 buf[CFG_SIZE];
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    Settings tmp;

    if (!rw)
        return -1;
    if (SDL_RWread(rw, buf, 1, CFG_SIZE) != CFG_SIZE) {
        SDL_RWclose(rw);
        return -1;
    }
    SDL_RWclose(rw);

    if (buf[0] != CFG_MAGIC_0 || buf[1] != CFG_MAGIC_1) return -1;
    if (buf[2] != CFG_VERSION)                          return -1;
    if (buf[3] > VOL_MAX)                               return -1;
    if (buf[4] > VOL_MAX)                               return -1;
    if (buf[5] > 1)                                     return -1;
    if (buf[6] < 1 || buf[6] > WIN_SCALE_MAX)           return -1;
    if (buf[7] != 0)                                    return -1;

    tmp.music      = buf[3];
    tmp.sfx        = buf[4];
    tmp.fullscreen = buf[5];
    tmp.scale      = buf[6];
    *c = tmp;
    return 0;
}

/* The two halves of "make the world match these settings". Separate because
 * they answer to different owners - one crosses into the audio callback and
 * may only go through atomics, the other is a window call that must happen on
 * the thread that made the window - and because the audio half is the only one
 * that is safe to call before there is a window at all. */
static void cfg_apply_audio(Audio *a, const Settings *c)
{
    SDL_AtomicSet(&a->music_vol, c->music);
    SDL_AtomicSet(&a->sfx_vol,   c->sfx);
}

static void cfg_apply_window(SDL_Window *win, const Settings *c)
{
    SDL_SetWindowFullscreen(win, c->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    /* Resizing while fullscreen would fight the display for the resolution and
     * leave the window the wrong size on the way back out. The scale is kept in
     * the settings either way, so leaving fullscreen restores it. */
    if (!c->fullscreen && c->scale >= 1)
        SDL_SetWindowSize(win, LOGICAL_W * c->scale, LOGICAL_H * c->scale);
}

/* ---- HUD ----------------------------------------------------------------
 *
 * Two counters, the abilities she has earned, a minimap, one-line toasts and a
 * completion banner. Nothing else.
 *
 * The minimap is drawn into a cached surface and blitted, so the per-frame cost
 * is one blit plus a handful of markers rather than 16,384 fill_rects. The
 * cache refreshes on an explicit dirty flag (a restore) or every 15 frames,
 * which is often enough to follow the fog opening up.
 *
 * These file-scope bytes are NOT a breach of the no-statics rule: that rule is
 * about world-sized arrays landing in .data, and this is ~90 bytes plus one
 * heap surface pointer.
 */
#define HUD_TOAST_TICKS 180   /* 3 s at 60 Hz */
#define HUD_WIN_TICKS   360   /* 6 s */
#define MM_STEP  2            /* world tiles per minimap pixel */
#define MM_W     (WORLD_W / MM_STEP)
#define MM_H     (WORLD_H / MM_STEP)
#define MM_X     (LOGICAL_W - MM_W - 6)
#define MM_Y     6

static struct {
    char         toast[64];
    int          toast_left;
    int          win_left;
    int          win_shown;
    int          mm_dirty;
    int          mm_tick;
    SDL_Surface *mm;
    /* The full-screen map. Same cached-surface-plus-dirty-flag shape as the
     * minimap above and for the same reason, one step larger: a rebuild is
     * 16,384 blocks plus a BFS, which is nothing once every fifteen frames and
     * real work every frame. */
    int          map_open;
    int          bm_dirty;
    int          bm_tick;
    int          bm_closed;   /* collectibles with no route she can walk today */
    SDL_Surface *bm;
} hud;

static void hud_toast(const char *s)
{
    SDL_strlcpy(hud.toast, s, sizeof(hud.toast));
    hud.toast_left = HUD_TOAST_TICKS;
}

/* Advanced on the fixed tick rather than per frame, so a toast lasts three
 * seconds whatever the frame rate is doing. */
static void hud_tick(const Game *g)
{
    if (hud.toast_left > 0) hud.toast_left--;
    if (hud.win_left   > 0) hud.win_left--;
    if (area_complete(g) && !hud.win_shown) {
        hud.win_shown = 1;
        hud.win_left = HUD_WIN_TICKS;
    }
}

static Uint32 mm_col(const Game *g, int tx, int ty)
{
    const World *w = &g->w;
    SDL_PixelFormat *f = hud.mm->format;
    float reveal = (float)w->reveal[ty][tx] / 255.0f;
    Uint8 reg = w->region[ty][tx];
    Uint32 c;
    Uint8 r1, g1, b1;

    /* The STRONGER of sight and restoration, which is the rule tile_level uses
     * on screen. Sight alone was wrong: a region she restored and then left
     * went back to black on the minimap while staying lit in the world. */
    if (reg < w->region_count && w->regions[reg].restoration > reveal)
        reveal = w->regions[reg].restoration;

    /* Water is tested BEFORE solid, and the order is the whole point: water IS
     * solid (world_stub marks water and rock alike), so testing solid first
     * made every pond draw as rock and the water branch unreachable. Ponds are
     * a Wade gate she has to find, and the minimap was hiding them among the
     * boulders. --hud-test asserts the two colours differ. */
    if (w->terr[ty][tx] == GT_WATER) {
        c = SDL_MapRGB(f, 0x34, 0x5f, 0x8a);
    } else if (w->solid[ty][tx]) {
        c = SDL_MapRGB(f, 0x0c, 0x0c, 0x10);
    } else {
        /* Unrestored to restored along the same eased float the world renderer
         * uses, so the minimap and the screen never disagree about progress. */
        float r = (reg < w->region_count) ? w->regions[reg].restoration : 0.0f;
        c = SDL_MapRGB(f, (Uint8)(0x3a + (0x55 - 0x3a) * r),
                          (Uint8)(0x40 + (0x85 - 0x40) * r),
                          (Uint8)(0x45 + (0x60 - 0x45) * r));
    }
    if (reveal >= 1.0f)
        return c;
    /* SDL_GetRGB rather than hardcoded >>16/>>8 shifts: the surface format is
     * whatever the window gave us and is not guaranteed to be ARGB8888. */
    SDL_GetRGB(c, f, &r1, &g1, &b1);
    return SDL_MapRGB(f, (Uint8)(0x18 + (r1 - 0x18) * reveal),
                         (Uint8)(0x22 + (g1 - 0x22) * reveal),
                         (Uint8)(0x2e + (b1 - 0x2e) * reveal));
}

/* One minimap pixel per MM_STEP x MM_STEP block of world. Where the block is
 * mixed, the FIRST OPEN tile wins over any wall - a navigation aid that erased
 * one-tile corridors because the sampled corner happened to be rock would be
 * worse than no minimap. */
static void mm_redraw(const Game *g)
{
    int mx, my;

    for (my = 0; my < MM_H; my++)
        for (mx = 0; mx < MM_W; mx++) {
            int bx = mx * MM_STEP, by = my * MM_STEP, sx = bx, sy = by, dx, dy;
            for (dy = 0; dy < MM_STEP; dy++)
                for (dx = 0; dx < MM_STEP; dx++)
                    if (!g->w.solid[by + dy][bx + dx]) {
                        sx = bx + dx; sy = by + dy;
                        dx = dy = MM_STEP;   /* first open tile wins */
                    }
            fill_rect(hud.mm, mx, my, 1, 1, mm_col(g, sx, sy));
        }
    hud.mm_dirty = 0;
}

static void mm_marker(SDL_Surface *fb, int tile, Uint32 col, int size)
{
    int mx = (tile % WORLD_W) / MM_STEP, my = (tile / WORLD_W) / MM_STEP;
    fill_rect(fb, MM_X + mx - size / 2, MM_Y + my - size / 2, size, size, col);
}

static void mm_draw(SDL_Surface *fb, const Game *g)
{
    int i;

    if (!hud.mm)
        hud.mm = SDL_CreateRGBSurface(0, MM_W, MM_H, fb->format->BitsPerPixel,
                                      fb->format->Rmask, fb->format->Gmask,
                                      fb->format->Bmask, fb->format->Amask);
    if (!hud.mm)
        return;   /* a missing minimap is a degraded HUD, not a fatal error */

    if (hud.mm_dirty || (hud.mm_tick++ % 15) == 0)
        mm_redraw(g);
    /* A one-pixel frame, because unexplored terrain is nearly black and an
     * unframed minimap over a dark forest reads as a hole punched in the
     * screen rather than as a map. Drawn as four edges rather than a filled
     * rect behind it - the minimap is opaque, so a backing would be overdraw. */
    {
        Uint32 edge = SDL_MapRGB(fb->format, 0x4a, 0x52, 0x5e);
        SDL_Rect dst;
        fill_rect(fb, MM_X - 1, MM_Y - 1, MM_W + 2, 1, edge);
        fill_rect(fb, MM_X - 1, MM_Y + MM_H, MM_W + 2, 1, edge);
        fill_rect(fb, MM_X - 1, MM_Y, 1, MM_H, edge);
        fill_rect(fb, MM_X + MM_W, MM_Y, 1, MM_H, edge);
        dst.x = MM_X; dst.y = MM_Y; dst.w = 0; dst.h = 0;
        SDL_BlitSurface(hud.mm, NULL, fb, &dst);
    }

    /* Only what she has already seen: an unrevealed collectible on the minimap
     * would hand her the whole area from the first frame. */
    for (i = 0; i < ENTITY_COUNT; i++) {
        int t = g->ents[i].tile;
        if (t < 0 || g->ents[i].restored) continue;
        if (g->w.reveal[t / WORLD_W][t % WORLD_W] < 24) continue;
        mm_marker(fb, t, g->ents[i].is_soul
                        ? SDL_MapRGB(fb->format, 0x9a, 0xd8, 0xe8)
                        : SDL_MapRGB(fb->format, 0xff, 0xd7, 0x6a), 2);
    }
    /* The portal, same reveal-gating as a collectible - a landmark on an
     * unexplored part of the map would hand it away same as an entity would.
     * Distinct violet, size 3 like the player: it is a fixed one-time
     * landmark, not a pickup, and deserves to read as more significant than
     * a fragment or soul marker. Shown regardless of area_complete - once
     * she has seen it, its position is not new information; only whether
     * she can use it yet is, and that is what the HUD banner is for. Areas 1
     * and 2 each have one (their own exit onward); Area 3 is terminal and
     * its portal_tile is never drawn, so this never fires for it. */
    if ((g->area == 1 || g->area == 2) && g->w.portal_tile >= 0 &&
        g->w.reveal[g->w.portal_tile / WORLD_W][g->w.portal_tile % WORLD_W] >= 24) {
        mm_marker(fb, g->w.portal_tile, SDL_MapRGB(fb->format, 0xb0, 0x6a, 0xff), 3);
    }
    mm_marker(fb, (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE),
              SDL_MapRGB(fb->format, 0xff, 0xff, 0xff), 3);
}

static void hud_draw(SDL_Surface *fb, const Game *g)
{
    char buf[64];
    Uint32 warm = SDL_MapRGB(fb->format, 0xf0, 0xd8, 0xb0);
    Uint32 pale = SDL_MapRGB(fb->format, 0x9a, 0xa8, 0xb8);
    /* Not darker: measured on screen, 0x55/0x5e/0x6a over fogged forest was
     * indistinguishable from the terrain, so "there are three of these to
     * find" stopped being communicated at all. This still recedes behind the
     * earned colour without disappearing. */
    Uint32 dim  = SDL_MapRGB(fb->format, 0x78, 0x82, 0x90);
    static const char *abil_name[3] = { "wade", "climb", "kindle" };
    static const Uint8 abil_bit[3]  = { ABIL_WADE, ABIL_CLIMB, ABIL_KINDLE };
    int i, x;

    SDL_snprintf(buf, sizeof(buf), "fragments %d/%d", g->frags_restored, FRAGMENT_COUNT);
    draw_text_shadow(fb, 8, 6, buf, warm);
    SDL_snprintf(buf, sizeof(buf), "souls %d/%d", g->souls_restored, SOUL_COUNT);
    draw_text_shadow(fb, 8, 6 + FONT_LINE, buf, warm);

    /* The abilities, always all three, unearned ones dim. Showing only what she
     * has would hide that there is anything else to find. */
    x = 8;
    for (i = 0; i < 3; i++) {
        int have = (g->p.abilities & abil_bit[i]) != 0;
        draw_text_shadow(fb, x, 6 + FONT_LINE * 2, abil_name[i], have ? warm : dim);
        x += text_w(abil_name[i]) + FONT_ADV;
    }

    if (hud.toast_left > 0) {
        /* The last half second fades toward the background rather than
         * vanishing on a frame boundary. */
        int f = hud.toast_left < 30 ? hud.toast_left : 30;
        Uint32 c = SDL_MapRGB(fb->format, (Uint8)(0x18 + (0xf0 - 0x18) * f / 30),
                                          (Uint8)(0x22 + (0xd0 - 0x22) * f / 30),
                                          (Uint8)(0x2e + (0x90 - 0x2e) * f / 30));
        draw_text_shadow(fb, (fb->w - text_w(hud.toast)) / 2,
                         fb->h - FONT_LINE - 10, hud.toast, c);
    }

    if (hud.win_left > 0) {
        const char *l1 = (g->area == 1) ? "the forest remembers"
                        : (g->area == 2) ? "the underworld remembers"
                        : "the lumiara remembers";
        /* Areas 1 and 2 each open onto a portal; Area 3 is terminal, so its
         * completion banner is the only one that says "all is restored". */
        const char *l2 = (g->area == 1 || g->area == 2) ? "the portal opens" : "all is restored";
        Uint32 c = SDL_MapRGB(fb->format, 0xff, 0xf0, 0xc0);
        draw_text_shadow(fb, (fb->w - text_w(l1)) / 2, fb->h / 2 - 20, l1, c);
        draw_text_shadow(fb, (fb->w - text_w(l2)) / 2, fb->h / 2 - 20 + FONT_LINE, l2, c);
    }

    /* %.0f rather than a 64-bit integer format: the seed is a Uint64, and
     * pulling in the 64-bit formatter costs more than this budget wants to
     * spend on one HUD line. */
    SDL_snprintf(buf, sizeof(buf), "seed %.0f", (double)g->seed);
    draw_text_shadow(fb, 8, fb->h - FONT_LINE - 4, buf, pale);

    mm_draw(fb, g);
}

/* ---- The menu -----------------------------------------------------------
 *
 * One screen serving two jobs, because they are the same screen: the title the
 * game opens on, and the pause screen Escape brings up. The only difference is
 * a couple of rows' wording, so `started` is the whole of the state that
 * separates them - two screens would be two things to keep in step.
 *
 * OPAQUE, and it replaces the world rather than covering it, exactly as the map
 * screen does. There is no alpha blend anywhere in this project and a dimming
 * pass would be a new per-pixel primitive over 129,600 pixels to show a world
 * nobody is looking at; being opaque is also what lets the test count its
 * pixels and lets the negative control assert a closed menu draws nothing.
 *
 * WHAT ROWS EXIST lives in exactly one function, menu_build. Drawing indexes
 * that list, moving the selection indexes that list, and activating indexes
 * that list, so a row cannot be drawn in one place and acted on in another -
 * the same reason synth_add_frag is a function rather than two copies of the
 * unlock ladder. menu_build is pure: everything it reads arrives in a MenuCtx,
 * so the tests drive every page with no window open.
 *
 * Labels may only use characters the 5x7 table actually fills. '(', ')', '[',
 * ']', '+', '*', '%', '_', '#', '&' and '"' are all-zero rows in FONT_5X7 and
 * would ship as holes, so the caret is '>' and the volume bar is '=' and '-'.
 * menu_selftest checks every label against font_bits rather than trusting this
 * comment to be read.
 */
typedef enum {
    MENU_NONE = 0,   /* playing; nothing intercepts input */
    MENU_ROOT,
    MENU_SLOTS,      /* the six saves, for loading or for replacing */
    MENU_CONFIRM,    /* "replace save 3?" - the only destructive step */
    MENU_SETTINGS,
    MENU_CONTROLS
} MenuPage;

/* Why the slot list is up. The same page reads a save or chooses one to throw
 * away, and the difference is entirely in which rows can be picked - so it is
 * one page with a mode rather than two pages that would drift apart. */
enum { SLOTS_LOAD = 0, SLOTS_NEW };

/* What activating a row means, as a value. menu_act is pure and returns one of
 * these; main() is what owns the window, the audio device, the world and the
 * running flag, and does the actual work. Splitting it that way is what lets
 * the test drive every row with no window open and no world generated. */
typedef enum {
    MA_NONE = 0,     /* out of range, a disabled row, or a value row */
    MA_START,        /* resume - close the menu, carry on */
    MA_CONTINUE,     /* load the most recently written save */
    MA_NEWGAME,
    MA_SAVE,         /* write the slot being played */
    MA_LOADPAGE,     /* open the slot list to read one */
    MA_SLOT,         /* act on rows[i].slot, according to the mode */
    MA_YES,          /* confirm the replacement */
    MA_SETTINGS,
    MA_CONTROLS,
    MA_BACK,
    MA_QUIT
} MenuAction;

/* Six slots plus a back row is the longest page, and the settings page is six.
 * Sized to the largest rather than to a guess, and asserted by the test. */
#define MENU_ROWS_MAX (SAVE_SLOTS + 1)
#define MENU_TEXT_MAX 40
/* 4x puts "wayfarer" at 188 px of the 480 the screen is wide - big enough to be
 * a title rather than a line of HUD, with margin left at the widest label. */
#define MENU_TITLE_SCALE 4
/* One blank line between rows. Tighter and the caret has nowhere to sit. */
#define MENU_ROW_H (FONT_LINE * 2)

/* adjust: which value left/right steps, or MADJ_NONE for a plain row. */
enum { MADJ_NONE = 0, MADJ_MUSIC, MADJ_SFX, MADJ_FULLSCREEN, MADJ_SCALE };

typedef struct {
    char       text[MENU_TEXT_MAX];
    int        enabled;
    int        adjust;
    int        slot;    /* which save this row is about, or -1 */
    MenuAction act;
} MenuRow;

static struct {
    MenuPage page;
    int      sel;
    int      started;   /* 0 = title screen, 1 = a game is under way */
    int      mode;      /* SLOTS_LOAD or SLOTS_NEW, while page is MENU_SLOTS */
    int      pending;   /* the slot MENU_CONFIRM is asking about */
} menu;

/* Everything menu_build and menu_draw are allowed to read. Gathered into one
 * value so both are pure functions of it: the test builds a context by hand and
 * drives every page without a window, a world, or a save file. */
typedef struct {
    MenuPage        page;
    int             started;
    int             mode;
    int             pending;
    const Settings *cfg;
    const SaveSlot *slots;
} MenuCtx;

static MenuCtx menu_ctx(const Settings *cfg, const SaveSlot *slots)
{
    MenuCtx c;

    c.page    = menu.page;
    c.started = menu.started;
    c.mode    = menu.mode;
    c.pending = menu.pending;
    c.cfg     = cfg;
    c.slots   = slots;
    return c;
}

/* The volume bar. Ten cells, '=' filled and '-' empty, both of which the font
 * actually has - a bar drawn from a character the table leaves blank would be
 * an invisible control, which is the failure mode the label test exists for. */
static void menu_bar(char *dst, size_t cap, const char *label, int v)
{
    char bar[VOL_MAX + 1];
    int i;

    for (i = 0; i < VOL_MAX; i++)
        bar[i] = (i < v) ? '=' : '-';
    bar[VOL_MAX] = '\0';
    SDL_snprintf(dst, cap, "%s %s %d", label, bar, v);
}

static const char *menu_area_name(Uint8 area)
{
    return area == 1 ? "forest" : area == 2 ? "underworld" : "lumiara";
}

/* One slot's row. What a header can honestly say and nothing more: which world
 * it is in and how much of that world is remembered. `newest` is marked because
 * it is exactly the save "continue" would take, and a list where that is
 * invisible makes continue look like it picked one at random. */
static void menu_slot_text(char *dst, size_t cap, int slot,
                           const SaveSlot *s, int newest)
{
    if (!s->used) {
        SDL_snprintf(dst, cap, "%d   empty", slot + 1);
        return;
    }
    SDL_snprintf(dst, cap, "%d   %s   %d/%d%s", slot + 1,
                 menu_area_name(s->area), s->done, ENTITY_COUNT,
                 newest ? "   newest" : "");
}

/* Every row of the current page, in order. Pure. Returns the count, which is
 * always <= MENU_ROWS_MAX. */
static int menu_build(const MenuCtx *c, MenuRow *rows)
{
    int n = 0, i, newest, any;

    SDL_memset(rows, 0, sizeof(MenuRow) * MENU_ROWS_MAX);
    for (i = 0; i < MENU_ROWS_MAX; i++)
        rows[i].slot = -1;

    newest = save_newest(c->slots);
    any    = (newest >= 0);

    switch (c->page) {
    case MENU_ROOT:
        if (c->started) {
            SDL_strlcpy(rows[n].text, "resume", MENU_TEXT_MAX);
            rows[n].enabled = 1; rows[n].act = MA_START; n++;
            SDL_strlcpy(rows[n].text, "save game", MENU_TEXT_MAX);
            rows[n].enabled = 1; rows[n].act = MA_SAVE; n++;
        } else {
            /* Greyed rather than absent when there is nothing to continue, and
             * the same for "load game" below. A row that appears only once a
             * save exists makes the menu change shape under the player between
             * their first launch and their second; a visible-but-dim row says
             * "this is a thing, you have not got one yet", which is the true
             * statement and the one that keeps the list a fixed shape. */
            SDL_strlcpy(rows[n].text, "continue", MENU_TEXT_MAX);
            rows[n].enabled = any; rows[n].act = MA_CONTINUE;
            rows[n].slot = newest; n++;
        }
        SDL_strlcpy(rows[n].text, "load game", MENU_TEXT_MAX);
        rows[n].enabled = any; rows[n].act = MA_LOADPAGE; n++;
        SDL_strlcpy(rows[n].text, "new game", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_NEWGAME; n++;
        SDL_strlcpy(rows[n].text, "settings", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_SETTINGS; n++;
        SDL_strlcpy(rows[n].text, "quit", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_QUIT; n++;
        break;

    case MENU_SLOTS:
        for (i = 0; i < SAVE_SLOTS; i++) {
            menu_slot_text(rows[n].text, MENU_TEXT_MAX, i, &c->slots[i],
                           i == newest);
            /* Reading needs a save to read; choosing where a new game goes does
             * not, so an empty slot is pickable in one mode and dead in the
             * other. That difference IS the mode. */
            rows[n].enabled = (c->mode == SLOTS_NEW) ? 1 : c->slots[i].used;
            rows[n].act     = MA_SLOT;
            rows[n].slot    = i;
            n++;
        }
        SDL_strlcpy(rows[n].text, "back", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_BACK; n++;
        break;

    case MENU_CONFIRM:
        /* "no" FIRST, so the caret starts on it. This is the only place in the
         * game that destroys something the player made, and the default answer
         * to a question nobody read must be the one that changes nothing. */
        SDL_strlcpy(rows[n].text, "no, keep it", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_BACK; n++;
        SDL_strlcpy(rows[n].text, "yes, replace it", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_YES;
        rows[n].slot = c->pending; n++;
        break;

    case MENU_SETTINGS:
        menu_bar(rows[n].text, MENU_TEXT_MAX, "music", c->cfg->music);
        rows[n].enabled = 1; rows[n].adjust = MADJ_MUSIC; n++;
        menu_bar(rows[n].text, MENU_TEXT_MAX, "sound", c->cfg->sfx);
        rows[n].enabled = 1; rows[n].adjust = MADJ_SFX; n++;
        SDL_snprintf(rows[n].text, MENU_TEXT_MAX, "fullscreen  %s",
                     c->cfg->fullscreen ? "on" : "off");
        rows[n].enabled = 1; rows[n].adjust = MADJ_FULLSCREEN; n++;
        /* Dim while fullscreen is on rather than absent: the window scale is
         * still the player's setting, it simply has nothing to size while the
         * display owns the resolution, and saying so beats the row vanishing. */
        SDL_snprintf(rows[n].text, MENU_TEXT_MAX, "window scale  %d",
                     c->cfg->scale);
        rows[n].enabled = !c->cfg->fullscreen; rows[n].adjust = MADJ_SCALE; n++;
        SDL_strlcpy(rows[n].text, "controls", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_CONTROLS; n++;
        SDL_strlcpy(rows[n].text, "back", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_BACK; n++;
        break;

    case MENU_CONTROLS:
        SDL_strlcpy(rows[n].text, "back", MENU_TEXT_MAX);
        rows[n].enabled = 1; rows[n].act = MA_BACK; n++;
        break;

    case MENU_NONE:
    default:
        break;
    }
    return n;
}

/* What row `idx` does. Out of range and disabled rows are MA_NONE rather than
 * an assert or a clamp: the selection is bounds-checked on every move, so a bad
 * index here means a bug elsewhere, and the honest response to "do the thing at
 * row -1" is to do nothing at all. The test drives exactly that. */
static MenuAction menu_act(const MenuRow *rows, int count, int idx)
{
    if (idx < 0 || idx >= count)
        return MA_NONE;
    if (!rows[idx].enabled)
        return MA_NONE;
    return rows[idx].act;
}

/* The first row that can actually be picked. Used when a page opens, so the
 * caret never starts on a dim row - which is what the very first launch would
 * otherwise show, with "continue" greyed at the top of the list. */
static int menu_first(const MenuRow *rows, int count)
{
    int i;

    for (i = 0; i < count; i++)
        if (rows[i].enabled)
            return i;
    return 0;
}

/* Open a page with the caret on the first row that can actually be picked.
 *
 * Every path that puts a page on screen goes through here - startup, escape,
 * the load list, the settings pages, and the --menu screenshot flag - because
 * they all have to answer the same question and answering it in five places is
 * how four of them end up wrong. The one that WAS wrong is the reason this
 * exists: --menu set the selection to 0 by hand, so the very screenshot taken
 * to check the title screen showed a caret resting on a dim "continue" that a
 * real launch never puts it on. A flag that paints over the thing it was
 * pointed at is worse than no flag. */
static void menu_open(MenuPage page, const Settings *cfg, const SaveSlot *slots)
{
    MenuRow r[MENU_ROWS_MAX];
    MenuCtx c;

    menu.page = page;
    c = menu_ctx(cfg, slots);
    menu.sel  = menu_first(r, menu_build(&c, r));
}

/* Move the selection by `step`, wrapping, skipping disabled rows. Bounded by
 * `count` iterations so a page whose rows are ALL disabled terminates instead
 * of spinning - it leaves the selection where it was, which is the only honest
 * answer when there is nothing to move to. */
static int menu_step(const MenuRow *rows, int count, int sel, int step)
{
    int i, n = sel;

    if (count <= 0)
        return 0;
    for (i = 0; i < count; i++) {
        n += step;
        if (n < 0)       n = count - 1;
        if (n >= count)  n = 0;
        if (rows[n].enabled)
            return n;
    }
    return sel;
}

/* Adjust the value on a row. Clamped rather than wrapped: a player holding
 * right to reach full volume should not fall off the end back to silence. */
static void menu_adjust(Settings *cfg, int adjust, int step)
{
    switch (adjust) {
    case MADJ_MUSIC:
        cfg->music += step;
        if (cfg->music < 0)       cfg->music = 0;
        if (cfg->music > VOL_MAX) cfg->music = VOL_MAX;
        break;
    case MADJ_SFX:
        cfg->sfx += step;
        if (cfg->sfx < 0)       cfg->sfx = 0;
        if (cfg->sfx > VOL_MAX) cfg->sfx = VOL_MAX;
        break;
    case MADJ_FULLSCREEN:
        cfg->fullscreen = !cfg->fullscreen;
        break;
    case MADJ_SCALE:
        cfg->scale += step;
        if (cfg->scale < 1)             cfg->scale = 1;
        if (cfg->scale > WIN_SCALE_MAX) cfg->scale = WIN_SCALE_MAX;
        break;
    default:
        break;
    }
}

/* The three footers, hoisted for the same reason as the control lines: the
 * label test walks them for width and for blank glyphs. Indexed by the enum
 * below so menu_draw picks one rather than spelling them out inline, where the
 * test could not see them. */
enum { MFOOT_TITLE = 0, MFOOT_PAUSED, MFOOT_SUB, MFOOT_COUNT };
static const char *const MENU_FOOTERS[MFOOT_COUNT] = {
    "arrows move   enter choose",
    "arrows move   enter choose   escape resume",
    "arrows move   enter choose   escape back"
};

/* The controls list. Here rather than inside menu_draw so the label test can
 * walk it for font holes: it is the longest text in the game and the most
 * likely place for a character the table leaves blank to slip in. */
static const char *const MENU_CONTROL_LINES[] = {
    "wasd or arrows   walk",
    "e or space       interact",
    "m                map",
    "f5               save this slot",
    "f9               reload this slot",
    "f11              fullscreen",
    "escape           this menu"
};
#define MENU_CONTROL_COUNT \
    ((int)(sizeof(MENU_CONTROL_LINES) / sizeof(MENU_CONTROL_LINES[0])))

/* The heading each page carries under the title. NULL on the root, which has
 * the game's own name there instead. */
static const char *menu_heading(const MenuCtx *c)
{
    switch (c->page) {
    case MENU_SLOTS:    return c->mode == SLOTS_NEW ? "replace which save"
                                                    : "load game";
    case MENU_CONFIRM:  return "replace this save";
    case MENU_SETTINGS: return "settings";
    case MENU_CONTROLS: return "controls";
    default:            return NULL;
    }
}

/* Placed by fb->w/fb->h, never LOGICAL_*: with no backbuffer we draw straight
 * into the window surface at native resolution, and a menu positioned by the
 * logical size would land in the top-left corner of a 1080p screen. Same rule
 * hud_draw and bigmap_draw follow, for the same reason. */
static void menu_draw(SDL_Surface *fb, const MenuCtx *c)
{
    Uint32 ink   = SDL_MapRGB(fb->format, 0x10, 0x12, 0x18);
    Uint32 warm  = SDL_MapRGB(fb->format, 0xf0, 0xd8, 0xb0);
    Uint32 pale  = SDL_MapRGB(fb->format, 0x9a, 0xa8, 0xb8);
    Uint32 dim   = SDL_MapRGB(fb->format, 0x78, 0x82, 0x90);
    Uint32 edge  = SDL_MapRGB(fb->format, 0x6a, 0x5c, 0x44);
    /* Darker than the HUD's dim, which only has to read as "not yet earned"
     * beside a lit one. Here it has to read as "you cannot pick this", against
     * rows that are themselves unlit until the caret reaches them - at the
     * HUD's value an unselected row and a disabled one were near enough
     * identical on screen to be worth nothing. */
    Uint32 off   = SDL_MapRGB(fb->format, 0x4c, 0x54, 0x60);
    MenuRow rows[MENU_ROWS_MAX];
    const char *head;
    int count, i, y, x, maxw = 0;

    if (c->page == MENU_NONE)
        return;

    count = menu_build(c, rows);
    fill_rect(fb, 0, 0, fb->w, fb->h, ink);

    /* The title only on the root page. On a subpage it would be a second thing
     * competing with the heading for the same glance, and the player already
     * knows what game they are in by then. */
    head = menu_heading(c);
    if (!head) {
        const char *t = "wayfarer";
        int tw = text_w_scaled(t, MENU_TITLE_SCALE);
        draw_text_scaled(fb, (fb->w - tw) / 2, fb->h / 8, t, warm,
                         MENU_TITLE_SCALE);
        fill_rect(fb, (fb->w - tw) / 2, fb->h / 8 + FONT_LINE_AT(MENU_TITLE_SCALE),
                  tw, 1, edge);
    } else {
        draw_text_scaled(fb, (fb->w - text_w_scaled(head, 2)) / 2, fb->h / 8,
                         head, warm, 2);
    }

    y = fb->h / 2 - (count * MENU_ROW_H) / 2;
    if (c->page == MENU_CONTROLS) {
        /* The list sits where the rows would, and `back` follows it. Every line
         * is drawn from the same left edge, so the key column and the action
         * column line up - the whole point of the padding inside the strings. */
        int lx = (fb->w - text_w(MENU_CONTROL_LINES[0])) / 2;
        int ly = fb->h / 3;
        for (i = 0; i < MENU_CONTROL_COUNT; i++) {
            draw_text(fb, lx, ly, MENU_CONTROL_LINES[i], pale);
            ly += FONT_LINE + 2;
        }
        y = ly + FONT_LINE;
    } else if (c->page == MENU_CONFIRM) {
        /* Name the save being thrown away, in its own words from the list, so
         * the answer is given to a specific thing rather than to the word
         * "yes". Above the rows, where the question belongs. */
        char what[MENU_TEXT_MAX];
        int  p = c->pending;
        if (p >= 0 && p < SAVE_SLOTS) {
            menu_slot_text(what, sizeof(what), p, &c->slots[p], 0);
            draw_text(fb, (fb->w - text_w(what)) / 2, fb->h / 3, what, pale);
            draw_text(fb, (fb->w - text_w("this cannot be undone.")) / 2,
                      fb->h / 3 + FONT_LINE * 2, "this cannot be undone.", dim);
        }
        /* The answers sit clear of the question rather than centred on the
         * screen: with only two rows the centred block rode up into the
         * warning, and a yes/no that touches the sentence it answers is a
         * yes/no somebody clicks without reading. */
        y = fb->h / 3 + FONT_LINE * 5;
    }

    /* The BLOCK is centred and the rows are left-aligned inside it, rather than
     * each row being centred on its own. Centring every row individually made
     * the settings page jitter: "fullscreen  off" and "window scale  2" are
     * different lengths, so their labels started at different x and the column
     * read as ragged even though each line was perfectly centred. */
    for (i = 0; i < count; i++) {
        int w = text_w(rows[i].text);
        if (w > maxw) maxw = w;
    }
    x = (fb->w - maxw) / 2;

    for (i = 0; i < count; i++) {
        Uint32 col = !rows[i].enabled ? off : (i == menu.sel ? warm : pale);
        draw_text(fb, x, y, rows[i].text, col);
        /* The caret sits outside the label rather than inside it, so the row
         * text does not shift by six pixels as the selection passes over. */
        if (i == menu.sel)
            draw_text(fb, x - FONT_ADV * 2, y, ">", warm);
        y += MENU_ROW_H;
    }

    /* The footer names the keys that work HERE. The map screen states how to
     * close itself for the same reason: a screen that gives no way out reads as
     * a hang, and this one is the first thing a new player ever sees.
     *
     * Names what escape does HERE, which is three different things across the
     * pages - and on the title screen it is nothing at all, so it goes
     * unmentioned rather than being promised and ignored. */
    {
        const char *f = MENU_FOOTERS[c->page != MENU_ROOT ? MFOOT_SUB
                                     : c->started         ? MFOOT_PAUSED
                                     : MFOOT_TITLE];
        draw_text(fb, (fb->w - text_w(f)) / 2, fb->h - FONT_LINE - 8, f, dim);
    }

    /* The toast, on the menu's own terms.
     *
     * The menu REPLACES the HUD, so a message raised while it is up - a save
     * that would not load, settings that would not write - was being posted to
     * a hud_draw that never runs, and vanished. Reusing hud.toast rather than
     * inventing a second message line keeps one place where the game says
     * something went wrong. It does not tick down here, because the menu pauses
     * the simulation: it stays as long as the player is still looking at the
     * screen it is about, and expires normally once play resumes. */
    if (hud.toast_left > 0)
        draw_text(fb, (fb->w - text_w(hud.toast)) / 2,
                  fb->h - FONT_LINE * 3 - 8, hud.toast, warm);
}

/* ---- The map screen -----------------------------------------------------
 *
 * What the map fragment buys, and the reason it is worth crossing a biome for:
 * the WHOLE layout, unfogged, with a dashed trail from where she stands to
 * every fragment and every soul she has not found yet.
 *
 * Deliberately not a fog lift. The two answer different questions - fog is
 * "what have I seen", the map is "what is there" - and merging them would
 * either hand her a lit world for a pickup or make the reward invisible on the
 * screen she opened to look at it. The minimap keeps its fog gating for the
 * same reason: it is the always-on navigation aid, and it is not the reward.
 *
 * 128 tiles at two pixels is 256 square, which does not leave room for a
 * caption above and below it on a 270-line screen - so the map sits against
 * the right edge and the legend takes the 200-odd pixels that leaves on the
 * left, rather than the map being shrunk to make room for its own caption.
 */
#define BM_SCALE      2
#define BM_W          (WORLD_W * BM_SCALE)
#define BM_H          (WORLD_H * BM_SCALE)
#define BM_TRAIL_STEP 3    /* a dot every N tiles, so a trail reads as dashed */
#define BM_REFRESH    15   /* frames between rebuilds while it is open */

/* Where the map panel's top-left corner lands on `fb`. A function because
 * --map-test has to look at particular pixels of it, and a test that restated
 * this arithmetic would keep passing while the panel moved out from under it.
 * Read from the surface rather than from LOGICAL_*: with no backbuffer we draw
 * at native resolution - the same reason hud_draw reads fb->w. */
static void bm_origin(const SDL_Surface *fb, int *ox, int *oy)
{
    *ox = fb->w - BM_W - 7;
    *oy = (fb->h - BM_H) / 2;
    if (*ox < 8) *ox = 8;
    if (*oy < 0) *oy = 0;
}

/* The two trail colours, in one place, because --map-test counts pixels of
 * exactly these to prove a trail reached the screen.
 *
 * Brighter than the first pass, which was picked against the DARK map of an
 * untouched biome and measured badly on a restored one: ground goes from
 * 0x3c444a to a full 0x6a9a70 as she brings an area back, and a muted gold
 * that read clearly on the first was nearly the same luminance as the second.
 * A map that gets harder to read the further you get is the wrong way round.
 * Kept a step below the endpoint markers, so a trail still reads as leading TO
 * the brighter thing at its end rather than competing with it. */
static Uint32 bm_trail_col(SDL_Surface *s, int is_soul)
{
    return is_soul ? SDL_MapRGB(s->format, 0x5f, 0xa6, 0xc2)
                   : SDL_MapRGB(s->format, 0xcc, 0x95, 0x2c);
}

/* Terrain only, and with NO fog term - that absence is the whole feature. Same
 * water-before-solid ordering as mm_col, and for the same reason: water IS
 * solid, so testing solid first would draw every pond as rock. */
static Uint32 bm_col(SDL_Surface *s, const Game *g, int tx, int ty)
{
    const World *w = &g->w;
    Uint8 reg = w->litreg[ty][tx];
    float r = (reg != REGION_NONE && reg < w->region_count)
              ? w->regions[reg].restoration : 0.0f;

    if (w->terr[ty][tx] == GT_WATER)
        return SDL_MapRGB(s->format, 0x2c, 0x4c, 0x74);
    if (w->solid[ty][tx])
        return SDL_MapRGB(s->format, 0x15, 0x15, 0x1d);
    /* Restoration still reads, so the map also shows how much of the biome she
     * has already brought back - the one progress channel it keeps. */
    return SDL_MapRGB(s->format, (Uint8)(0x3c + (0x6a - 0x3c) * r),
                                 (Uint8)(0x44 + (0x9a - 0x44) * r),
                                 (Uint8)(0x4a + (0x70 - 0x4a) * r));
}

/* Walk a BFS distance field back from `from` toward its source, dotting as it
 * goes. Reconstructed from the field rather than from a stored parent array:
 * the step to take is any neighbour one hop nearer, which the field already
 * says, and a parent array would be a second description of the same thing.
 *
 * Bounded by a guard count as well as by the distance, so a malformed field
 * cannot spin here - the render path has no business trusting an invariant it
 * could just check. */
static void bm_trail(SDL_Surface *s, const int *dist, int from, Uint32 col)
{
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    int cur = from, guard = WORLD_W * WORLD_H, step = 0;

    if (from < 0 || dist[from] < 0)
        return;
    while (dist[cur] > 0 && guard-- > 0) {
        int x = cur % WORLD_W, y = cur / WORLD_W, d, next = -1;
        for (d = 0; d < 4; d++) {
            int nx = x + dxs[d], ny = y + dys[d], ni;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
            ni = ny * WORLD_W + nx;
            if (dist[ni] == dist[cur] - 1) { next = ni; break; }
        }
        if (next < 0)
            return;
        cur = next;
        /* The last dot stops short of her own marker rather than under it. */
        if ((step++ % BM_TRAIL_STEP) == 0 && dist[cur] > 0)
            fill_rect(s, (cur % WORLD_W) * BM_SCALE, (cur / WORLD_W) * BM_SCALE,
                      BM_SCALE, BM_SCALE, col);
    }
}

/* Terrain and trails into the cache. The markers and her own position are NOT
 * cached - they are drawn live over the blit, so nothing on screen can be up to
 * BM_REFRESH frames stale about where something is. */
static void bm_redraw(const Game *g, Scratch *sc)
{
    int tx, ty, i;

    for (ty = 0; ty < WORLD_H; ty++)
        for (tx = 0; tx < WORLD_W; tx++)
            fill_rect(hud.bm, tx * BM_SCALE, ty * BM_SCALE, BM_SCALE, BM_SCALE,
                      bm_col(hud.bm, g, tx, ty));

    /* Routed through bfs_gated with the abilities she is HOLDING, so the map
     * can never draw a way through a gate she cannot pass. Something behind
     * such a gate gets no trail at all and is counted instead - see the legend
     * line. Drawing a route she cannot walk would be a worse failure than
     * drawing none: it would send her at a pond bank and blame her for it. */
    bfs_gated(&g->w, g->p.abilities,
              (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE),
              sc->dist, sc->queue);
    hud.bm_closed = 0;
    for (i = 0; i < ENTITY_COUNT; i++) {
        if (g->ents[i].tile < 0 || g->ents[i].restored) continue;
        if (sc->dist[g->ents[i].tile] < 0) { hud.bm_closed++; continue; }
        bm_trail(hud.bm, sc->dist, g->ents[i].tile,
                 bm_trail_col(hud.bm, g->ents[i].is_soul));
    }
    hud.bm_dirty = 0;
}

static void bm_marker(SDL_Surface *fb, int ox, int oy, int tile, Uint32 col, int size)
{
    int x = (tile % WORLD_W) * BM_SCALE, y = (tile / WORLD_W) * BM_SCALE;

    fill_rect(fb, ox + x - size / 2, oy + y - size / 2, size, size, col);
}

/* One legend row: a colour chip and its name, on the same baseline. */
static void bm_key(SDL_Surface *fb, int x, int y, Uint32 chip, const char *s,
                   Uint32 text)
{
    fill_rect(fb, x, y + 1, 5, 5, chip);
    draw_text(fb, x + 9, y, s, text);
}

/* Fills the whole surface, so it is a SCREEN rather than an overlay - which is
 * also why the tick loop holds her still while it is up: walking blind under an
 * opaque map is input she did not mean to give. */
static void bigmap_draw(SDL_Surface *fb, const Game *g, Scratch *sc)
{
    static const char *title[BIOME_COUNT] = {
        "map of the forest", "map of the underworld", "map of lumiara"
    };
    Uint32 ink   = SDL_MapRGB(fb->format, 0x10, 0x12, 0x18);
    Uint32 warm  = SDL_MapRGB(fb->format, 0xf0, 0xd8, 0xb0);
    Uint32 pale  = SDL_MapRGB(fb->format, 0x9a, 0xa8, 0xb8);
    Uint32 edge  = SDL_MapRGB(fb->format, 0x6a, 0x5c, 0x44);
    Uint32 gold  = SDL_MapRGB(fb->format, 0xff, 0xd7, 0x6a);
    Uint32 soulc = SDL_MapRGB(fb->format, 0x9a, 0xd8, 0xe8);
    Uint32 viol  = SDL_MapRGB(fb->format, 0xb0, 0x6a, 0xff);
    Uint32 white = SDL_MapRGB(fb->format, 0xff, 0xff, 0xff);
    int ox, oy, lx = 8, ly, i;
    char buf[64];
    SDL_Rect dst;

    if (!hud.bm)
        hud.bm = SDL_CreateRGBSurface(0, BM_W, BM_H, fb->format->BitsPerPixel,
                                      fb->format->Rmask, fb->format->Gmask,
                                      fb->format->Bmask, fb->format->Amask);
    if (!hud.bm)
        return;   /* a missing map screen is a degraded HUD, not a fatal error */
    if (hud.bm_dirty || (hud.bm_tick++ % BM_REFRESH) == 0)
        bm_redraw(g, sc);

    bm_origin(fb, &ox, &oy);

    fill_rect(fb, 0, 0, fb->w, fb->h, ink);
    fill_rect(fb, ox - 1, oy - 1, BM_W + 2, 1, edge);
    fill_rect(fb, ox - 1, oy + BM_H, BM_W + 2, 1, edge);
    fill_rect(fb, ox - 1, oy, 1, BM_H, edge);
    fill_rect(fb, ox + BM_W, oy, 1, BM_H, edge);
    dst.x = ox; dst.y = oy; dst.w = 0; dst.h = 0;
    SDL_BlitSurface(hud.bm, NULL, fb, &dst);

    /* Endpoints over the trails, so a dot chain always terminates in the thing
     * it leads to. Unrestored only: a trail to something already remembered
     * would be a route to nothing. */
    for (i = 0; i < ENTITY_COUNT; i++) {
        if (g->ents[i].tile < 0 || g->ents[i].restored) continue;
        bm_marker(fb, ox, oy, g->ents[i].tile,
                  g->ents[i].is_soul ? soulc : gold, 3);
    }
    if ((g->area == 1 || g->area == 2) && g->w.portal_tile >= 0)
        bm_marker(fb, ox, oy, g->w.portal_tile, viol, 3);
    bm_marker(fb, ox, oy,
              (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE), white, 3);

    ly = oy;
    draw_text(fb, lx, ly, title[g->w.biome < BIOME_COUNT ? g->w.biome : 0], warm);
    ly += FONT_LINE * 2;
    SDL_snprintf(buf, sizeof(buf), "fragments %d/%d", g->frags_restored, FRAGMENT_COUNT);
    draw_text(fb, lx, ly, buf, warm);
    ly += FONT_LINE;
    SDL_snprintf(buf, sizeof(buf), "souls %d/%d", g->souls_restored, SOUL_COUNT);
    draw_text(fb, lx, ly, buf, warm);
    ly += FONT_LINE * 2;
    bm_key(fb, lx, ly, gold,  "memory fragment", pale); ly += FONT_LINE;
    bm_key(fb, lx, ly, soulc, "found soul", pale);      ly += FONT_LINE;
    if (g->area == 1 || g->area == 2) {
        bm_key(fb, lx, ly, viol, "the way onward", pale);
        ly += FONT_LINE;
    }
    bm_key(fb, lx, ly, white, "you are here", pale);
    ly += FONT_LINE * 2;
    draw_text(fb, lx, ly, "trails lead to what is", pale);       ly += FONT_LINE;
    draw_text(fb, lx, ly, "still missing.", pale);               ly += FONT_LINE;
    /* Stated rather than left as a silence: a collectible with no trail and no
     * explanation reads as the map being broken, not as a gate she has yet to
     * open. The count is what bm_redraw actually skipped, so the sentence
     * cannot outlive the condition that produced it. */
    if (hud.bm_closed > 0) {
        SDL_snprintf(buf, sizeof(buf), "%d lie beyond what you", hud.bm_closed);
        ly += FONT_LINE;
        draw_text(fb, lx, ly, buf, pale);              ly += FONT_LINE;
        draw_text(fb, lx, ly, "can cross for now.", pale);
        ly += FONT_LINE;
    }
    ly += FONT_LINE;
    draw_text(fb, lx, ly, "m   close the map", warm);
}

/* The one interaction key, in one function, so the tests drive exactly what E
 * runs. Returns the entity restored, or -1. */
static int try_interact(Game *g, Audio *a)
{
    int i = entity_in_reach(&g->w, g->ents, g->p.x, g->p.y);
    char buf[64];

    if (i < 0)
        return -1;
    game_restore(g, i);
    hud.mm_dirty = 1;
    if (g->ents[i].is_soul) {
        SDL_AtomicAdd(&a->voice_fire, 1);
        sfx_fire(a, SFX_SOUL);
        SDL_snprintf(buf, sizeof(buf), "a soul is remembered  %d/%d",
                     g->souls_restored, SOUL_COUNT);
    } else {
        SDL_AtomicAdd(&a->layer_fire, 1);
        sfx_fire(a, SFX_CHIME);
        if (g->ents[i].grants == ABIL_WADE)        SDL_snprintf(buf, sizeof(buf), "you remember wading");
        else if (g->ents[i].grants == ABIL_CLIMB)  SDL_snprintf(buf, sizeof(buf), "you remember climbing");
        else if (g->ents[i].grants == ABIL_KINDLE) SDL_snprintf(buf, sizeof(buf), "you remember the light");
        else SDL_snprintf(buf, sizeof(buf), "a fragment returns  %d/%d",
                          g->frags_restored, FRAGMENT_COUNT);
    }
    hud_toast(buf);
    return i;
}

/* The map fragment, on the same key and checked AFTER try_interact, so a mote
 * and a chart lying in the same reach resolve the way the prompt drew them.
 * Returns 1 if it was taken.
 *
 * Deliberately does NOT fire layer_fire: that counter is what the music reads
 * as "another memory has come back", and a map is not a memory. It gets the
 * pickup chime and nothing else, so the score cannot advance a layer for a
 * thing that restored no part of the world. */
static int try_take_map(Game *g, Audio *a)
{
    if (!map_in_reach(&g->w, g->p.x, g->p.y))
        return 0;
    g->w.map_tile = -1;
    g->maps |= (Uint8)(1u << (g->area - 1));
    hud.bm_dirty = 1;
    sfx_fire(a, SFX_CHIME);
    hud_toast("a map of this place  press m");
    return 1;
}

/* Step to another world without leaving the session, for '[' and ']'.
 *
 * Everything a fresh start touches has to be reset here or it survives into a
 * world it does not describe, and each one fails quietly rather than loudly:
 * the minimap cache is a whole SURFACE that would keep drawing the old map, and
 * win_shown is a latch that would suppress the completion banner in the new
 * world because it had already fired in the old one.
 *
 * Delegates to game_init rather than reproducing it - a seed reaches a playable
 * state through exactly one path, which is the same rule that keeps a loaded
 * world from drifting from a generated one.
 *
 * Payload before flag, as everywhere the audio callback is involved: it owns
 * audio.rng and the synth and it is running right now. Restore counts go to
 * zero because a new world starts with nothing remembered, so the music drops
 * back to its opening layer instead of carrying the old world's progress. */
/* The payload-then-flag handoff to the audio callback, factored out because
 * three independent call sites (reseed, F9 load, and the portal transition)
 * all need to agree on rng seed, restore counts AND now which biome's
 * LAYER_CFG_TABLE row to play - three copies of this would only need one of
 * them to forget the new area payload for stale music to keep playing over
 * the wrong biome. Payload before flag, as everywhere the callback is
 * involved: it owns audio.rng and the synth and is running right now. */
static void audio_request_reset(Audio *a, Uint64 seed, int frags, int souls, int area)
{
    a->rng_seed_req = seed;
    a->reset_frags  = frags;
    a->reset_souls  = souls;
    a->reset_area   = area;
    SDL_AtomicSet(&a->rng_req, 1);
    SDL_AtomicSet(&a->reset_req, 1);
}

static void game_reseed(Game *g, Scratch *sc, Audio *a, Uint64 seed)
{
    game_init(g, sc, seed);
    hud.mm_dirty  = 1;
    hud.win_shown = 0;
    hud.win_left  = 0;
    /* Both, not just the flag: the cached map SURFACE would keep drawing the
     * old world's layout, which is the same failure the minimap cache has and
     * the same fix. Closed as well as dirtied - the new world's map has not
     * been found, so there is nothing to be looking at. */
    hud.map_open  = 0;
    hud.bm_dirty  = 1;
    audio_request_reset(a, seed, 0, 0, BIOME_FOREST);
}

/* Step through a portal: fold the area being LEFT's live ents[] into the
 * persistent restored mask at its own bit offset, then generate the next
 * area the exact same way game_init generates Area 1 - see game_init_area.
 * Generalized over which area is being left (g->area), not hardcoded to
 * "area 1's bits" - with three areas, "keep the other area, overwrite mine"
 * has no single "the other area" any more, and would corrupt whichever area
 * is neither the one being left nor the one being entered. Same class of fix
 * as game_save's shift/keep_mask above, same reason.
 *
 * One-way by design (no return portal from a later area to an earlier one):
 * the design is symmetric enough that a return trip would be nearly free to
 * add later (same function, an earlier area argument), but it is a second
 * interactive object, HUD affordance and test surface the source plan does
 * not ask for. */
static void game_transition_to_area(Game *g, Scratch *sc, Uint8 next_area)
{
    int i;
    Uint32 shift = (g->area == 1) ? 0 : (g->area == 2) ? SAVE_AREA2_SHIFT : SAVE_AREA3_SHIFT;
    Uint32 live_bits = 0;

    for (i = 0; i < ENTITY_COUNT; i++)
        if (g->ents[i].restored)
            live_bits |= 1u << i;
    g->restored = (g->restored & ~(SAVE_AREA1_BITS << shift)) | (live_bits << shift);
    game_init_area(g, sc, g->seed, next_area);
    hud.mm_dirty  = 1;
    hud.win_shown = 0;
    hud.win_left  = 0;
    /* g->maps is untouched - the Forest map stays found - but the new area's
     * own map has not been, and the cache still holds the old biome. */
    hud.map_open  = 0;
    hud.bm_dirty  = 1;
}

/* The portal, checked alongside try_interact on the same key: only once the
 * current area is area_complete (the portal "lights up" - see hud_draw's
 * banner) and only within the same INTERACT_RADIUS entities use. Area 3 has
 * no portal of its own to use (it is terminal - see try_interact's caller),
 * so only areas 1 and 2 reach this. Returns 1 if the step was taken, 0
 * otherwise, mirroring try_interact's shape. */
static int try_use_portal(Game *g, Scratch *sc, Audio *a)
{
    float ex, ey, dx, dy;

    if ((g->area != 1 && g->area != 2) || g->w.portal_tile < 0 || !area_complete(g))
        return 0;
    ex = (float)(g->w.portal_tile % WORLD_W) * TILE + TILE * 0.5f;
    ey = (float)(g->w.portal_tile / WORLD_W) * TILE + TILE * 0.5f;
    dx = ex - g->p.x;
    dy = ey - g->p.y;
    if (dx * dx + dy * dy > INTERACT_RADIUS * INTERACT_RADIUS)
        return 0;
    if (g->area == 1) {
        game_transition_to_area(g, sc, 2);
        /* Root seed, not the Area 2 world-generation salt: F9's own reload of
         * an Area 2 save reseeds audio from the FILE's root seed the same
         * way, so this stays the one seed audio ever sees, whichever area is
         * active. */
        audio_request_reset(a, g->seed, 0, 0, BIOME_UNDERWORLD);
        hud_toast("the underworld opens");
    } else {
        game_transition_to_area(g, sc, 3);
        audio_request_reset(a, g->seed, 0, 0, BIOME_LUMIARA);
        hud_toast("the lumiara opens");
    }
    return 1;
}

/* What a refusal says, as a pure function of the ability bits, so the sentence
 * and the condition that produces it cannot drift apart - --move-test asserts
 * the mapping directly rather than restating it. Ordered wade, climb, kindle so
 * a tile refused on two counts names the one she is likelier to open first, and
 * so a pond that also touches the map edge reads as water rather than as cliff.
 * NULL for "nothing to say", which is the only case that draws no toast. */
static const char *refusal_text(Uint8 missing)
{
    if (!missing)                  return NULL;
    if (missing & ABIL_WADE)       return "the water is too deep";
    if (missing & ABIL_CLIMB)      return "too steep to climb";
    return "too dark to enter";
}

/* Say it, once per bump rather than once per tick - see the call site's
 * rate limit. */
static void gate_report(Uint8 missing, Audio *a)
{
    const char *s = refusal_text(missing);

    if (!s)
        return;
    sfx_fire(a, SFX_DENY);
    hud_toast(s);
}

#if WAYFARER_SELFTEST
/* ======================================================================
 * Verification scaffolding. Compiled out of the shipping build entirely -
 * this is a different binary, not a runtime flag, so "no debug code in the
 * submission" is structural rather than remembered.
 *
 * Every test carries a NEGATIVE CONTROL: a deliberately broken case the check
 * must reject. A checker that has never rejected anything proves nothing.
 * ====================================================================== */

/* A framebuffer with no window. Surface creation needs no video subsystem, and
 * a real format is required because the fade path reads channel masks. */
static SDL_Surface *test_surface(int w, int h)
{
    return SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
}

/* --lit reveals the whole map so the minimap (and every marker gated on
 * reveal - fragments, souls, the portal) can be looked at without exploring
 * first. world_gen rebuilds World.reveal from nothing every time a world is
 * (re)generated - a fresh game, a reseed, a portal transition, a load - so a
 * one-shot reveal applied only before the main loop starts goes dark again
 * the moment any of those happen. Factored out so every such call site can
 * re-apply it, rather than only the first world ever being lit. */
static void reveal_all(World *w)
{
    int lx, ly;
    for (ly = 0; ly < WORLD_H; ly++)
        for (lx = 0; lx < WORLD_W; lx++)
            w->reveal[ly][lx] = 255;
}

/* Decode a sprite and count how many of its pixels are palette index 0, i.e.
 * transparent. Used to prove a base-fill tile really covers its whole cell. */
static int sprite_transparent_px(const ArtSprite *sp)
{
    unsigned int i = sp->data_off, n = sp->data_off + sp->data_len;
    int seen = 0, trans = 0;

    if (n > ART_DATA_BYTES) return -1;
    while (i < n && seen < (int)sp->w * (int)sp->h) {
        unsigned int c = ART_DATA[i++], count, k;
        if (c >= 0x80) {
            count = (c & 0x7Fu) + 1u;
            if (i + count > n) break;
            for (k = 0; k < count && seen < (int)sp->w * (int)sp->h; k++, seen++)
                if (ART_DATA[i + k] == 0) trans++;
            i += count;
        } else {
            count = c + 1u;
            if (i >= n) break;
            for (k = 0; k < count && seen < (int)sp->w * (int)sp->h; k++, seen++)
                if (ART_DATA[i] == 0) trans++;
            i++;
        }
    }
    return trans;
}


/* ---- Authored ground colours, and counting them -------------------------
 *
 * The Fantasy Forest sheets say what ground a piece of art was drawn ON in the
 * only way a flat-shaded pixel sheet can: the ground colour is literally in the
 * sprite, around its base or in the corners the crop did not remove. That makes
 * "is this the right art for this ground" a MEASURABLE question rather than an
 * aesthetic one, which is the entire reason these tables exist.
 *
 * Taken from Tileset.png's own base fills, not from a palette picker: the three
 * greens are the bright grass, dark grass and dark olive of tile_grass_base and
 * tile_olive_edge, and the two browns are tile_dirt_fill's two shades.
 *
 * The stone list is deliberately SHORT of the full rock palette. 175,144,116 is
 * the lightest stone highlight AND the lily flower's petal highlight - three
 * pixels of it sit in ART_TILE_C7_R13, which is open water - so including it
 * would make a lily read as a boulder. The four below appear in rock art and
 * nowhere else in this sheet, measured. */
typedef struct { unsigned char r, g, b; } ArtRGB;

static const ArtRGB forest_green_rgb[3] = {
    { 163, 179,  21 }, { 112, 128,  26 }, { 75, 67, 23 }
};
static const ArtRGB forest_brown_rgb[2] = {
    { 164,  97,  43 }, { 122,  63,  32 }
};
static const ArtRGB forest_stone_rgb[4] = {
    {  76,  48,  52 }, { 125,  85,  85 }, { 49, 19, 33 }, { 69, 77, 89 }
};
/* The PALE warm highlight of the same stone, split out because it is what
 * separates the outcrop's sunlit body from a cliff face in shadow. The
 * reference render's map edge is almost entirely free of these two, and the
 * band that replaced it was full of them - so this is the measurable form of
 * "the wrong rock asset is on the border". */
static const ArtRGB forest_palestone_rgb[2] = {
    { 125,  85,  85 }, { 175, 144, 116 }
};

/* Decode a sprite and count how many pixels carry one of the listed colours.
 *
 * Resolves colour -> palette index once per call and then compares indices, so
 * the inner loop stays a table lookup. A colour the global palette does not
 * contain matches nothing, which is the honest answer: no sprite can be drawing
 * it. Palette entry k is sprite index k+1 - index 0 is transparent and is not
 * stored (see fogpal_build, which makes the same +1 step). */
static int sprite_colour_px(const ArtSprite *sp, const ArtRGB *cols, int n)
{
    unsigned char want[256];
    unsigned int i = sp->data_off, end = sp->data_off + sp->data_len;
    int total = (int)sp->w * (int)sp->h;
    int seen = 0, hits = 0, k;

    SDL_memset(want, 0, sizeof want);
    for (k = 0; k < n; k++) {
        int p;
        for (p = 0; p < ART_PAL_N && p < 255; p++) {
            if (ART_PAL[p * 3] == cols[k].r && ART_PAL[p * 3 + 1] == cols[k].g &&
                ART_PAL[p * 3 + 2] == cols[k].b) {
                want[p + 1] = 1;
                break;
            }
        }
    }
    if (end > ART_DATA_BYTES) return -1;
    while (i < end && seen < total) {
        unsigned int c = ART_DATA[i++], count, j;
        if (c >= 0x80) {
            count = (c & 0x7Fu) + 1u;
            if (i + count > end) break;
            for (j = 0; j < count && seen < total; j++, seen++)
                if (want[ART_DATA[i + j]]) hits++;
            i += count;
        } else {
            count = c + 1u;
            if (i >= end) break;
            for (j = 0; j < count && seen < total; j++, seen++)
                if (want[ART_DATA[i]]) hits++;
            i++;
        }
    }
    return hits;
}

/* Strict validator, self-test only. Where the shipping decoder CLAMPS so that
 * corrupt data degrades into a visible hole, this REJECTS, and demands exact
 * agreement: a stream must decode to precisely w*h pixels and consume precisely
 * its declared slice. ART_DATA is const and compiled in, so it cannot rot
 * between builds - build time is the moment to check it, and validating every
 * sprite every frame would spend real time re-proving something already known. */
static int art_stream_ok_sp(const ArtSprite *sp)
{
    unsigned int i = sp->data_off, n = sp->data_off + sp->data_len;
    long total = 0;

    if (sp->w == 0 || sp->h == 0)
        return 0;
    if (n > ART_DATA_BYTES || n < sp->data_off)
        return 0;
    while (i < n) {
        unsigned int c = ART_DATA[i++];
        unsigned int count, k;
        if (c >= 0x80) {
            count = (c & 0x7Fu) + 1u;
            if (i + count > n)
                return 0;
            for (k = 0; k < count; k++)
                if (ART_DATA[i + k] > ART_PAL_N)
                    return 0;
            i += count;
        } else {
            count = c + 1u;
            if (i >= n)
                return 0;
            if (ART_DATA[i] > ART_PAL_N)
                return 0;
            i++;
        }
        total += (long)count;
        if (total > (long)sp->w * sp->h)
            return 0;
    }
    return total == (long)sp->w * sp->h && i == n;
}

/* An INDEPENDENT encoder/decoder pair, written to the format spec rather than
 * by copying the bake or the shipping decoder. A round-trip against the same
 * code that produced the data proves only that it is self-consistent. */
static int art_ref_encode(const unsigned char *idx, int n, unsigned char *out, int cap)
{
    int i = 0, w = 0;

    while (i < n) {
        int run = 1;
        while (i + run < n && idx[i + run] == idx[i] && run < 128) run++;
        if (run >= 2) {
            if (w + 2 > cap) return -1;
            out[w++] = (unsigned char)(run - 1);
            out[w++] = idx[i];
            i += run;
        } else {
            int lit = 0, j = i;
            while (j < n && lit < 128) {
                int r2 = 1;
                while (j + r2 < n && idx[j + r2] == idx[j] && r2 < 128) r2++;
                if (r2 >= 2) break;
                lit++; j++;
            }
            if (w + 1 + lit > cap) return -1;
            out[w++] = (unsigned char)(0x80 | (lit - 1));
            for (j = 0; j < lit; j++) out[w++] = idx[i + j];
            i += lit;
        }
    }
    return w;
}

static int art_ref_decode(const unsigned char *rle, int n, unsigned char *out, int cap)
{
    int i = 0, w = 0;

    while (i < n) {
        unsigned int c = rle[i++];
        unsigned int count, k;
        if (c >= 0x80) {
            count = (c & 0x7Fu) + 1u;
            if (i + (int)count > n) return -1;
            for (k = 0; k < count; k++) {
                if (w >= cap) return -1;
                out[w++] = rle[i + k];
            }
            i += (int)count;
        } else {
            count = c + 1u;
            if (i >= n) return -1;
            for (k = 0; k < count; k++) {
                if (w >= cap) return -1;
                out[w++] = rle[i];
            }
            i++;
        }
    }
    return w;
}

#define REF_N 700
static int sprite_selftest(void)
{
    int fails = 0, i, n, enc, dec;
    static unsigned char pat[REF_N], rle[REF_N * 2 + 16], back[REF_N + 8];
    long px_total = 0, rle_total = 0;

    /* (a) Round-trip an awkward pattern: a 300-run that must split at 128, a
     * stretch of alternating singles that must batch into literals, an
     * exactly-128 run, and a lone final pixel. */
    n = 0;
    for (i = 0; i < 300; i++) pat[n++] = 7;
    for (i = 0; i < 120; i++) pat[n++] = (unsigned char)(1 + (i & 1));
    for (i = 0; i < 128; i++) pat[n++] = 3;
    for (i = 0; i < 151; i++) pat[n++] = (unsigned char)(1 + (i % 5));
    pat[n++] = 9;
    enc = art_ref_encode(pat, n, rle, (int)sizeof rle);
    dec = art_ref_decode(rle, enc, back, (int)sizeof back);
    if (enc < 0 || dec != n) {
        printf("  round-trip: encoded %d bytes, decoded %d px, expected %d\n", enc, dec, n);
        fails++;
    } else {
        for (i = 0; i < n; i++) {
            if (back[i] != pat[i]) {
                printf("  round-trip: px %d is %d, expected %d\n", i, back[i], pat[i]);
                fails++;
                break;
            }
        }
    }
    /* Negative control: a run whose count overshoots the buffer must be
     * rejected, not silently truncated. */
    {
        unsigned char bad[4];
        bad[0] = 200 - 128; /* a RUN of 73 */
        bad[1] = 5;
        if (art_ref_decode(bad, 2, back, 4) != -1) {
            printf("  round-trip negative control FAILED: overlong run accepted\n");
            fails++;
        }
    }

    /* (b) Every baked sprite must decode to exactly w*h px with in-range
     * indices and consume exactly its slice. */
    for (i = 0; i < ART_SPRITE_COUNT; i++) {
        if (!art_stream_ok_sp(&ART_SPRITES[i])) {
            printf("  sprite %d: malformed stream (%dx%d off %u len %u)\n", i,
                   ART_SPRITES[i].w, ART_SPRITES[i].h,
                   ART_SPRITES[i].data_off, ART_SPRITES[i].data_len);
            fails++;
        }
        px_total  += (long)ART_SPRITES[i].w * ART_SPRITES[i].h;
        rle_total += (long)ART_SPRITES[i].data_len;
    }
    /* Negative control: a record that lies about its width must be rejected. */
    {
        ArtSprite lie = ART_SPRITES[0];
        lie.w = (unsigned short)(lie.w + 1);
        if (art_stream_ok_sp(&lie)) {
            printf("  validator negative control FAILED: lying width accepted\n");
            fails++;
        }
        lie = ART_SPRITES[0];
        lie.data_len = ART_DATA_BYTES;   /* runs past its slice */
        if (art_stream_ok_sp(&lie)) {
            printf("  validator negative control FAILED: overlong slice accepted\n");
            fails++;
        }
    }

    /* (c) Palette bookkeeping. */
    if (ART_PAL_BYTES != ART_PAL_N * 3) {
        printf("  palette: ART_PAL_BYTES %d != ART_PAL_N*3 %d\n", ART_PAL_BYTES, ART_PAL_N * 3);
        fails++;
    }

    /* (d) The two anchor conventions.
     *
     * Tiles must stay 16x16 top-left anchored, or their grid position stops
     * meaning anything. Decorations must be bottom-centre: the ground-contact
     * point. Character frames must be CELL-RELATIVE, and the assertion below is
     * the interesting one - it is the positive signature of that convention
     * rather than a restatement of it.
     *
     * Under cell-relative anchoring, (anchor_y - h) is the gap from a frame's
     * lowest drawn pixel to the shared ground line, so it VARIES between frames
     * whose feet sit at different heights. Under a per-frame bottom-centre
     * anchor it would be identically 0 for all 64. So requiring more than one
     * distinct value is exactly the check that fails if someone "simplifies"
     * the bake to one anchor rule - which is the mistake that would make the
     * walk cycle skate, and which is invisible in a still frame. */
    {
        int tiles = 0, decs = 0, chars = 0;
        int gap_seen[64], gaps = 0;
        for (i = 0; i < ART_SPRITE_COUNT; i++) {
            const ArtSprite *sp = &ART_SPRITES[i];
            if (sp->w == ART_TILE_PX && sp->h == ART_TILE_PX &&
                sp->anchor_x == 0 && sp->anchor_y == 0) {
                tiles++;
                continue;
            }
            if (i >= ART_CH_IDLE_DOWN_0) {
                int gap = (int)sp->anchor_y - (int)sp->h;
                int j, found = 0;
                chars++;
                if (sp->anchor_x > ART_CHAR_CX || sp->anchor_y > ART_CHAR_FOOT) {
                    printf("  sprite %d: character anchor (%d,%d) outside its cell\n",
                           i, sp->anchor_x, sp->anchor_y);
                    fails++;
                }
                if (gap < 0) {
                    printf("  sprite %d: character foot below the ground line\n", i);
                    fails++;
                }
                for (j = 0; j < gaps; j++) if (gap_seen[j] == gap) { found = 1; break; }
                if (!found && gaps < 64) gap_seen[gaps++] = gap;
                continue;
            }
            if (sp->anchor_x != sp->w / 2 || sp->anchor_y != sp->h) {
                printf("  sprite %d: decoration anchor (%d,%d), expected (%d,%d)\n",
                       i, sp->anchor_x, sp->anchor_y, sp->w / 2, sp->h);
                fails++;
            }
            decs++;
        }
        if (chars != 64) {
            printf("  expected 64 character frames, counted %d\n", chars);
            fails++;
        }
        if (gaps < 2) {
            printf("  character anchors: (anchor_y - h) takes only %d distinct value(s)"
                   " - anchors look bottom-centre, not cell-relative\n", gaps);
            fails++;
        }
        printf("sprite  : %d records = %d tiles + %d decorations + %d char frames\n",
               ART_SPRITE_COUNT, tiles, decs, chars);
        printf("          %ld px from %ld RLE bytes (%.2fx), %d palette colours\n",
               px_total, rle_total, (double)px_total / (double)rle_total, ART_PAL_N);
        printf("          char anchor gap takes %d distinct values (cell-relative: >1)\n", gaps);
    }

    /* PROP DENSITY NORMALISATION.
     *
     * The rates in prop_at are Forest's, and only stay correct for another
     * biome if that biome's art is a comparable size - which is exactly what
     * stopped being true when Lumiara put chests and lantern posts in the
     * pebble and grass-tuft slots. See prop_keep_build. */
    {
        static const char *pname[PROP_COUNT] = {
            "none", "tree", "pine", "bush", "log", "rock",
            "stone", "mushroom", "tuft", "reed"
        };
        int k, b, thinned = 0, ref, box;

        prop_keep_build();

        /* Forest is the reference, so it must come back completely untouched -
         * if it ever thins, --mockup-test's Forest-calibrated pixel census is
         * silently measuring a different world than the one it was tuned on. */
        for (k = 0; k < PROP_COUNT; k++) {
            if (prop_art[BIOME_FOREST][k].n <= 0) continue;
            if (prop_keep[BIOME_FOREST][k] != PROP_ROLL_N) {
                printf("  prop density: Forest slot %s keeps %d of %d -"
                       " the reference biome must never be thinned\n",
                       pname[k], prop_keep[BIOME_FOREST][k], PROP_ROLL_N);
                fails++;
            }
        }
        for (b = 0; b < BIOME_COUNT; b++)
            for (k = 0; k < PROP_COUNT; k++)
                if (prop_art[b][k].n > 0 && prop_keep[b][k] < PROP_ROLL_N)
                    thinned++;

        /* NEGATIVE CONTROL: the rule has to DISCRIMINATE, not just thin
         * everything. Lumiara's PROP_STONE is the largest overshoot in the
         * build and must come back thinned; Forest's own PROP_STONE, measured
         * by the identical code path, must come back untouched. One of those
         * failing means the derivation is keyed on something other than
         * relative footprint. */
        ref = prop_art_box(&prop_art[BIOME_FOREST][PROP_STONE]);
        box = prop_art_box(&prop_art[BIOME_LUMIARA][PROP_STONE]);
        if (ref <= 0 || box <= ref) {
            printf("  prop density negative control FAILED: Lumiara PROP_STONE"
                   " (%d px^2) is not larger than Forest's (%d), so this check"
                   " cannot demonstrate thinning\n", box, ref);
            fails++;
        } else if (prop_keep[BIOME_LUMIARA][PROP_STONE] >= PROP_ROLL_N) {
            printf("  prop density: Lumiara PROP_STONE is %.1fx Forest's box"
                   " but was not thinned\n", (double)box / (double)ref);
            fails++;
        } else {
            printf("sprite  : prop density - Lumiara %s is %.1fx Forest's box"
                   " -> keeps %d of %d, while Forest keeps %d of %d\n",
                   pname[PROP_STONE], (double)box / (double)ref,
                   prop_keep[BIOME_LUMIARA][PROP_STONE], PROP_ROLL_N,
                   prop_keep[BIOME_FOREST][PROP_STONE], PROP_ROLL_N);
        }
        printf("sprite  : prop density - %d of %d populated slots thinned;"
               " keep/64 by biome:\n", thinned, BIOME_COUNT * (PROP_COUNT - 1));
        for (b = 0; b < BIOME_COUNT; b++) {
            printf("          %-10s",
                   b == BIOME_FOREST ? "forest" : b == BIOME_UNDERWORLD ? "underworld" : "lumiara");
            for (k = 1; k < PROP_COUNT; k++)
                printf(" %s %d", pname[k], prop_art[b][k].n > 0 ? prop_keep[b][k] : PROP_ROLL_N);
            printf("\n");
        }
    }

    printf("sprite  : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* The SHIPPING decoder against malformed records. The contract being checked is
 * containment: whatever the data says, the decoder must not write one pixel
 * outside the box the record declares. Corrupt art should draw a hole, not
 * scribble over the rest of the frame. */
static int decode_selftest(void)
{
    const Uint32 SENTINEL = 0x00ABCDEFu;
    SDL_Surface *fb = test_surface(128, 128);
    int fails = 0, case_i, x, y;
    ArtSprite bad[4];
    int nbad = 0;

    if (!fb) {
        printf("decode  : could not create a test surface: %s\n", SDL_GetError());
        return 1;
    }
    if (!fogpal && !fogpal_build(fb)) {
        printf("decode  : could not build the fog palette\n");
        SDL_FreeSurface(fb);
        return 1;
    }

    /* Slice runs past the end of ART_DATA. */
    bad[nbad] = ART_SPRITES[0];  bad[nbad].data_len = ART_DATA_BYTES; nbad++;
    /* Slice truncated to a single byte, so a RUN has no index to read. */
    bad[nbad] = ART_SPRITES[0];  bad[nbad].data_len = 1; nbad++;
    /* Window shifted by one byte, so payload bytes are read as control bytes -
     * which is what actually exercises the index clamp, since a shifted stream
     * reliably produces indices above the palette. */
    bad[nbad] = ART_SPRITES[1];  bad[nbad].data_off += 1; nbad++;
    /* Unsigned wraparound on data_off + data_len. */
    bad[nbad] = ART_SPRITES[0];  bad[nbad].data_off = 8; bad[nbad].data_len = 0xFFFFFFF0u; nbad++;

    for (case_i = 0; case_i < nbad; case_i++) {
        const ArtSprite *sp = &bad[case_i];
        int cx = 64, cy = 64;
        int bx0 = cx - (int)sp->anchor_x, by0 = cy - (int)sp->anchor_y;
        int bx1 = bx0 + (int)sp->w, by1 = by0 + (int)sp->h;
        int stray = 0;

        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                ((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] = SENTINEL;

        draw_sprite_sp(fb, sp, cx, cy, FOG_LEVELS - 1, 0, 0);

        for (y = 0; y < fb->h; y++) {
            for (x = 0; x < fb->w; x++) {
                Uint32 v = ((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x];
                int inside = (x >= bx0 && x < bx1 && y >= by0 && y < by1);
                if (!inside && v != SENTINEL)
                    stray++;
            }
        }
        if (stray) {
            printf("  malformed case %d wrote %d px outside its declared box\n", case_i, stray);
            fails++;
        }
        /* Negative control: the strict validator must REJECT every one of these,
         * or they are not actually malformed and this test proves nothing. */
        if (art_stream_ok_sp(sp)) {
            printf("  malformed case %d was accepted by the validator\n", case_i);
            fails++;
        }
    }

    /* Positive control: a decoder clamped into drawing nothing at all would pass
     * every check above. A valid sprite must still put pixels on the surface. */
    {
        int drawn = 0;
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                ((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] = SENTINEL;
        draw_sprite_sp(fb, &ART_SPRITES[ART_TREE_A], 64, 120, FOG_LEVELS - 1, 0, 0);
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                if (((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] != SENTINEL)
                    drawn++;
        if (drawn == 0) {
            printf("  positive control FAILED: a valid sprite drew nothing\n");
            fails++;
        } else {
            printf("decode  : 4 malformed streams contained; valid sprite drew %d px\n", drawn);
        }
    }

    SDL_FreeSurface(fb);
    printf("decode  : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* The fog LUT must agree with fog_lerp exactly - it exists only as a cache of
 * it - and the blend must preserve the VALUE HIERARCHY: if colour A is lighter
 * than B at full reveal, it must still be lighter at every fog level. That is
 * the property the whole "keep FOG_KEEP of the luminance contrast" design is
 * for, and without it props flatten into silhouettes. */
/* Collision and movement. */
static int move_selftest(int seeds, Uint64 base)
{
    World *w = (World *)SDL_malloc(sizeof(World));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    Entity ents[ENTITY_COUNT];
    int fails = 0, s;
    /* wall_refusal, measured over real generated forests - see the block after
     * this loop. Counted rather than spot-checked: a check that happened to
     * find no ponds would pass vacuously. */
    int wr_water = 0, wr_border = 0, wr_rock_quiet = 0, wr_open_quiet = 0;
    int wr_water_bad = 0, wr_border_bad = 0, wr_rock_bad = 0, wr_both = 0;

    if (!w || !sc) { printf("move    : out of memory\n"); SDL_free(w); SDL_free(sc); return 1; }

    for (s = 0; s < seeds; s++) {
        Uint64 seed = base + (Uint64)s;
        Player a, b;
        int i;

        world_gen(w, sc, ents, seed, BIOME_FOREST);
        if (w->spawn_tile < 0) continue;

        /* Every tile centre in the world, asked what the game would ask when she
         * steps onto it. The foot box at a tile centre lies wholly inside that
         * tile (proved above), so each answer is about ONE tile and nothing
         * else - which is what makes "a pond says water, a boulder says
         * nothing" a statement the sweep can actually check.
         *
         * The mutual-exclusion count is the load-bearing one: gate_refusal and
         * wall_refusal partition the blocked positions between them, and if
         * they ever both answered, one bump would fire two toasts. */
        {
            int tx, ty;
            for (ty = 0; ty < WORLD_H; ty++)
                for (tx = 0; tx < WORLD_W; tx++) {
                    float cx = (float)tx * TILE + TILE * 0.5f;
                    float cy = (float)ty * TILE + TILE * 0.5f;
                    Uint8 gate = gate_refusal(w, ABIL_NONE, cx, cy);
                    Uint8 wall = wall_refusal(w, cx, cy);
                    if (gate && wall) wr_both++;
                    if (!w->solid[ty][tx]) {
                        /* Open ground: a WALL has nothing to say here whatever
                         * the gates are doing. */
                        if (wall) wr_open_quiet--; else wr_open_quiet++;
                    } else if (world_border(tx, ty)) {
                        if (wall == ABIL_CLIMB) wr_border++; else wr_border_bad++;
                    } else if (w->terr[ty][tx] == GT_WATER) {
                        if (wall == ABIL_WADE) wr_water++; else wr_water_bad++;
                    } else {
                        /* An ordinary rock outcrop. Silence is the requirement,
                         * not an omission: it is the common wall, and a toast
                         * on every boulder would bury the two that matter. */
                        if (wall == 0) wr_rock_quiet++; else wr_rock_bad++;
                    }
                }
        }

        SDL_zero(a);
        a.x = (float)(w->spawn_tile % WORLD_W) * TILE + TILE * 0.5f;
        a.y = (float)(w->spawn_tile / WORLD_W) * TILE + TILE * 0.5f;
        a.abilities = ABIL_ALL;

        /* A player standing at spawn must not be inside a wall. */
        if (player_blocked(w, a.abilities, a.x, a.y)) {
            printf("  seed %.0f: spawn is inside a wall\n", (double)seed);
            fails++;
        }

        /* REGRESSION: standing on a tile centre, the foot box must lie inside a
         * single tile row and column. FOOT_H is exactly TILE/2, so the box's
         * top edge falls on a tile boundary there, and without the half-open
         * epsilon it spills into the row above - which froze the autopilot
         * against an open tile. Checked as geometry, not as a symptom. */
        {
            int cx = w->spawn_tile % WORLD_W, cy = w->spawn_tile / WORLD_W;
            float px = (float)cx * TILE + TILE * 0.5f;
            float py = (float)cy * TILE + TILE * 0.5f;
            int x0 = (int)SDL_floorf((px - FOOT_W * 0.5f + 0.001f) / TILE);
            int x1 = (int)SDL_floorf((px + FOOT_W * 0.5f - 0.001f) / TILE);
            int y0 = (int)SDL_floorf((py - FOOT_H * 0.5f + 0.001f) / TILE);
            int y1 = (int)SDL_floorf((py + FOOT_H * 0.5f - 0.001f) / TILE);
            if (x0 != cx || x1 != cx || y0 != cy || y1 != cy) {
                printf("  seed %.0f: foot box at a tile centre spans x%d..%d y%d..%d,"
                       " expected the single tile (%d,%d)\n",
                       (double)seed, x0, x1, y0, y1, cx, cy);
                fails++;
            }
        }

        /* Determinism: the same input from the same start lands in the same
         * place, bit for bit. Without this no seeded test of movement means
         * anything. */
        b = a;
        for (i = 0; i < 600; i++) {
            float ang = (float)i * 0.11f;
            move_axis(w, &a, SDL_sinf(ang) * PLAYER_SPEED * TICK_DT, 0.0f);
            move_axis(w, &a, 0.0f, SDL_cosf(ang) * PLAYER_SPEED * TICK_DT);
        }
        for (i = 0; i < 600; i++) {
            float ang = (float)i * 0.11f;
            move_axis(w, &b, SDL_sinf(ang) * PLAYER_SPEED * TICK_DT, 0.0f);
            move_axis(w, &b, 0.0f, SDL_cosf(ang) * PLAYER_SPEED * TICK_DT);
        }
        if (a.x != b.x || a.y != b.y) {
            printf("  seed %.0f: movement is not deterministic (%.4f,%.4f vs %.4f,%.4f)\n",
                   (double)seed, a.x, a.y, b.x, b.y);
            fails++;
        }
        /* And it must never have ended up inside a wall. */
        if (player_blocked(w, a.abilities, a.x, a.y)) {
            printf("  seed %.0f: walked into a wall\n", (double)seed);
            fails++;
        }

        /* No-drift: pushing hard into a wall and releasing must not accumulate
         * position. Walk west into whatever is there, then east the same
         * amount, from a spot with a wall to the west. */
        {
            Player c;
            SDL_zero(c);
            c.abilities = ABIL_ALL;
            c.x = a.x; c.y = a.y;
            for (i = 0; i < 200; i++) move_axis(w, &c, -PLAYER_SPEED * TICK_DT, 0.0f);
            if (player_blocked(w, c.abilities, c.x, c.y)) {
                printf("  seed %.0f: ended inside a wall after pushing west\n", (double)seed);
                fails++;
            }
        }
    }

    /* ---- wall_refusal: the two Forest walls that have something to say ----
     *
     * A pond and the map's border ring both stop her against something she can
     * see, and until now both were silent - gate_refusal speaks only for the
     * ability LOCKS and returns 0 for anything solid. */
    if (wr_water_bad || wr_border_bad || wr_rock_bad || wr_both || wr_open_quiet < 0) {
        printf("FAIL  move: wall_refusal wrong on %d water, %d border, %d rock"
               " tile(s); %d position(s) answered by BOTH gate and wall;"
               " open-ground tally %d\n",
               wr_water_bad, wr_border_bad, wr_rock_bad, wr_both, wr_open_quiet);
        fails++;
    } else if (wr_water == 0 || wr_border == 0 || wr_rock_quiet == 0) {
        /* A sweep that met no ponds, no border or no boulders proved nothing
         * about them. */
        printf("FAIL  move: wall_refusal never met one of its cases"
               " (water %d, border %d, rock %d) - the sweep proves nothing\n",
               wr_water, wr_border, wr_rock_quiet);
        fails++;
    } else {
        printf("move    : wall_refusal says water on %d pond tiles, climb on %d"
               " border tiles, nothing on %d outcrop tiles; gate and wall never"
               " both answered\n", wr_water, wr_border, wr_rock_quiet);
    }

    /* The sentence each refusal produces, asserted against refusal_text rather
     * than restated - the toast the player reads is the requirement, and it has
     * to be pinned to the bit that causes it. */
    {
        const char *tw = refusal_text(ABIL_WADE);
        const char *tc = refusal_text(ABIL_CLIMB);
        if (!tw || SDL_strcmp(tw, "the water is too deep") != 0 ||
            !tc || SDL_strcmp(tc, "too steep to climb") != 0 ||
            refusal_text(ABIL_NONE) != NULL) {
            printf("FAIL  move: refusal wording - wade says \"%s\", climb says"
                   " \"%s\", nothing says \"%s\"\n",
                   tw ? tw : "(null)", tc ? tc : "(null)",
                   refusal_text(ABIL_NONE) ? refusal_text(ABIL_NONE) : "(null)");
            fails++;
        } else {
            printf("move    : wade -> \"%s\", climb -> \"%s\", open -> silence\n", tw, tc);
        }
    }

    /* NEGATIVE CONTROLS, on a world built by hand so each case is certain
     * rather than hoped for: one pond tile, one border tile, one outcrop tile,
     * one open tile. Two things the checker above cannot see on its own:
     *
     *   (a) BIOME. The same pond in the Underworld must say nothing - the
     *       feature is Forest-only, and a check run only against Forest worlds
     *       would pass just as happily on an implementation that ignored biome.
     *   (b) The rock/water distinction is real. If wall_refusal answered from
     *       `solid` alone it would pass every water and border assertion above
     *       and fail here, on the boulder. */
    {
        static const struct { int tx, ty; Uint8 want; const char *what; } cases[] = {
            { 64, 64, ABIL_WADE,  "pond"   },
            {  0, 40, ABIL_CLIMB, "border" },
            { 30, 30, 0,          "outcrop"},
            { 50, 50, 0,          "grass"  }
        };
        int x, y, k, bad = 0, ctl_biome = 0, ctl_rock = 0;

        SDL_memset(w, 0, sizeof(World));
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++) {
                w->terr[y][x] = GT_GRASS;
                w->region[y][x] = w->litreg[y][x] = REGION_NONE;
            }
        for (x = 0; x < WORLD_W; x++) {
            w->solid[0][x] = w->solid[WORLD_H - 1][x] = 1;
            w->terr[0][x] = w->terr[WORLD_H - 1][x] = GT_ROCK;
        }
        for (y = 0; y < WORLD_H; y++) {
            w->solid[y][0] = w->solid[y][WORLD_W - 1] = 1;
            w->terr[y][0] = w->terr[y][WORLD_W - 1] = GT_ROCK;
        }
        for (y = 62; y <= 66; y++)                      /* a pond */
            for (x = 62; x <= 66; x++) { w->terr[y][x] = GT_WATER; w->solid[y][x] = 1; }
        for (y = 29; y <= 31; y++)                      /* an outcrop */
            for (x = 29; x <= 31; x++) { w->terr[y][x] = GT_ROCK; w->solid[y][x] = 1; }
        w->region_count = 0;
        w->spawn_region = -1;
        w->biome = BIOME_FOREST;

        for (k = 0; k < (int)(sizeof cases / sizeof cases[0]); k++) {
            float cx = (float)cases[k].tx * TILE + TILE * 0.5f;
            float cy = (float)cases[k].ty * TILE + TILE * 0.5f;
            Uint8 got = refusal_at(w, ABIL_NONE, cx, cy);
            if (got != cases[k].want) {
                printf("  wall control: %s tile gave 0x%02X, expected 0x%02X\n",
                       cases[k].what, got, cases[k].want);
                bad++;
            }
            if (k == 2 && got == 0) ctl_rock = 1;   /* the boulder stayed silent */
        }
        w->biome = BIOME_UNDERWORLD;
        if (wall_refusal(w, 64.0f * TILE + TILE * 0.5f, 64.0f * TILE + TILE * 0.5f) == 0 &&
            wall_refusal(w, TILE * 0.5f, 40.0f * TILE + TILE * 0.5f) == 0)
            ctl_biome = 1;
        w->biome = BIOME_FOREST;

        if (bad || !ctl_biome || !ctl_rock) {
            printf("FAIL  move: wall control - %d case(s) wrong; biome gate %s;"
                   " boulder-stays-quiet %s\n", bad,
                   ctl_biome ? "held" : "LEAKED", ctl_rock ? "held" : "BROKE");
            fails++;
        } else {
            printf("move    : wall controls - pond/border/outcrop/grass all as"
                   " specified, and the same pond is silent outside the Forest\n");
        }
    }

    /* Negative control: a solver that TELEPORTS instead of sweeping must be
     * caught tunnelling through a one-tile wall. Built as an explicit world so
     * the case is certain rather than hoped for. */
    {
        int x, y, tunnelled = 0;
        Player p;
        SDL_memset(w, 0, sizeof(World));
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                w->solid[y][x] = (Uint8)(x == 0 || y == 0 ||
                                         x == WORLD_W - 1 || y == WORLD_H - 1);
        for (y = 0; y < WORLD_H; y++) w->solid[y][20] = 1;   /* a one-tile wall */
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                w->region[y][x] = REGION_NONE;
        w->region_count = 0;
        w->spawn_region = -1;

        SDL_zero(p);
        p.abilities = ABIL_ALL;
        p.x = 10.0f * TILE; p.y = 10.0f * TILE;
        /* The real solver: sweeping, must stop at the wall. */
        for (x = 0; x < 100; x++) move_axis(w, &p, 40.0f, 0.0f);
        if (p.x > 20.0f * TILE) {
            printf("  move negative control FAILED: the sweeping solver tunnelled\n");
            fails++;
        }
        /* The broken solver: one big jump with a single end-point test. The
         * destination must be a real OPEN tile beyond the wall - aiming past
         * the edge of the map instead lands outside, where solid_at reports
         * wall, and the control silently proves nothing. */
        {
            Player q;
            SDL_zero(q);
            q.abilities = ABIL_ALL;
            q.x = 10.0f * TILE; q.y = 10.0f * TILE;
            if (!player_blocked(w, q.abilities, 40.0f * TILE, q.y))
                tunnelled = 1;   /* wall at tile 20; tile 40 is open */
        }
        if (!tunnelled) {
            printf("  move negative control FAILED: a teleporting solver was not"
                   " able to cross the wall, so this proves nothing\n");
            fails++;
        } else {
            printf("move    : negative control - a teleporting solver crosses a"
                   " 1-tile wall the sweeping one stops at\n");
        }
    }

    SDL_free(w);
    SDL_free(sc);
    /* FRAME PACING. The simulation is fixed-step and the renderer is not, and
     * nothing phase-locks them: there is no vsync here, and the frame nap
     * resolves to whole milliseconds against a 16.67 ms period. So ticks do not
     * land one per frame - the count alternates 1, 1, 2, 1, 0, ... as the two
     * rates drift past each other - and a frame drawn at the last TICK position
     * moves the world 0 px, then 2.4 px, then 1.2 px at walking speed. The
     * simulation is perfectly regular and the screen still stutters.
     *
     * Driven at a frame rate deliberately close to but not equal to the tick
     * rate, which is the worst case: near-equal rates beat slowly, so the
     * uneven frames arrive in long visible runs rather than as noise.
     *
     * The negative control is the OLD behaviour, measured in the same run: if
     * the raw per-frame advance does not vary here, this scenario never
     * produced an uneven tick and the check proves nothing. */
    {
        const float frame_dt = 1.0f / 59.7f;   /* not a multiple of TICK_DT */
        float acc = 0.0f, x = 0.0f, prev_x = 0.0f;
        float last_raw = 0.0f, last_drawn = 0.0f;
        float raw_lo = 1e9f, raw_hi = -1e9f, lerp_lo = 1e9f, lerp_hi = -1e9f;
        int f;

        for (f = 0; f < 400; f++) {
            float drawn;
            acc += frame_dt;
            while (acc >= TICK_DT) {
                prev_x = x;
                x += PLAYER_SPEED * TICK_DT;
                acc -= TICK_DT;
            }
            drawn = render_lerp(prev_x, x, acc / TICK_DT);
            if (f >= 20) {          /* skip the first frames, where acc is settling */
                float d_raw = x - last_raw, d_drawn = drawn - last_drawn;
                if (d_raw   < raw_lo)  raw_lo  = d_raw;
                if (d_raw   > raw_hi)  raw_hi  = d_raw;
                if (d_drawn < lerp_lo) lerp_lo = d_drawn;
                if (d_drawn > lerp_hi) lerp_hi = d_drawn;
            }
            last_raw = x;
            last_drawn = drawn;
        }

        if (lerp_hi - lerp_lo > 0.01f) {
            printf("  interpolated advance varies by %.4f px/frame (limit 0.01) -"
                   " the drawn position is not evenly paced\n", lerp_hi - lerp_lo);
            fails++;
        }
        /* And it must never go backwards, which would read as a jerk. */
        if (lerp_lo <= 0.0f) {
            printf("  interpolated advance reached %.4f px/frame - she stalls or"
                   " moves backwards between ticks\n", lerp_lo);
            fails++;
        }
        if (raw_hi - raw_lo < 1.0f) {
            printf("  pacing negative control FAILED: the un-interpolated advance"
                   " varied by only %.4f px/frame, so this scenario never produced"
                   " an uneven tick\n", raw_hi - raw_lo);
            fails++;
        } else {
            printf("move    : negative control - drawing at the tick position varies"
                   " %.2f-%.2f px/frame at %.1f fps\n",
                   raw_lo, raw_hi, 1.0f / frame_dt);
        }
        printf("move    : interpolated advance holds %.4f-%.4f px/frame\n",
               lerp_lo, lerp_hi);
    }

    printf("move    : %d seeds, determinism and wall containment hold\n", seeds);
    printf("move    : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* Regions, and the parity between the model and an actual walk. */

/* Crossings into a stricter gate that nothing visible accounts for. The bug, as
 * a number. See the call site in gating_selftest for what it means. */
static int gate_unexplained(const World *w)
{
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    int x, y, d, n = 0;

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            Uint8 a = w->region[y][x];
            int ox, oy, seen_solid = 0;
            if (w->solid[y][x] || a == REGION_NONE) continue;
            for (d = 0; d < 4; d++) {
                int nx = x + dx[d], ny = y + dy[d];
                if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
                if (w->solid[ny][nx]) continue;
                /* Stepping OUT of this tile into one that demands more. */
                if (region_requires(w, w->region[ny][nx]) & ~region_requires(w, a))
                    break;
            }
            if (d == 4) continue;
            for (oy = -GATE_LOOK; oy <= GATE_LOOK && !seen_solid; oy++)
                for (ox = -GATE_LOOK; ox <= GATE_LOOK; ox++)
                    if (solid_at(w, x + ox, y + oy)) { seen_solid = 1; break; }
            if (!seen_solid) n++;
        }
    }
    return n;
}

static int gating_selftest(int seeds, Uint64 base)
{
    static const Uint8 tiers[4] = {
        ABIL_NONE, ABIL_WADE, ABIL_WADE | ABIL_CLIMB, ABIL_ALL
    };
    World *w = (World *)SDL_malloc(sizeof(World));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    Entity ents[ENTITY_COUNT];
    int fails = 0, s, gated_seeds = 0, checked = 0;

    if (!w || !sc) { printf("gating  : out of memory\n"); SDL_free(w); SDL_free(sc); return 1; }

    for (s = 0; s < seeds; s++) {
        Uint64 seed = base + (Uint64)s;
        int t;

        world_gen(w, sc, ents, seed, BIOME_FOREST);
        if (w->region_count < 2) continue;
        checked++;

        for (t = 0; t < 4; t++) {
            Uint32 walked = walk_regions(w, tiers[t], w->spawn_tile, sc->seen, sc->queue);
            Uint32 modelled = regions_reachable(w, tiers[t]);
            if (walked != modelled) {
                printf("  seed %.0f tier %d: walk 0x%X != graph 0x%X\n",
                       (double)seed, t, (unsigned)walked, (unsigned)modelled);
                fails++;
            }
        }
        /* With nothing held, at least some seeds must actually be gated, or
         * gating is decorative and the parity above proves nothing. */
        if (regions_reachable(w, ABIL_NONE) != regions_reachable(w, ABIL_ALL))
            gated_seeds++;

        /* Every region must be internally connected: a region whose tiles are
         * in two pieces would make the adjacency graph a lie. Guaranteed by
         * lockstep BFS, so this is checking the construction, not hoping. */
        {
            int r;
            for (r = 0; r < w->region_count; r++) {
                int seedt = w->regions[r].seed_tile, i, count = 0;
                if (seedt < 0) continue;
                SDL_memset(sc->seen, 0, (size_t)WORLD_W * WORLD_H);
                {
                    int head = 0, tail = 0;
                    sc->seen[seedt] = 1;
                    sc->queue[tail++] = seedt;
                    while (head < tail) {
                        int nb[4], n, k, idx = sc->queue[head++];
                        count++;
                        n = tile_neighbours(w, idx, nb);
                        for (k = 0; k < n; k++) {
                            if (sc->seen[nb[k]]) continue;
                            if (w->region[nb[k] / WORLD_W][nb[k] % WORLD_W] != r) continue;
                            sc->seen[nb[k]] = 1;
                            sc->queue[tail++] = nb[k];
                        }
                    }
                }
                if (count != w->regions[r].tiles) {
                    printf("  seed %.0f region %d is in pieces: %d of %d tiles connected\n",
                           (double)seed, r, count, w->regions[r].tiles);
                    fails++;
                }
                (void)i;
            }
        }
    }

    if (checked == 0) {
        printf("gating  : no seed produced a usable world\n");
        fails++;
    } else if (gated_seeds == 0) {
        printf("  gating never blocked anything across %d seeds - the parity check"
               " above is vacuous\n", checked);
        fails++;
    }

    /* Negative control: a graph walker that ignores the terrain gate must
     * disagree with the real walk on a gated seed. */
    {
        int disagreed = 0;
        for (s = 0; s < seeds && !disagreed; s++) {
            Uint32 gateless = 0, walked;
            int queue[REGION_COUNT], head = 0, tail = 0, i;
            world_gen(w, sc, ents, base + (Uint64)s, BIOME_FOREST);
            if (w->region_count < 2 || w->spawn_region < 0) continue;
            gateless = 1u << w->spawn_region;
            queue[tail++] = w->spawn_region;
            while (head < tail) {
                int r = queue[head++];
                for (i = 0; i < w->region_count; i++) {
                    if (!(w->regions[r].adj & (1u << i))) continue;
                    if (gateless & (1u << i)) continue;
                    gateless |= 1u << i;      /* NOTE: no terrain check */
                    queue[tail++] = i;
                }
            }
            walked = walk_regions(w, ABIL_NONE, w->spawn_tile, sc->seen, sc->queue);
            if (gateless != walked) disagreed = 1;
        }
        if (!disagreed) {
            printf("  gating negative control FAILED: a gate-blind walker agreed"
                   " with the real walk on every seed\n");
            fails++;
        } else {
            printf("gating  : negative control - a gate-blind graph walker disagrees"
                   " with the real walk\n");
        }
    }

    /* EVERY GATE MUST BE VISIBLE, which is the bug the ridges exist to fix: a
     * gate is a property of a whole region, and the partition is a Voronoi over
     * open ground, so before the ridges the line where "too steep to climb"
     * began ran through flat grass with nothing drawn on it.
     *
     * Measured as unexplained crossings: an open tile she can stand on, next to
     * an open tile that demands an ability this one did not, with no solid tile
     * anywhere within GATE_LOOK - nothing on screen, in other words, that could
     * account for being stopped. Solid rather than rock specifically, because a
     * pond bank explains a Wade gate just as honestly as a cliff explains a
     * Climb one.
     *
     * The control is the SAME measurement on the same worlds with the ridge
     * step skipped - the previous behaviour, rebuilt here from the real
     * generator functions rather than described. If that does not produce a
     * large count, this check is measuring nothing. */
    {
        int before = 0, after = 0, seeds_bad = 0;
        for (s = 0; s < seeds; s++) {
            Uint64 seed = base + (Uint64)s;
            Rng rng;
            int depth[REGION_COUNT], n;

            /* The pipeline up to the point where gates exist but ridges do not,
             * which is exactly the state the old generator shipped. */
            world_stub(w, seed);
            world_spawn(w, sc);
            regions_build(w, sc);
            if (w->spawn_tile < 0 || w->region_count < 2) continue;
            regions_depth(w, depth);
            rng_seed(&rng, seed, STREAM_ENTITIES);
            regions_assign_terrain(w, &rng, depth);

            before += gate_unexplained(w);

            /* The same two steps, in the same order, that gate_try runs. */
            gate_ridges(w, seed);
            gate_repair(w);
            regions_relink(w, sc);
            n = gate_unexplained(w);
            after += n;
            if (n) seeds_bad++;
        }
        if (before == 0) {
            printf("  gate-visibility negative control FAILED: the un-ridged"
                   " generator left no unexplained gate crossings, so this check"
                   " cannot detect one\n");
            fails++;
        } else {
            printf("gating  : negative control - without ridges, %d unexplained gate"
                   " crossings over %d seeds\n", before, seeds);
        }
        if (after) {
            printf("  %d unexplained gate crossings remain on %d of %d seeds -"
                   " she is stopped by nothing she can see\n", after, seeds_bad, seeds);
            fails++;
        } else {
            printf("gating  : every gate crossing is flanked by something solid"
                   " within %d tiles\n", GATE_LOOK);
        }
    }

    SDL_free(w);
    SDL_free(sc);
    printf("gating  : %d seeds, walk == graph at 4 ability tiers; %d seeds actually gated\n",
           checked, gated_seeds);
    printf("gating  : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* Every generated world must be finishable, and the verifier must be capable of
 * saying no. */
static int reach_selftest(int seeds, Uint64 base)
{
    World *w = (World *)SDL_malloc(sizeof(World));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    Entity ents[ENTITY_COUNT];
    int fails = 0, s, relaxed = 0, worst_attempts = 0, checked = 0;

    if (!w || !sc) { printf("reach   : out of memory\n"); SDL_free(w); SDL_free(sc); return 1; }

    /* All three areas: Area 2 and Area 3's worlds are BIOME_UNDERWORLD and
     * BIOME_LUMIARA generated from the exact seed derivation game_init_area
     * uses (seed ^ AREA2_SEED_SALT / AREA3_SEED_SALT), so this exercises the
     * same worlds the portals actually lead to, not a standalone
     * approximation of them. world_solvable and place_entities are already
     * generic over ENTITY_COUNT, ABIL_* and world_gen (see their own
     * comments), so this loop needed no change beyond the outer area pass. */
    {
    int area;
    for (area = 1; area <= 3; area++) {
    Uint8 biome = (area == 1) ? BIOME_FOREST : (area == 2) ? BIOME_UNDERWORLD : BIOME_LUMIARA;
    for (s = 0; s < seeds; s++) {
        Uint64 seed = base + (Uint64)s;
        Uint64 area_seed = (area == 1) ? seed : (area == 2) ? (seed ^ AREA2_SEED_SALT) : (seed ^ AREA3_SEED_SALT);
        int attempts = world_gen(w, sc, ents, area_seed, biome);
        int restored = 0, i;

        if (w->region_count < 1) continue;
        checked++;
        if (attempts < 0) relaxed++;
        else if (attempts > worst_attempts) worst_attempts = attempts;

        if (!world_solvable(w, ents, &restored)) {
            printf("  area %d seed %.0f: SHIPPED UNSOLVABLE - only %d of %d restorable\n",
                   area, (double)seed, restored, ENTITY_COUNT);
            fails++;
        }
        /* Every entity must actually be somewhere, on an open tile. */
        for (i = 0; i < ENTITY_COUNT; i++) {
            if (ents[i].tile < 0) {
                printf("  area %d seed %.0f: entity %d was never placed\n",
                       area, (double)seed, i);
                fails++;
            } else if (w->solid[ents[i].tile / WORLD_W][ents[i].tile % WORLD_W]) {
                printf("  area %d seed %.0f: entity %d is inside a wall\n",
                       area, (double)seed, i);
                fails++;
            }
        }
    }
    }
    }

    /* Negative controls: the verifier must REJECT worlds that cannot be
     * finished. Without these, `world_solvable` returning 1 proves nothing. */
    {
        int restored = 0, i;
        world_gen(w, sc, ents, base, BIOME_FOREST);
        if (w->region_count >= 3) {
            /* (a) Seal everything behind Kindle and strand the Kindle grant
             * inside the seal. Nothing beyond spawn can ever be entered. */
            for (i = 0; i < w->region_count; i++)
                if (i != w->spawn_region) w->regions[i].terrain = TERRAIN_DARK;
            for (i = 0; i < ENTITY_COUNT; i++)
                ents[i].region = (Uint8)((w->spawn_region + 1) % w->region_count);
            if (world_solvable(w, ents, &restored)) {
                printf("  reach negative control FAILED: a sealed world was called solvable\n");
                fails++;
            } else if (restored != 0) {
                printf("  reach negative control: sealed world let %d entities through\n", restored);
                fails++;
            }

            /* (b) A self-locked gate: one region needs Wade, and the only Wade
             * grant is inside it. Catches a verifier that checks the first tier
             * instead of iterating to a fixed point - which is the subtle way
             * to get this wrong. */
            for (i = 0; i < w->region_count; i++) w->regions[i].terrain = TERRAIN_NORMAL;
            {
                int stranded = (w->spawn_region + 1) % w->region_count;
                w->regions[stranded].terrain = TERRAIN_WATER;
                for (i = 0; i < ENTITY_COUNT; i++) {
                    ents[i].region = (Uint8)w->spawn_region;
                    ents[i].grants = 0;
                }
                ents[0].region = (Uint8)stranded;
                ents[0].grants = ABIL_WADE;
                if (world_solvable(w, ents, &restored)) {
                    printf("  reach negative control FAILED: a self-locked gate"
                           " was called solvable\n");
                    fails++;
                } else if (restored != ENTITY_COUNT - 1) {
                    printf("  reach negative control: expected %d of %d restored, got %d\n",
                           ENTITY_COUNT - 1, ENTITY_COUNT, restored);
                    fails++;
                }
            }
            printf("reach   : negative controls - sealed world and self-locked gate both rejected\n");
        }
    }

    SDL_free(w);
    SDL_free(sc);
    printf("reach   : %d seeds all solvable; worst placement took %d attempt(s);"
           " %d seed(s) needed gating relaxed\n", checked, worst_attempts, relaxed);
    printf("reach   : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* Distance field over tiles the given abilities can actually stand on. Uses the
 * REAL tile_blocked, so the autopilot cannot walk somewhere the player could
 * not - which is the whole point of driving the real simulation. */
/* A headless playthrough, driving the REAL simulation - real tile_blocked, real
 * move_axis, real interact radius, real apply_restore. A test that walked a
 * private copy of the rules would only prove things about the copy.
 *
 * The distance field is rebuilt ONCE PER GOAL, not once per step. Re-rooting it
 * every step is the obvious way to write this and is ~20,000x more BFS work -
 * it turned a sub-second test into one that ran for over six minutes without
 * finishing a single seed. */
static int play_selftest(int seeds, Uint64 base)
{
    World *w = (World *)SDL_malloc(sizeof(World));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    Entity ents[ENTITY_COUNT];
    int fails = 0, s, completed = 0, checked = 0;
    long worst_steps = 0;

    if (!w || !sc) { printf("play    : out of memory\n"); SDL_free(w); SDL_free(sc); return 1; }

    /* All three areas, same seed derivation game_init_area uses - see reach_selftest. */
    {
    int area;
    for (area = 1; area <= 3; area++) {
    Uint8 biome = (area == 1) ? BIOME_FOREST : (area == 2) ? BIOME_UNDERWORLD : BIOME_LUMIARA;
    for (s = 0; s < seeds; s++) {
        Uint64 seed = base + (Uint64)s;
        Uint64 area_seed = (area == 1) ? seed : (area == 2) ? (seed ^ AREA2_SEED_SALT) : (seed ^ AREA3_SEED_SALT);
        Player p;
        int frags = 0, souls = 0;
        long step;

        world_gen(w, sc, ents, area_seed, biome);
        if (w->spawn_tile < 0 || w->region_count < 1) continue;
        checked++;

        SDL_zero(p);
        p.x = (float)(w->spawn_tile % WORLD_W) * TILE + TILE * 0.5f;
        p.y = (float)(w->spawn_tile / WORLD_W) * TILE + TILE * 0.5f;

        step = 0;
        {
            int goal;
            /* One iteration per collectible, plus slack for a goal that becomes
             * unreachable and has to be re-picked. */
            for (goal = 0; goal < ENTITY_COUNT * 3; goal++) {
                int target = -1, i, best = 1 << 30, here, stuck;

                i = entity_in_reach(w, ents, p.x, p.y);
                if (i >= 0) apply_restore(w, ents, &p, i, &frags, &souls);
                if (frags + souls >= ENTITY_COUNT) break;

                here = (int)(p.y / TILE) * WORLD_W + (int)(p.x / TILE);
                bfs_gated(w, p.abilities, here, sc->dist, sc->queue);
                for (i = 0; i < ENTITY_COUNT; i++) {
                    int t = ents[i].tile, d;
                    if (t < 0 || ents[i].restored) continue;
                    d = sc->dist[t];
                    if (d >= 0 && d < best) { best = d; target = t; }
                }
                if (target < 0) break;   /* nothing reachable: the run stalled */

                /* Field rooted at the GOAL, so walking is a gradient descent
                 * that needs no further BFS until the goal changes. */
                bfs_gated(w, p.abilities, target, sc->dist, sc->queue);

                for (stuck = 0; stuck < 20000; stuck++) {
                    static const int dxs[4] = { 1, -1, 0, 0 };
                    static const int dys[4] = { 0, 0, 1, -1 };
                    int hx, hy, d, next = -1, bestd;
                    float tx, ty, ddx, ddy, len;

                    i = entity_in_reach(w, ents, p.x, p.y);
                    if (i >= 0) {
                        apply_restore(w, ents, &p, i, &frags, &souls);
                        break;     /* goal met; pick the next one */
                    }
                    hx = (int)(p.x / TILE); hy = (int)(p.y / TILE);
                    here = hy * WORLD_W + hx;
                    bestd = sc->dist[here];
                    if (bestd < 0) break;
                    for (d = 0; d < 4; d++) {
                        int nx = hx + dxs[d], ny = hy + dys[d], ni;
                        if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
                        ni = ny * WORLD_W + nx;
                        if (sc->dist[ni] < 0) continue;
                        if (sc->dist[ni] < bestd) { bestd = sc->dist[ni]; next = ni; }
                    }
                    if (next < 0) {
                        /* Already on the goal tile but not in interact range -
                         * step toward its exact centre instead of the grid. */
                        next = target;
                        if (here == target) {
                            tx = (float)(target % WORLD_W) * TILE + TILE * 0.5f;
                            ty = (float)(target / WORLD_W) * TILE + TILE * 0.5f;
                            ddx = tx - p.x; ddy = ty - p.y;
                            if (ddx * ddx + ddy * ddy < 1.0f) break;
                        }
                    }
                    tx = (float)(next % WORLD_W) * TILE + TILE * 0.5f;
                    ty = (float)(next / WORLD_W) * TILE + TILE * 0.5f;
                    ddx = tx - p.x; ddy = ty - p.y;
                    len = SDL_sqrtf(ddx * ddx + ddy * ddy);
                    if (len > 0.0f) {
                        move_axis(w, &p, (ddx / len) * PLAYER_SPEED * TICK_DT, 0.0f);
                        move_axis(w, &p, 0.0f, (ddy / len) * PLAYER_SPEED * TICK_DT);
                    }
                    step++;
                }
            }
        }
        if (step > worst_steps) worst_steps = step;
        if (frags + souls >= ENTITY_COUNT) {
            completed++;
        } else {
            printf("  area %d seed %.0f: stalled with %d of %d restored after %ld steps\n",
                   area, (double)seed, frags + souls, ENTITY_COUNT, step);
            fails++;
        }
    }
    }
    }

    SDL_free(w);
    SDL_free(sc);
    printf("play    : %d/%d seeds completed; worst run %ld steps (%.0f s of play)\n",
           completed, checked, worst_steps, (double)worst_steps * (double)TICK_DT);
    printf("play    : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* ---- --mockup-test ------------------------------------------------------
 *
 * The check that turns "follow the mockup pattern" from a taste judgement into
 * a property. It renders real frames and counts VISIBLE PIXELS by colour class,
 * which is the same measurement taken from Mockup1.png and Mockup2.png - unlike
 * a ground-type census, which counts terrain the canopy is standing on top of.
 *
 * Bands come from those two files:
 *   grass 39.2/42.8  olive 9.1/10.2  dirt 6.5/4.0  water 12.4/11.0
 *   tree-dark 10.6/9.8  foliage-mid 5.5/5.5  outline 5.1/5.3  rock 5.6/5.0
 * widened to leave room for seed-to-seed variation without letting a class
 * drift to double or half what the art does.
 */
enum { MC_GRASS = 0, MC_OLIVE, MC_DIRT, MC_WATER, MC_TREE, MC_FOLIAGE,
       MC_OUTLINE, MC_ROCK, MC_OTHER, MC_COUNT };

static int mockup_class_of(int r, int g, int b)
{
    Uint32 c = ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
    switch (c) {
    case 0xA3B315: return MC_GRASS;
    case 0x70801A: return MC_OLIVE;
    case 0xA4612B: case 0xA2622F: case 0x7F4A14: case 0x7A3F20: return MC_DIRT;
    case 0x138D59: case 0x07333B: case 0x127561: case 0x14AD82:
    case 0x1C454F: case 0x3E7D8D: return MC_WATER;
    case 0x235D31: return MC_TREE;
    case 0x4A8636: return MC_FOLIAGE;
    case 0x10141C: return MC_OUTLINE;
    case 0x4C3034: case 0x7D5555: case 0xAF9074: return MC_ROCK;
    default: return MC_OTHER;
    }
}

static int mockup_selftest(void)
{
    static const struct { const char *name; double lo, hi; } band[MC_COUNT] = {
        { "grass",   30.0, 50.0 }, { "olive",   5.0, 16.0 },
        { "dirt",     2.0, 12.0 }, { "water",   5.0, 20.0 },
        { "tree",     6.0, 16.0 }, { "foliage", 3.0,  9.0 },
        { "outline",  3.0,  9.0 }, { "rock",    2.0, 10.0 },
        { "other",    0.0, 12.0 }
    };
    SDL_Surface *fb = test_surface(LOGICAL_W, LOGICAL_H);
    World *w = (World *)SDL_malloc(sizeof(World));
    DrawList *dl = (DrawList *)SDL_malloc(sizeof(DrawList));
    double count[MC_COUNT];
    int fails = 0, s, i, x, y, frames = 0;
    const int SEEDS = 6;

    if (!fb || !w || !dl) {
        printf("mockup  : out of memory\n");
        if (fb) SDL_FreeSurface(fb);
        SDL_free(w); SDL_free(dl);
        return 1;
    }
    if (!fogpal && !fogpal_build(fb)) {
        printf("mockup  : could not build the fog palette\n");
        SDL_FreeSurface(fb); SDL_free(w); SDL_free(dl);
        return 1;
    }
    for (i = 0; i < MC_COUNT; i++) count[i] = 0.0;

    for (s = 0; s < SEEDS; s++) {
        int cx, cy;
        world_stub(w, (Uint64)(s + 1));
        SDL_memset(w->reveal, 0xFF, sizeof w->reveal);
        /* Four spread camera positions per seed, so one lucky framing cannot
         * carry the average. */
        for (cy = 240; cy + LOGICAL_H < WORLD_H * TILE; cy += 620) {
            for (cx = 240; cx + LOGICAL_W < WORLD_W * TILE; cx += 620) {
                Player p;
                SDL_zero(p);
                p.x = (float)(cx + LOGICAL_W / 2);
                p.y = (float)(cy + LOGICAL_H / 2);
                render_world(fb, w, (Uint64)(s + 1), cx, cy);
                props_build(LOGICAL_W, LOGICAL_H, w, (Uint64)(s + 1), cx, cy,
                            NULL, &p, 0.0f, dl);
                props_draw(fb, dl);
                frames++;
                for (y = 0; y < fb->h; y++) {
                    for (x = 0; x < fb->w; x++) {
                        Uint8 r, g, b;
                        SDL_GetRGB(((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x],
                                   fb->format, &r, &g, &b);
                        count[mockup_class_of(r, g, b)] += 1.0;
                    }
                }
            }
        }
    }

    {
        double total = 0.0;
        for (i = 0; i < MC_COUNT; i++) total += count[i];
        printf("mockup  : %d frames over %d seeds, visible-pixel census\n", frames, SEEDS);
        for (i = 0; i < MC_COUNT; i++) {
            double pct = 100.0 * count[i] / total;
            int ok = (pct >= band[i].lo && pct <= band[i].hi);
            printf("          %-8s %5.1f%%  [%.0f-%.0f] %s\n",
                   band[i].name, pct, band[i].lo, band[i].hi, ok ? "" : "OUT OF BAND");
            if (!ok) fails++;
        }
    }

    /* Negative control: a flat all-grass world must be REJECTED, or the bands
     * are not actually constraining anything. */
    {
        double flat[MC_COUNT];
        int bad = 0;
        for (i = 0; i < MC_COUNT; i++) flat[i] = 0.0;
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                w->terr[y][x] = GT_GRASS;
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                w->canopy[y][x] = 0;
        SDL_memset(w->reveal, 0xFF, sizeof w->reveal);
        render_world(fb, w, 1, 300, 300);
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++) {
                Uint8 r, g, b;
                SDL_GetRGB(((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x],
                           fb->format, &r, &g, &b);
                flat[mockup_class_of(r, g, b)] += 1.0;
            }
        for (i = 0; i < MC_COUNT; i++) {
            double pct = 100.0 * flat[i] / (double)(fb->w * fb->h);
            if (pct < band[i].lo || pct > band[i].hi) bad++;
        }
        if (bad == 0) {
            printf("  mockup negative control FAILED: an all-grass world"
                   " satisfied every band\n");
            fails++;
        } else {
            printf("mockup  : negative control - a flat all-grass world misses %d of %d bands\n",
                   bad, MC_COUNT);
        }
    }

    SDL_FreeSurface(fb);
    SDL_free(w);
    SDL_free(dl);
    printf("mockup  : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* ---- Rock/stone ground context, measured off the art ---------------------
 *
 * Which ground a Forest rock sprite was DRAWN for, read from its own pixels:
 * a green skirt and no brown means grass art, brown and no green means dirt
 * art, neither means the skirtless variant. -1 for anything that is not one of
 * the seven, -2 for a sprite carrying both (which would mean the sheet changed
 * under us and the whole classification needs re-deriving).
 *
 * MEASURED rather than looked up, deliberately. A test that asked
 * prop_ground_art which ground its own entries were for would agree with
 * itself no matter what was in the table; asking the PIXELS makes the check an
 * independent second opinion, and the two only agree if the table is right.
 *
 * Cached because sprite_colour_px decodes a whole sprite and the sweep below
 * asks this tens of thousands of times. */
static int prop_skirt_class(int art)
{
    static const short ids[7] = { ART_ROCK_A, ART_ROCK_B, ART_ROCK_C,
                                  ART_STONE_A, ART_STONE_B, ART_STONE_C, ART_PEBBLES };
    static int cached[7];
    static int ready;
    int i;

    if (!ready) {
        for (i = 0; i < 7; i++) {
            const ArtSprite *sp = &ART_SPRITES[ids[i]];
            int green = sprite_colour_px(sp, forest_green_rgb, 3);
            int brown = sprite_colour_px(sp, forest_brown_rgb, 2);
            cached[i] = (green > 0 && brown > 0) ? -2
                      : (green > 0) ? GC_GREEN
                      : (brown > 0) ? GC_BROWN : GC_BARE;
        }
        ready = 1;
    }
    for (i = 0; i < 7; i++)
        if (ids[i] == art) return cached[i];
    return -1;
}

/* A SKIRTED sprite has to match its ground exactly. A skirtless one is safe on
 * any ground, which is precisely what lets PEBBLES stay in all three tables
 * instead of needing two variants that do not exist. */
static int prop_skirt_ok(int skirt, int gc) { return skirt == GC_BARE || skirt == gc; }

/* The y-sort and the prop-ghosting predicate. */
static int sort_selftest(void)
{
    static const struct {
        int after, sx0, sy0, sx1, sy1, want;
        const char *what;
    } cases[] = {
        { 0,  0,  0, 10, 10, 0, "behind the player, overlapping (drawn first)" },
        { 0,  4,  4,  6,  6, 0, "behind, prop box inside the player box" },
        { 0,  2,  2, 12, 12, 0, "behind, straddling the player box" },
        { 1,  0,  0, 10, 10, 1, "in front, overlapping" },
        { 1,  4,  4,  6,  6, 1, "in front, prop box inside the player box" },
        { 1, 40,  0, 50, 10, 0, "in front, clear in x" },
        { 1,  0, 40, 10, 50, 0, "in front, clear in y" },
        { 1, 10,  0, 20, 10, 0, "in front, edge-touching only (boxes half-open)" },
        { 0, 40, 40, 50, 50, 0, "behind and clear" }
    };
    /* The player box every case is tested against. */
    const int px0 = 0, py0 = 0, px1 = 10, py1 = 10;
    int fails = 0, i;
    DrawList *dl;

    for (i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        int got = prop_covers_player(cases[i].after,
                                     cases[i].sx0, cases[i].sy0, cases[i].sx1, cases[i].sy1,
                                     px0, py0, px1, py1);
        if (got != cases[i].want) {
            printf("  cover %s: got %d, expected %d\n", cases[i].what, got, cases[i].want);
            fails++;
        }
    }
    /* Negative control: a predicate that ignores draw order - the obvious way
     * to write this - must fail the table, or the `after` clause is untested. */
    {
        int bad = 0;
        for (i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
            int naive = !(cases[i].sx1 <= px0 || cases[i].sx0 >= px1 ||
                          cases[i].sy1 <= py0 || cases[i].sy0 >= py1);
            if (naive != cases[i].want) bad++;
        }
        if (bad == 0) {
            printf("  sort negative control FAILED: an order-blind predicate"
                   " satisfied the whole table\n");
            fails++;
        } else {
            printf("sort    : negative control - order-blind cover test misses %d of %d cases\n",
                   bad, (int)(sizeof cases / sizeof *cases));
        }
    }

    /* STRUCTURAL: every entry of every ground-context table must actually be
     * art for that ground, measured off the sprite rather than asserted. */
    {
        int gc, slot, k, wrong = 0, ctl = 0;
        for (gc = 0; gc < GC_COUNT; gc++) {
            for (slot = 0; slot < 2; slot++) {
                const PropArt *pa = prop_ground_art(BIOME_FOREST,
                                                    slot == 0 ? PROP_ROCK : PROP_STONE, gc);
                if (!pa || pa->n <= 0) {
                    printf("  ground table for class %d slot %d is empty\n", gc, slot);
                    fails++;
                    continue;
                }
                for (k = 0; k < pa->n; k++) {
                    int skirt = prop_skirt_class(pa->ids[k]);
                    if (!prop_skirt_ok(skirt, gc)) {
                        printf("  ground class %d entry %d (sprite %d) is %s art\n",
                               gc, k, pa->ids[k],
                               skirt == GC_GREEN ? "grass" : skirt == GC_BROWN ? "dirt"
                             : skirt == -2 ? "mixed-skirt" : "unclassifiable");
                        fails++;
                        wrong++;
                    }
                }
            }
        }
        /* Negative control: the flat tables this replaced held all three
         * variants at once, so on any given ground two thirds of what they
         * could return was art for a DIFFERENT ground. If the predicate cannot
         * see that, it is not measuring anything. */
        for (gc = 0; gc < GC_COUNT; gc++) {
            for (k = 0; k < (int)(sizeof art_rocks / sizeof *art_rocks); k++)
                if (!prop_skirt_ok(prop_skirt_class(art_rocks[k]), gc)) ctl++;
            for (k = 0; k < (int)(sizeof art_stones / sizeof *art_stones); k++)
                if (!prop_skirt_ok(prop_skirt_class(art_stones[k]), gc)) ctl++;
        }
        if (ctl == 0) {
            printf("  ground-context negative control FAILED: the old flat rock"
                   " tables measured as correct on every ground\n");
            fails++;
        } else {
            printf("sort    : negative control - the flat rock/stone tables offer"
                   " %d wrong-ground sprites across the 3 grounds\n", ctl);
        }
        if (!wrong)
            printf("sort    : every ground-context rock/stone entry matches its ground\n");
    }

    dl = (DrawList *)SDL_malloc(sizeof(DrawList));
    if (!dl) { printf("sort    : out of memory\n"); return fails + 1; }

    /* Sorting: non-decreasing in feet_y, and STABLE, so a frame cannot flicker
     * between two orderings of equal-footed sprites. Stability is checked by
     * giving items a payload that records their build order. */
    {
        Rng r;
        int k, unsorted_bad = 0;
        rng_seed(&r, 12345, STREAM_TERRAIN);
        dl->n = 0;
        for (k = 0; k < 200; k++) {
            /* Deliberately many duplicate keys, which is where an unstable
             * sort shows up. */
            dl->item[dl->n].feet_y = (int)rng_below(&r, 12);
            dl->item[dl->n].x = k;              /* build order, as a payload */
            dl->item[dl->n].y = 0;
            dl->item[dl->n].art = 0;
            dl->item[dl->n].fade = 0;
            dl->n++;
        }
        /* Negative control: the list must NOT already be ordered, or the check
         * below proves nothing. */
        for (k = 1; k < dl->n; k++)
            if (dl->item[k].feet_y < dl->item[k - 1].feet_y) unsorted_bad++;
        if (unsorted_bad == 0) {
            printf("  sort negative control FAILED: the input was already sorted\n");
            fails++;
        }

        draw_list_sort(dl);

        for (k = 1; k < dl->n; k++) {
            if (dl->item[k].feet_y < dl->item[k - 1].feet_y) {
                printf("  sorted list is out of order at %d (%d < %d)\n",
                       k, dl->item[k].feet_y, dl->item[k - 1].feet_y);
                fails++;
                break;
            }
            if (dl->item[k].feet_y == dl->item[k - 1].feet_y &&
                dl->item[k].x < dl->item[k - 1].x) {
                printf("  sort is not stable at %d: build order %d before %d\n",
                       k, dl->item[k].x, dl->item[k - 1].x);
                fails++;
                break;
            }
        }
        printf("sort    : %d items sorted, order and stability hold (%d inversions in the input)\n",
               dl->n, unsorted_bad);
    }

    /* The draw list must not overflow at the real view size. A silent overflow
     * looks like props randomly failing to appear, which is close to
     * undiagnosable from a screenshot. */
    {
        World *w = (World *)SDL_malloc(sizeof(World));
        int worst = 0, s, samples = 0;
        /* Ground-context tallies, gathered over the same sweep: every rock and
         * stone actually placed by props_build, checked against the terrain it
         * was placed on. The structural check above proves the TABLES are
         * right; this proves they are what the renderer reaches. */
        long rocks_seen = 0, rocks_bad = 0, rocks_ctl = 0;
        if (w) {
            Player p;
            SDL_zero(p);
            for (s = 0; s < 8; s++) {
                int cx, cy;
                world_stub(w, (Uint64)(s + 1));
                w->biome = BIOME_FOREST;
                for (cy = 0; cy + LOGICAL_H < WORLD_H * TILE; cy += 61) {
                    for (cx = 0; cx + LOGICAL_W < WORLD_W * TILE; cx += 67) {
                        int k;
                        p.x = (float)(cx + LOGICAL_W / 2);
                        p.y = (float)(cy + LOGICAL_H / 2);
                        props_build(LOGICAL_W, LOGICAL_H, w, (Uint64)(s + 1),
                                    cx, cy, NULL, &p, 0.0f, dl);
                        samples++;
                        if (dl->n > worst) worst = dl->n;
                        if (dl->dropped) {
                            printf("  draw list overflowed (%d dropped) at seed %d cam %d,%d\n",
                                   dl->dropped, s + 1, cx, cy);
                            fails++;
                            s = 8; cy = WORLD_H * TILE; break;
                        }
                        for (k = 0; k < dl->n; k++) {
                            int skirt = prop_skirt_class(dl->item[k].art);
                            int tx, ty, gc;
                            if (skirt == -1) continue;   /* not a rock or a stone */
                            /* Invert props_build's own placement to recover the
                             * tile: x = tx*TILE + TILE/2 - cam_x, y = ty*TILE +
                             * TILE - cam_y. */
                            tx = (dl->item[k].x + cx - TILE / 2) / TILE;
                            ty = (dl->item[k].y + cy - TILE) / TILE;
                            if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
                                continue;
                            gc = ground_class(w->terr[ty][tx]);
                            rocks_seen++;
                            if (!prop_skirt_ok(skirt, gc)) {
                                if (rocks_bad == 0)
                                    printf("  seed %d tile %d,%d is ground class %d but"
                                           " carries sprite %d (%s art)\n",
                                           s + 1, tx, ty, gc, dl->item[k].art,
                                           skirt == GC_GREEN ? "grass" : "dirt");
                                rocks_bad++;
                            }
                            /* Negative control on the SAME placements: what the
                             * old ground-blind pick would have drawn here. */
                            {
                                Uint32 h = tile_hash((Uint64)(s + 1), tx, ty);
                                const short *old = (skirt >= 0 &&
                                    (dl->item[k].art == ART_ROCK_A ||
                                     dl->item[k].art == ART_ROCK_B ||
                                     dl->item[k].art == ART_ROCK_C)) ? art_rocks : art_stones;
                                int on = (old == art_rocks)
                                       ? (int)(sizeof art_rocks / sizeof *art_rocks)
                                       : (int)(sizeof art_stones / sizeof *art_stones);
                                if (!prop_skirt_ok(prop_skirt_class(old[(h >> 24) % (Uint32)on]), gc))
                                    rocks_ctl++;
                            }
                        }
                    }
                }
            }
            printf("sort    : worst case %d of %d draw slots over %d camera positions\n",
                   worst, DRAW_MAX, samples);
            if (rocks_seen == 0) {
                printf("  ground-context sweep saw no rocks at all - the check"
                       " proved nothing\n");
                fails++;
            } else if (rocks_bad) {
                printf("  %ld of %ld placed rocks/stones carry the wrong ground\n",
                       rocks_bad, rocks_seen);
                fails++;
            } else if (rocks_ctl == 0) {
                printf("  ground-context negative control FAILED: the old"
                       " ground-blind pick was right on all %ld placements\n", rocks_seen);
                fails++;
            } else {
                printf("sort    : %ld placed rocks/stones all match their ground;"
                       " the old ground-blind pick got %ld of them wrong\n",
                       rocks_seen, rocks_ctl);
            }
        }
        SDL_free(w);
    }

    SDL_free(dl);
    printf("sort    : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* The blob autotiler, against a hand-written truth table. Deliberately a table
 * rather than a re-derivation: restating the formula in the test would only
 * prove the formula equals itself. */
static int autotile_selftest(void)
{
    static const struct {
        int n, s, e, w, want;
        const char *what;
    } cases[] = {
        { 1, 1, 1, 1, 4, "interior" },
        { 0, 1, 1, 1, 1, "north differs -> top edge" },
        { 1, 0, 1, 1, 7, "south differs -> bottom edge" },
        { 1, 1, 0, 1, 5, "east differs -> right edge" },
        { 1, 1, 1, 0, 3, "west differs -> left edge" },
        { 0, 1, 1, 0, 0, "north+west -> top-left corner" },
        { 0, 1, 0, 1, 2, "north+east -> top-right corner" },
        { 1, 0, 1, 0, 6, "south+west -> bottom-left corner" },
        { 1, 0, 0, 1, 8, "south+east -> bottom-right corner" },
        { 0, 0, 0, 0, 0, "isolated -> top-left (no 1x1 tile exists)" }
    };
    int fails = 0, i, hits[9];

    for (i = 0; i < 9; i++) hits[i] = 0;
    for (i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        int got = blob_slice(cases[i].n, cases[i].s, cases[i].e, cases[i].w);
        if (got != cases[i].want) {
            printf("  %s: got slice %d, expected %d\n", cases[i].what, got, cases[i].want);
            fails++;
        }
    }
    /* Every slice must be reachable, or part of the 3x3 is dead art that will
     * never appear on screen and nobody will notice is missing. */
    for (i = 0; i < 16; i++) {
        hits[blob_slice(i & 1, (i >> 1) & 1, (i >> 2) & 1, (i >> 3) & 1)]++;
    }
    for (i = 0; i < 9; i++) {
        if (!hits[i]) {
            printf("  slice %d is unreachable from any neighbour mask\n", i);
            fails++;
        }
    }

    /* Every tile the tables name must exist, and the rock ring's centre must be
     * the hollow the source sheet actually has. */
    {
        const short *tables[8];
        int sizes[8], t, k;
        tables[0] = tile_grass_base;   sizes[0] = 4;
        tables[1] = tile_dirt_fill;    sizes[1] = 6;
        tables[2] = tile_water_fill;   sizes[2] = 10;
        tables[3] = tile_grass_edge;   sizes[3] = 9;
        tables[4] = tile_olive_edge;   sizes[4] = 9;
        tables[5] = tile_water_edge;   sizes[5] = 9;
        tables[6] = tile_grass_detail; sizes[6] = 6;
        tables[7] = tile_rock_fill;    sizes[7] = 2;
        for (t = 0; t < 8; t++) {
            for (k = 0; k < sizes[t]; k++) {
                if (tables[t][k] < 0 || tables[t][k] >= ART_SPRITE_COUNT) {
                    printf("  table %d entry %d is not a valid sprite id\n", t, k);
                    fails++;
                } else if (ART_SPRITES[tables[t][k]].w != TILE ||
                           ART_SPRITES[tables[t][k]].h != TILE) {
                    printf("  table %d entry %d is not %dx%d\n", t, k, TILE, TILE);
                    fails++;
                }
            }
        }
        if (tile_rock_ring[4] != ART_NONE) {
            printf("  rock ring centre should be hollow, is sprite %d\n", tile_rock_ring[4]);
            fails++;
        }
        for (k = 0; k < 9; k++) {
            if (k == 4) continue;
            if (tile_rock_ring[k] < 0) {
                printf("  rock ring slice %d is missing\n", k);
                fails++;
            }
        }
    }

    /* blob_interior must name EXACTLY the mask whose slice draws nothing.
     *
     * That agreement is the entire contract behind the pond and outcrop fills.
     * The ground pass lays a fill on the cells blob_interior calls interior; the
     * rim pass skips the cells whose slice is the hollow centre. If the two ever
     * disagree, a cell gets NEITHER - a hole in the middle of a pond - or BOTH,
     * which is the opaque square with its bank floating inside it that the pond
     * fix removed. Neither failure is visible in the predicate; both are obvious
     * on screen, and by then they look like an art bug.
     *
     * Exhaustive over all 16 neighbour masks, against a world built to order
     * rather than a generated one, so no seed has to happen to contain the
     * arrangement being tested. */
    {
        World *w = (World *)SDL_malloc(sizeof(World));
        int mask, bad_axis = 0;

        if (!w) {
            printf("  autotile: out of memory for the interior check\n");
            fails++;
        } else {
            for (mask = 0; mask < 16; mask++) {
                int n = mask & 1, s = (mask >> 1) & 1;
                int e = (mask >> 2) & 1, wst = (mask >> 3) & 1;
                int tx = 8, ty = 8, got, want, slice;
                SDL_memset(w->terr, GT_GRASS, sizeof w->terr);
                w->terr[ty][tx] = GT_WATER;
                if (n)   w->terr[ty - 1][tx] = GT_WATER;
                if (s)   w->terr[ty + 1][tx] = GT_WATER;
                if (e)   w->terr[ty][tx + 1] = GT_WATER;
                if (wst) w->terr[ty][tx - 1] = GT_WATER;
                slice = blob_slice(n, s, e, wst);
                got   = blob_interior(w, tx, ty, GT_WATER);
                want  = (slice == 4);
                if (got != want) {
                    printf("  interior disagrees with the slicer at mask %d:"
                           " interior says %d, slice is %d\n", mask, got, slice);
                    fails++;
                }
                /* Negative control over the same masks: a predicate that forgets
                 * the east/west axis - the same omission the slicer control
                 * below makes - must be caught by this comparison. */
                if ((n && s) != want) bad_axis++;
            }
            if (bad_axis == 0) {
                printf("  interior negative control FAILED: a north/south-only"
                       " predicate agreed with the slicer on every mask\n");
                fails++;
            } else {
                printf("autotile: negative control - a north/south-only interior"
                       " test disagrees on %d of 16 masks\n", bad_axis);
            }
            SDL_free(w);
        }
    }

    /* Negative control: a slicer that ignores the west/east axis - the classic
     * way to get this wrong - must fail the same table. */
    {
        int bad = 0;
        for (i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
            int row = cases[i].n ? (cases[i].s ? 1 : 2) : 0;
            if (row * 3 + 1 != cases[i].want) bad++;
        }
        if (bad == 0) {
            printf("  autotile negative control FAILED: an axis-blind slicer"
                   " satisfied the truth table\n");
            fails++;
        } else {
            printf("autotile: negative control - axis-blind slicer misses %d of %d cases\n",
                   bad, (int)(sizeof cases / sizeof *cases));
        }
    }


    printf("autotile: %d cases, all 9 slices reachable, tables reference real 16x16 tiles\n",
           (int)(sizeof cases / sizeof *cases));
    printf("autotile: %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* The ground pass must cover every visible pixel exactly once. A renderer that
 * left gaps and one that hid them by overdrawing are both wrong, and only
 * counting writes distinguishes them. */
/* Transparent-pixel budget for a rock fill cell: twice the worst the two
 * authored cells actually measure. See the check in tile_selftest. */
#define ROCK_FILL_MAX_TRANS 24

/* How much of itself a BLOCKING tile has to draw as obstacle, out of 256.
 *
 * 96 is three eighths of the cell - enough that a glance reads it as something
 * to walk around. The number that matters is on the other side of the gap: the
 * ring's two top corners draw 12 and 18, so anything between about 20 and 240
 * separates "drawn" from "not drawn" equally well, and nothing is being tuned
 * to sit just above a measurement. */
#define SOLID_MIN_OBSTACLE_PX 96

/* Obstacle pixels the ground pass puts on a blocking cell.
 *
 * `with_fill` selects whether the rock rubble underlay is counted, which is the
 * only difference between the current renderer and the one that shipped the
 * invisible walls - so the same function measures the fix and the control.
 *
 * Ordinary ground returns 0 on purpose. A solid cell drawn as grass is exactly
 * the failure being looked for, and an opacity measure would score it 256. */
static int solid_obstacle_px(const World *w, int tx, int ty, int with_fill)
{
    const TileSet *ts = tileset_for(w->biome);
    int t = w->terr[ty][tx], sl;

    if (t == GT_WATER) {
        if (blob_interior(w, tx, ty, GT_WATER)) return 256;
        sl = blob_slice(terr_at(w, tx, ty - 1) == GT_WATER,
                        terr_at(w, tx, ty + 1) == GT_WATER,
                        terr_at(w, tx + 1, ty) == GT_WATER,
                        terr_at(w, tx - 1, ty) == GT_WATER);
        return 256 - sprite_transparent_px(&ART_SPRITES[ts->water_edge[sl]]);
    }
    if (t == GT_ROCK) {
        int ring;
        /* Mirrors the renderer: the underlay goes under every rock cell, and
         * the border ring takes the cliff table instead of the outcrop one.
         * If this stopped mirroring it, the invisible-wall check would be
         * measuring a tile the screen never shows. */
        if (with_fill)
            return 256 - sprite_transparent_px(
                &ART_SPRITES[(world_border(tx, ty) ? ts->rock_cliff
                                                   : ts->rock_fill)[0]]);
        sl = blob_slice(terr_at(w, tx, ty - 1) == GT_ROCK,
                        terr_at(w, tx, ty + 1) == GT_ROCK,
                        terr_at(w, tx + 1, ty) == GT_ROCK,
                        terr_at(w, tx - 1, ty) == GT_ROCK);
        ring = ts->rock_ring[sl];
        return ring == ART_NONE ? 0
             : 256 - sprite_transparent_px(&ART_SPRITES[ring]);
    }
    return 0;
}

static int tile_selftest(void)
{
    SDL_Surface *fb = test_surface(LOGICAL_W, LOGICAL_H);
    World *w;
    int fails = 0, x, y;
    Uint32 gap_colour;

    if (!fb) { printf("tile    : could not create a test surface\n"); return 1; }
    if (!fogpal && !fogpal_build(fb)) {
        printf("tile    : could not build the fog palette\n");
        SDL_FreeSurface(fb); return 1;
    }
    w = (World *)SDL_malloc(sizeof(World));
    if (!w) { printf("tile    : out of memory\n"); SDL_FreeSurface(fb); return 1; }

    /* Everything through the obstacle-visibility check is art-dependent, so it
     * runs once per biome. world_stub bypasses world_gen entirely (it is
     * testing terrain generation in isolation), so it never sets w->biome -
     * this loop is the one place that must set it by hand. The ground-type
     * census after the loop is terrain-only (GT_* thresholds are shared
     * vocabulary, see the BIOME_* comment at their definition) and does not
     * need repeating. */
    {
    int biome;
    for (biome = 0; biome < BIOME_COUNT; biome++) {
    const TileSet *ts = tileset_for((Uint8)biome);
    const char *bname = biome == BIOME_FOREST ? "forest"
                       : biome == BIOME_UNDERWORLD ? "underworld" : "lumiara";

    world_stub(w, 1);
    w->biome = (Uint8)biome;
    /* world_stub alone leaves reveal at whatever the buffer held; the coverage
     * check below is about geometry, not fog, so light the whole map. */
    SDL_memset(w->reveal, 0xFF, sizeof w->reveal);

    /* STRUCTURAL check, ahead of the pixel one: every tile used as a base fill
     * must be fully opaque. The pixel-coverage test below detects the symptom -
     * ~1% of the screen unpainted - but names no cause; this names the tile.
     *
     * This is exactly how the bug it now guards was found: the authored block
     * at cols 3-4 reads as six more grass fill variants and is not, each
     * carrying 6-14 transparent pixels. */
    {
        const struct { const short *ids; int n; const char *what; } bases[] = {
            { ts->grass_base, 4,  "grass base" },
            { ts->dirt_fill,  6,  "dirt fill" },
            { ts->water_fill, 10, "water fill" }
        };
        int t, k;
        for (t = 0; t < (int)(sizeof bases / sizeof *bases); t++) {
            for (k = 0; k < bases[t].n; k++) {
                const ArtSprite *sp = &ART_SPRITES[bases[t].ids[k]];
                int trans = sprite_transparent_px(sp);
                if (trans != 0) {
                    printf("  %s %s entry %d has %d transparent px - not usable as a base\n",
                           bname, bases[t].what, k, trans);
                    fails++;
                }
            }
        }
        /* Negative control: a tile known to carry transparency must be
         * REJECTED by the same measurement, or it is not measuring opacity.
         * Forest's detail tile specifically - there is no Underworld
         * equivalent (see the tile table comment), and none is needed: this
         * is testing the opacity MEASUREMENT, not biome-specific art. */
        if (sprite_transparent_px(&ART_SPRITES[tile_grass_detail[0]]) == 0) {
            printf("  base-opacity negative control FAILED: a detail tile"
                   " measured as fully opaque\n");
            fails++;
        }
    }

    /* The pond interior must be OPEN WATER: water and lilies, no stone and no
     * land. Forest only - it is the one biome whose water fill is authored art
     * rather than a tinted floor cell (Underworld) or a painterly crop pushed
     * through the quantizer (Lumiara), so it is the one biome where these exact
     * source colours mean anything.
     *
     * Opacity, which the check above already asserts, cannot see this: a tile
     * carrying a corner of grass and a boulder is perfectly opaque and
     * perfectly wrong. It has to be measured in COLOUR, and that measurement is
     * what names the six rim cells that used to be in this table. */
    if (biome == BIOME_FOREST) {
        static const short rejected[6] = {
            ART_TILE_C6_R10, ART_TILE_C7_R10, ART_TILE_C6_R11,
            ART_TILE_C7_R11, ART_TILE_C6_R12, ART_TILE_C7_R12
        };
        int k, worst_land = 0, worst_stone = 0, caught = 0;

        for (k = 0; k < 10; k++) {
            const ArtSprite *sp = &ART_SPRITES[ts->water_fill[k]];
            int land = sprite_colour_px(sp, forest_green_rgb, 3) +
                       sprite_colour_px(sp, forest_brown_rgb, 2);
            int stone = sprite_colour_px(sp, forest_stone_rgb, 4);
            if (land > worst_land)   worst_land = land;
            if (stone > worst_stone) worst_stone = stone;
            if (land > 0) {
                printf("  water fill entry %d paints %d px of LAND colour -"
                       " a pond would show a grass/dirt notch in open water\n", k, land);
                fails++;
            }
            if (stone > 0) {
                printf("  water fill entry %d paints %d px of STONE colour -"
                       " a boulder in the middle of a pond\n", k, stone);
                fails++;
            }
        }
        /* Negative control, and a pointed one: the six cells this table used to
         * name must each be REJECTED by the same predicate. A purity check that
         * has never rejected the art it was written against proves nothing, and
         * these are not hypothetical bad tiles - they are the ones that shipped. */
        for (k = 0; k < 6; k++) {
            const ArtSprite *sp = &ART_SPRITES[rejected[k]];
            if (sprite_colour_px(sp, forest_green_rgb, 3) +
                sprite_colour_px(sp, forest_brown_rgb, 2) > 0 &&
                sprite_colour_px(sp, forest_stone_rgb, 4) > 0)
                caught++;
        }
        if (caught != 6) {
            printf("  water-purity negative control FAILED: only %d of the 6 rim"
                   " cells measured as contaminated\n", caught);
            fails++;
        } else {
            printf("tile    : forest water fill is open water (worst %d land px,"
                   " %d stone px); all 6 rejected rim cells still measure dirty\n",
                   worst_land, worst_stone);
        }
    }

    /* An outcrop's rubble and its rim must have been drawn for the SAME ground.
     *
     * The sheet ships both pieces twice, once in grass and once in dirt, and
     * they sit adjacent in it - so taking the rim from one block and the
     * rubble from the other is an easy mistake to make and an invisible one to
     * read in the tables, which name cells by coordinate. On screen it is not
     * invisible at all: it is ~80 px of the wrong ground per cell, which is the
     * brown rectangle an outcrop used to stamp on green grass.
     *
     * Measured off the art, so this compares the two cells rather than
     * restating which ones were picked. */
    if (biome == BIOME_FOREST) {
        int k, ring_green = 0, ring_brown = 0, fill_green = 0, fill_brown = 0;

        for (k = 0; k < 9; k++) {
            if (tile_rock_ring[k] == ART_NONE) continue;
            ring_green += sprite_colour_px(&ART_SPRITES[tile_rock_ring[k]], forest_green_rgb, 3);
            ring_brown += sprite_colour_px(&ART_SPRITES[tile_rock_ring[k]], forest_brown_rgb, 2);
        }
        for (k = 0; k < 2; k++) {
            fill_green += sprite_colour_px(&ART_SPRITES[ts->rock_fill[k]], forest_green_rgb, 3);
            fill_brown += sprite_colour_px(&ART_SPRITES[ts->rock_fill[k]], forest_brown_rgb, 2);
        }
        if ((ring_green > 0) != (fill_green > 0) || (ring_brown > 0) != (fill_brown > 0)) {
            printf("  rock rim and rubble disagree on their ground: rim %d green /"
                   " %d brown px, rubble %d green / %d brown px\n",
                   ring_green, ring_brown, fill_green, fill_brown);
            fails++;
        } else {
            printf("tile    : forest rock rim and rubble share a ground"
                   " (rim %d green %d brown, rubble %d green %d brown)\n",
                   ring_green, ring_brown, fill_green, fill_brown);
        }
        /* Negative control: the dirt-context rubble that used to be paired with
         * this grass rim must be REJECTED against it, or the comparison is not
         * looking at ground at all. */
        {
            static const short dirt_rubble[2] = { ART_TILE_C6_R7, ART_TILE_C7_R7 };
            int cg = 0, cb = 0;
            for (k = 0; k < 2; k++) {
                cg += sprite_colour_px(&ART_SPRITES[dirt_rubble[k]], forest_green_rgb, 3);
                cb += sprite_colour_px(&ART_SPRITES[dirt_rubble[k]], forest_brown_rgb, 2);
            }
            if ((ring_green > 0) == (cg > 0) && (ring_brown > 0) == (cb > 0)) {
                printf("  rock-ground negative control FAILED: the dirt rubble"
                       " matched the grass rim\n");
                fails++;
            } else {
                printf("tile    : negative control - the dirt rubble (%d green,"
                       " %d brown px) is rejected against the grass rim\n", cg, cb);
            }
        }
    }

    /* The map's edge is a cliff, and must be drawn from cliff art.
     *
     * Two properties, both measured off the sprites rather than asserted about
     * them: it has to be DARK - no pale sunlit highlight, which is what made
     * the old border read as a broad beige band where the reference shows a
     * thin cool ridge - and it still has to cover enough of its cell to be
     * visibly solid, because a nicer-looking edge is not worth reintroducing a
     * wall she cannot see. */
    if (biome == BIOME_FOREST) {
        int k, worst_cover = 256, pale = 0, ctl_pale = 0;

        for (k = 0; k < 2; k++) {
            const ArtSprite *sp = &ART_SPRITES[ts->rock_cliff[k]];
            int cover = 256 - sprite_transparent_px(sp);
            int p = sprite_colour_px(sp, forest_palestone_rgb, 2);
            if (cover < worst_cover) worst_cover = cover;
            pale += p;
            if (p > 0) {
                printf("  cliff cell %d paints %d px of pale highlight - that is"
                       " outcrop body art, not a cliff face\n", k, p);
                fails++;
            }
            if (cover < SOLID_MIN_OBSTACLE_PX) {
                printf("  cliff cell %d covers only %d px of 256 (floor %d) -"
                       " the edge of the map would be a wall she cannot see\n",
                       k, cover, SOLID_MIN_OBSTACLE_PX);
                fails++;
            }
        }
        /* Negative control, on both halves at once. The outcrop BODY - the art
         * the border used to be drawn from - must be rejected by the same pale
         * test, and the sheet's north rim must be rejected by the same coverage
         * floor. Those are the two specific wrong answers available here, so
         * they are the two the check has to be able to refuse. */
        for (k = 0; k < 2; k++)
            ctl_pale += sprite_colour_px(&ART_SPRITES[ts->rock_fill[k]],
                                         forest_palestone_rgb, 2);
        {
            int n_rim = 256 - sprite_transparent_px(&ART_SPRITES[ART_TILE_C4_R3]);
            if (ctl_pale == 0 || n_rim >= SOLID_MIN_OBSTACLE_PX) {
                printf("  cliff negative control FAILED: outcrop body pale px %d"
                       " (want > 0), north rim cover %d (want < %d)\n",
                       ctl_pale, n_rim, SOLID_MIN_OBSTACLE_PX);
                fails++;
            } else {
                printf("tile    : border cliff is dark (%d pale px) and covers %d of 256;"
                       " controls - outcrop body %d pale px, north rim covers %d\n",
                       pale, worst_cover, ctl_pale, n_rim);
            }
        }
    }

    /* The rock fill is an OVERLAY, so it is held to NEARLY opaque rather than
     * fully - but its whole job is to hide the ground inside a rock outcrop, so
     * "nearly" is a real bound and not a shrug. The two cells measure 12 and 11
     * transparent px; the limit is set at twice the worst so a re-bake can move
     * a pixel without tripping it, and every OTHER cell in that block of the
     * sheet is rim art measuring an order of magnitude worse - which is what
     * the control demonstrates. Picking one of those by mistake is the specific
     * error this catches, and it is an easy one to make: they sit adjacent in
     * the sheet and all read as "rock". */
    {
        int k, worst = 0, ctl;
        /* Forest's rock_ring[0] carries real transparency by construction (a
         * corner slice of a hollow-centre ring) and works as its own negative
         * control. Underworld's and Lumiara's water_edge/rock_ring[0] do not -
         * both biomes' curated cells are 16x16 tile-grid crops, verified
         * fully opaque at bake time (see each table's own comment) - so any
         * known-porous DECORATION sprite serves instead; a decoration's
         * interior gaps are exactly that, by construction of Get-OpaqueBox
         * trimming to the silhouette's bounding box rather than its filled
         * area. ART_LUM_STAG's antlers and legs leave plenty of its box empty. */
        int ctl_sprite = (biome == BIOME_FOREST) ? tile_rock_ring[0]
                        : (biome == BIOME_UNDERWORLD) ? ART_UW_CRYSTAL_4 : ART_LUM_STAG;
        for (k = 0; k < 2; k++) {
            int trans = sprite_transparent_px(&ART_SPRITES[ts->rock_fill[k]]);
            if (trans > worst) worst = trans;
            if (trans > ROCK_FILL_MAX_TRANS) {
                printf("  %s rock fill entry %d has %d transparent px (limit %d) -"
                       " an outcrop's interior would show ground through it\n",
                       bname, k, trans, ROCK_FILL_MAX_TRANS);
                fails++;
            }
        }
        ctl = sprite_transparent_px(&ART_SPRITES[ctl_sprite]);
        if (ctl <= ROCK_FILL_MAX_TRANS) {
            printf("  %s rock-fill negative control FAILED: a rim tile measured as"
                   " nearly opaque, so this check cannot reject one\n", bname);
            fails++;
        } else {
            printf("tile    : %s rock fill worst %d transparent px of %d (limit %d);"
                   " a rim tile measures %d\n",
                   bname, worst, TILE * TILE, ROCK_FILL_MAX_TRANS, ctl);
        }
    }

    /* Fill with a colour no palette entry can produce, then require that the
     * ground pass alone leaves none of it visible. */
    gap_colour = SDL_MapRGB(fb->format, 0xFF, 0x00, 0xFF);
    for (y = 0; y < fb->h; y++)
        for (x = 0; x < fb->w; x++)
            ((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] = gap_colour;

    render_world(fb, w, 1, 0, 0);

    {
        int gaps = 0;
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                if (((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] == gap_colour)
                    gaps++;
        if (gaps) {
            printf("  %d of %d px were never written by the ground pass\n",
                   gaps, fb->w * fb->h);
            fails++;
        }
    }

    /* At a camera offset that is not a multiple of TILE, the same must hold -
     * this is where an off-by-one in the visible range shows up, as a one-tile
     * strip of unpainted pixels along the right or bottom edge. */
    {
        int gaps = 0;
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                ((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] = gap_colour;
        render_world(fb, w, 1, 7, 11);
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                if (((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] == gap_colour)
                    gaps++;
        if (gaps) {
            printf("  %d px unpainted at a non-tile-aligned camera offset\n", gaps);
            fails++;
        }
    }

    /* Negative control: a range that stops one tile short MUST leave a gap, or
     * the check above cannot detect the off-by-one it exists to catch. */
    {
        int gaps = 0, tx, ty;
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                ((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] = gap_colour;
        for (ty = 0; ty < fb->h / TILE; ty++)          /* deliberately not +1 */
            for (tx = 0; tx < fb->w / TILE; tx++)
                draw_sprite(fb, ts->grass_base[0], tx * TILE - 7, ty * TILE - 11,
                            FOG_LEVELS - 1);
        for (y = 0; y < fb->h; y++)
            for (x = 0; x < fb->w; x++)
                if (((Uint32 *)((Uint8 *)fb->pixels + y * fb->pitch))[x] == gap_colour)
                    gaps++;
        if (gaps == 0) {
            printf("  %s tile negative control FAILED: a short range left no gap,"
                   " so the coverage check cannot detect one\n", bname);
            fails++;
        } else {
            printf("tile    : %s negative control - a one-tile-short range leaves %d px unpainted\n",
                   bname, gaps);
        }
    }

    /* EVERY BLOCKING TILE MUST DRAW ITSELF.
     *
     * This is the check that was missing, and the shape of its absence is worth
     * keeping: --gating-test already proved every gate boundary had solid tiles
     * along it, and the boundaries were still invisible on screen, because
     * "there is a solid tile here" and "there is something here to see" are
     * different claims and only the first was being made.
     *
     * The rock ring fakes height by drawing its mass low in the cell, so its
     * nine slices cover 12 to 256 pixels of their 256. Collision is the whole
     * square regardless. Wherever a thin slice was selected - the top of every
     * outcrop, and every diagonal step of one - the result was a solid cell
     * standing on open grass, which stops her dead and says nothing, because a
     * wall is not a gate and gate_report has nothing to report.
     *
     * Measured over generated worlds rather than a constructed case: the thin
     * slices appear at particular SHAPES, and a hand-built outcrop would only
     * ever contain the shapes I thought to build. */
    {
        int bad = 0, ctl = 0, worst = 256, s;
        const int SEEDS = 8;

        for (s = 0; s < SEEDS; s++) {
            world_stub(w, (Uint64)(s + 1));
            w->biome = (Uint8)biome;
            for (y = 0; y < WORLD_H; y++)
                for (x = 0; x < WORLD_W; x++) {
                    int px;
                    if (!w->solid[y][x]) continue;
                    px = solid_obstacle_px(w, x, y, 1);
                    if (px < worst) worst = px;
                    if (px < SOLID_MIN_OBSTACLE_PX) bad++;
                    if (solid_obstacle_px(w, x, y, 0) < SOLID_MIN_OBSTACLE_PX) ctl++;
                }
        }
        if (bad) {
            printf("  %s: %d blocking tiles draw less than %d px of obstacle over %d"
                   " seeds - she is stopped by something she cannot see\n",
                   bname, bad, SOLID_MIN_OBSTACLE_PX, SEEDS);
            fails++;
        }
        /* Negative control: the same worlds, scored without the rubble underlay
         * - which is precisely the renderer that shipped the invisible walls.
         * If that does not fail this check, the check cannot detect them. */
        if (ctl == 0) {
            printf("  %s obstacle-visibility negative control FAILED: the un-filled"
                   " renderer left nothing under the threshold\n", bname);
            fails++;
        } else {
            printf("tile    : %s negative control - without the rubble underlay,"
                   " %d blocking tiles drew under %d px\n", bname, ctl, SOLID_MIN_OBSTACLE_PX);
        }
        printf("tile    : %s every blocking tile draws at least %d px of obstacle"
               " (worst %d of 256)\n", bname, SOLID_MIN_OBSTACLE_PX, worst);
    }

    }   /* end for (biome ...) */
    }

    /* Ground-type census, averaged over 8 seeds so no threshold can be tuned to
     * flatter one map.
     *
     * Deliberately NOT compared against the mockups' numbers here, and the
     * distinction matters: the mockup figures (grass 39-43%, olive 9-10%, dirt
     * 4-6.5%, water 11-12%) are counts of VISIBLE PIXELS, and about 16% of
     * those pixels are tree canopy covering ground that is still grass
     * underneath. A ground-type census and a visible-pixel census are different
     * measurements, and holding this one to those bands would drive the
     * thresholds to the wrong place.
     *
     * The comparable check is a pixel census of a RENDERED frame, which only
     * becomes meaningful once props exist - that is --mockup-test, in phase 4.
     * Reported here so the generator's drift is visible in the meantime. */
    {
        static const char *gname[GT_COUNT] = { "grass", "olive", "dirt", "water", "rock" };
        int counts[GT_COUNT], t, s;
        long rock_tiles = 0, enclosed = 0, lone = 0;
        const int SEEDS = 8;

        for (t = 0; t < GT_COUNT; t++) counts[t] = 0;
        for (s = 0; s < SEEDS; s++) {
            world_stub(w, (Uint64)(s + 1));
            for (y = 0; y < WORLD_H; y++)
                for (x = 0; x < WORLD_W; x++) {
                    counts[w->terr[y][x]]++;
                    if (w->terr[y][x] != GT_ROCK) continue;
                    rock_tiles++;
                    if (blob_interior(w, x, y, GT_ROCK)) enclosed++;
                    /* An outcrop cell with no rock neighbour at all. The rock
                     * ring has no art for "one tile of stone" - it would draw
                     * the blob's top-left CORNER, 12 px of rim, over a full
                     * rubble square - so these read as a solid block with no
                     * edge, which is exactly the defect the pond's 2x2 rule
                     * exists to prevent for water. Counted here because
                     * anything that thins the rock field trades outcrop size
                     * against this, and the trade should be visible. */
                    if (!(terr_at(w, x - 1, y) == GT_ROCK || terr_at(w, x + 1, y) == GT_ROCK ||
                          terr_at(w, x, y - 1) == GT_ROCK || terr_at(w, x, y + 1) == GT_ROCK))
                        lone++;
                }
        }
        printf("tile    : ground types over %d seeds:", SEEDS);
        for (t = 0; t < GT_COUNT; t++)
            printf(" %s %.1f%%", gname[t],
                   100.0 * counts[t] / (double)(WORLD_W * WORLD_H * SEEDS));
        printf("\n");

        /* Outcrop SHAPE, held between two bounds. Both exist because a rejected
         * experiment crossed them - see the note above world_stub - and they
         * are what let the next attempt at ridge-shaped rock be judged in one
         * run instead of six.
         *
         * ENCLOSED is a floor. A cell with rock on all four sides is the only
         * place prop_at will stand a free-standing boulder, and PROP_ROCK /
         * PROP_STONE on GT_ROCK are the only callers of the skirtless
         * ART_ROCK_C / ART_STONE_C. Thin the rock field too far and those three
         * sprites quietly become dead shipped bytes with nothing to say so.
         *
         * LONE is a ceiling, and the sharper of the two. An outcrop tile with
         * no rock neighbour draws the ring's 12 px top-left corner over a full
         * rubble square: a solid block with no edge, standing on open grass.
         * Measured at 0.9% for the shipped field and 8.9% for the band that was
         * tried, so the limit sits between them, nearer the good end. */
        if (rock_tiles == 0) {
            printf("  the census generated no rock at all\n");
            fails++;
        } else if (enclosed * 50 < rock_tiles) {       /* under 2% */
            printf("  only %ld of %ld rock tiles are fully enclosed (%.1f%%) -"
                   " too few to place ART_ROCK_C on\n",
                   enclosed, rock_tiles, 100.0 * (double)enclosed / (double)rock_tiles);
            fails++;
        } else if (lone * 25 > rock_tiles) {           /* over 4% */
            printf("  %ld of %ld rock tiles are lone (%.1f%%) - each draws as a"
                   " solid square with no rim on open grass\n",
                   lone, rock_tiles, 100.0 * (double)lone / (double)rock_tiles);
            fails++;
        } else {
            printf("tile    : %ld rock tiles: %.1f%% enclosed (floor 2%%),"
                   " %.1f%% lone (ceiling 4%%)\n", rock_tiles,
                   100.0 * (double)enclosed / (double)rock_tiles,
                   100.0 * (double)lone / (double)rock_tiles);
        }
    }

    SDL_free(w);
    SDL_FreeSurface(fb);
    printf("tile    : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* The negative control's blend for fog_selftest: reaches the same haze but
 * swaps red and green on the way, so it carries no luminance guarantee at all.
 * Exists only to be rejected. */
#define FOG_LUM_EPS 1.35f   /* measured; see fog_selftest */

static Uint32 fog_lerp_hue_swap(SDL_Surface *s, int r, int gr, int b, float reveal)
{
    float lum = 0.299f * (float)r + 0.587f * (float)gr + 0.114f * (float)b;
    float fr = FOG_TINT_R + (lum - FOG_TINT_R) * FOG_KEEP;
    float fg = FOG_TINT_G + (lum - FOG_TINT_G) * FOG_KEEP;
    float fb = FOG_TINT_B + (lum - FOG_TINT_B) * FOG_KEEP;

    /* Eased identically to fog_lerp, so the ONE thing this control varies is
     * the hue swap - a second difference would leave it unclear which of them
     * the inversions it produces are actually testing for. */
    reveal = fog_ease(reveal);
    /* Note the swap: green's target gets red's source and vice versa. Rounded
     * like the real one, for the same reason the ease is shared. */
    return SDL_MapRGB(s->format,
                      (Uint8)(fr + ((float)gr - fr) * reveal + 0.5f),
                      (Uint8)(fg + ((float)r  - fg) * reveal + 0.5f),
                      (Uint8)(fb + ((float)b  - fb) * reveal + 0.5f));
}

static int fog_selftest(void)
{
    SDL_Surface *fb = test_surface(8, 8);
    int fails = 0, lv, k, inversions = 0, checked = 0;
    float worst_gap = 0.0f;

    if (!fb) {
        printf("fog     : could not create a test surface\n");
        return 1;
    }
    if (!fogpal && !fogpal_build(fb)) {
        printf("fog     : could not build the fog palette\n");
        SDL_FreeSurface(fb);
        return 1;
    }

    /* ---- Restoration must reach the tiles she cannot walk on ---------------
     *
     * A restored area brings its colour back region by region, and restoration
     * is looked up per tile. Water and rock are solid, so they are REGION_NONE
     * in the collision partition and used to get NO restoration at all: a
     * finished area kept grey ponds and a grey map edge in the middle of fully
     * restored grass. World.litreg exists to answer this question instead, and
     * these three checks are what hold it to that.
     *
     * Run against a REAL generated world rather than a hand-built one - the
     * property is about the shape ponds and outcrops actually take, and a
     * fixture would only prove the BFS works on the fixture. */
    {
        World *w = (World *)SDL_malloc(sizeof(World));
        Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
        int s, relabelled = 0, unlit = 0, gaps = 0, solid_gaps = 0;
        int dim_new = 0, dim_old = 0, worlds = 0, collision_moved = 0;

        if (!w || !sc) {
            printf("  fog: out of memory for the restoration-lighting check\n");
            fails++;
        } else {
            for (s = 1; s <= 6; s++) {
                int x, y;
                Uint8 solid_before[WORLD_H][WORLD_W];
                Uint8 region_before[WORLD_H][WORLD_W];

                world_stub(w, (Uint64)s);
                world_spawn(w, sc);
                regions_build(w, sc);
                if (w->region_count <= 0) continue;
                worlds++;
                /* Sight contributes nothing, so what is measured below is the
                 * restoration channel ALONE - which is the channel that was
                 * broken and the one --lit was hiding. */
                SDL_memset(w->reveal, 0, sizeof w->reveal);
                for (k = 0; k < w->region_count; k++)
                    w->regions[k].restoration = 1.0f;

                SDL_memcpy(solid_before, w->solid, sizeof solid_before);
                SDL_memcpy(region_before, w->region, sizeof region_before);

                for (y = 0; y < WORLD_H; y++) {
                    for (x = 0; x < WORLD_W; x++) {
                        Uint8 rg = w->region[y][x], lr = w->litreg[y][x];
                        int old_restored, old_level;
                        /* (a) an owned tile must keep its own label: this fills
                         * gaps, it does not repartition the map. */
                        if (rg != REGION_NONE && lr != rg) relabelled++;
                        /* (a) and every tile must end up lit by something. */
                        if (lr == REGION_NONE) unlit++;
                        /* Negative control input: how much the OLD field left
                         * unanswered, and how much of that was solid. */
                        if (rg == REGION_NONE) {
                            gaps++;
                            if (w->solid[y][x]) solid_gaps++;
                        }
                        /* (b) behavioural, on the real function. */
                        if (tile_level(w, x, y) != FOG_LEVELS - 1) dim_new++;
                        /* ...against exactly what tile_level used to compute. */
                        old_restored = (rg == REGION_NONE) ? 0
                                     : (int)(w->regions[rg].restoration * 255.0f);
                        old_level = old_restored >> 3;
                        if (old_level != FOG_LEVELS - 1 && w->solid[y][x]) dim_old++;
                    }
                }
                /* (c) RENDER-ONLY, asserted rather than promised: re-running the
                 * lighting pass must not move one byte of what decides
                 * collision. */
                regions_light(w, sc);
                if (SDL_memcmp(solid_before, w->solid, sizeof solid_before) != 0 ||
                    SDL_memcmp(region_before, w->region, sizeof region_before) != 0)
                    collision_moved++;
            }

            if (worlds == 0) {
                printf("  fog: no world generated a partition, so the"
                       " restoration-lighting check proved nothing\n");
                fails++;
            }
            if (relabelled) {
                printf("  %d owned tile(s) had their region relabelled by the"
                       " lighting pass\n", relabelled);
                fails++;
            }
            if (unlit) {
                printf("  %d tile(s) have no lighting region, so restoration"
                       " can never reach them\n", unlit);
                fails++;
            }
            if (dim_new) {
                printf("  %d tile(s) stay below full colour in a fully restored"
                       " world\n", dim_new);
                fails++;
            }
            if (collision_moved) {
                printf("  the lighting pass moved solid[][] or region[][] in %d"
                       " world(s) - it is not render-only\n", collision_moved);
                fails++;
            }
            /* Negative control, and the specific one that matters: the OLD rule
             * must be caught failing, on solid tiles, by the same measurement.
             * If it is not, this test would have passed against the bug. */
            if (dim_old == 0 || solid_gaps == 0) {
                printf("  restoration-lighting negative control FAILED: reading"
                       " region[][] left %d solid tiles dim and %d unlabelled -"
                       " both must be non-zero\n", dim_old, solid_gaps);
                fails++;
            } else {
                printf("fog     : negative control - the old region[][] rule leaves"
                       " %d solid tiles dim in a restored world (%d of %d unlabelled tiles"
                       " are solid)\n", dim_old, solid_gaps, gaps);
            }
            /* Reported only when the checks above actually held. A summary line
             * that prints beside its own failure is worse than no summary: the
             * mutation run that proved this test bites also printed "every tile
             * reaches level 31" directly under the line saying 15480 did not. */
            if (!fails)
                printf("fog     : %d worlds fully restored: every tile reaches level %d,"
                       " no owned tile relabelled, collision untouched\n",
                       worlds, FOG_LEVELS - 1);
        }
        SDL_free(w);
        SDL_free(sc);
    }

    for (lv = 0; lv < FOG_LEVELS; lv++) {
        float rev = (float)lv / (float)(FOG_LEVELS - 1);
        for (k = 0; k < ART_PAL_N; k++) {
            const unsigned char *c = &ART_PAL[k * 3];
            Uint32 want = fog_lerp(fb, c[0], c[1], c[2], rev);
            if (FOGPAL(lv, k + 1) != want) {
                printf("  LUT level %d index %d: %08X != fog_lerp %08X\n",
                       lv, k + 1, FOGPAL(lv, k + 1), want);
                fails++;
                lv = FOG_LEVELS; break;
            }
        }
    }

    /* Full reveal must reproduce the palette exactly, or everything is
     * permanently slightly washed out and the art gets blamed. */
    for (k = 0; k < ART_PAL_N; k++) {
        const unsigned char *c = &ART_PAL[k * 3];
        Uint32 want = SDL_MapRGB(fb->format, c[0], c[1], c[2]);
        if (FOGPAL(FOG_LEVELS - 1, k + 1) != want) {
            printf("  level %d index %d does not reproduce the true colour\n", FOG_LEVELS - 1, k + 1);
            fails++;
            break;
        }
    }

    /* Value hierarchy, over every pair of palette colours at every level.
     *
     * The bar is MEASURED rather than chosen, and re-measured here after the
     * Underworld palette landed: across all 32 levels the worst luminance gap
     * that inverts is 1.304, between the portal's teal #3D6E70 (luminance
     * 110.46) and one of the Dead_arm decoration's olive-grey #5D6250
     * (111.77) - unrelated sprites that simply happen to land close in
     * luminance once the palette grew past 65 colours. Nothing at 1.35 or
     * above inverts. Pairs that close are indistinguishable on screen, so
     * requiring them to keep their order would be demanding that 8-bit
     * rounding preserve a difference it cannot represent.
     *
     * worst_gap is printed unconditionally, so if a future palette pushes an
     * inversion toward the threshold the number moves visibly BEFORE the test
     * flips to red. */
    for (lv = 0; lv < FOG_LEVELS; lv++) {
        int a, b;   /* the LUT is read directly here, so no reveal float is needed */
        for (a = 0; a < ART_PAL_N; a++) {
            for (b = a + 1; b < ART_PAL_N; b++) {
                const unsigned char *ca = &ART_PAL[a * 3], *cb = &ART_PAL[b * 3];
                float la = 0.299f*ca[0] + 0.587f*ca[1] + 0.114f*ca[2];
                float lb = 0.299f*cb[0] + 0.587f*cb[1] + 0.114f*cb[2];
                Uint8 r1, g1, b1, r2, g2, b2;
                float fa, fbb, gap;
                if (la == lb) continue;
                gap = la > lb ? la - lb : lb - la;
                SDL_GetRGB(FOGPAL(lv, a + 1), fb->format, &r1, &g1, &b1);
                SDL_GetRGB(FOGPAL(lv, b + 1), fb->format, &r2, &g2, &b2);
                fa  = 0.299f*r1 + 0.587f*g1 + 0.114f*b1;
                fbb = 0.299f*r2 + 0.587f*g2 + 0.114f*b2;
                /* Equal-after-rounding is fine; ORDER REVERSAL is not. */
                if ((la < lb && fa > fbb) || (la > lb && fa < fbb)) {
                    if (gap > worst_gap) worst_gap = gap;
                    if (gap >= FOG_LUM_EPS) {
                        inversions++;
                        if (inversions == 1)
                            printf("  level %d inverts #%02X%02X%02X vs #%02X%02X%02X"
                                   " (luminance gap %.2f)\n",
                                   lv, ca[0], ca[1], ca[2], cb[0], cb[1], cb[2], gap);
                    }
                }
                if (gap >= FOG_LUM_EPS) checked++;
            }
        }
    }
    if (inversions) {
        printf("  value hierarchy: %d of %d separable pairs invert under fog\n",
               inversions, checked);
        fails++;
    }

    /* Negative control. The earlier version of this control merely counted
     * palette pairs with unequal luminance, which is a tautology - it exercised
     * none of the checking above and would have passed against any blend at all.
     * This one runs the REAL check against a deliberately hue-rotating blend
     * that carries no luminance guarantee. If that does not produce inversions
     * well above the threshold, the check cannot detect a broken fog function. */
    {
        int bad_inv = 0, a, b;
        for (a = 0; a < ART_PAL_N; a++) {
            for (b = a + 1; b < ART_PAL_N; b++) {
                const unsigned char *ca = &ART_PAL[a * 3], *cb = &ART_PAL[b * 3];
                float la = 0.299f*ca[0] + 0.587f*ca[1] + 0.114f*ca[2];
                float lb = 0.299f*cb[0] + 0.587f*cb[1] + 0.114f*cb[2];
                Uint8 r1, g1, b1, r2, g2, b2;
                float fa, fbb, gap;
                if (la == lb) continue;
                gap = la > lb ? la - lb : lb - la;
                if (gap < FOG_LUM_EPS) continue;
                /* Half reveal, where a bad blend does the most damage. */
                SDL_GetRGB(fog_lerp_hue_swap(fb, ca[0], ca[1], ca[2], 0.5f),
                           fb->format, &r1, &g1, &b1);
                SDL_GetRGB(fog_lerp_hue_swap(fb, cb[0], cb[1], cb[2], 0.5f),
                           fb->format, &r2, &g2, &b2);
                fa  = 0.299f*r1 + 0.587f*g1 + 0.114f*b1;
                fbb = 0.299f*r2 + 0.587f*g2 + 0.114f*b2;
                if ((la < lb && fa > fbb) || (la > lb && fa < fbb))
                    bad_inv++;
            }
        }
        if (bad_inv == 0) {
            printf("  fog negative control FAILED: a hue-rotating blend inverted"
                   " nothing, so this check cannot detect a broken fog function\n");
            fails++;
        } else {
            printf("fog     : negative control - hue-rotating blend inverts %d separable pairs\n",
                   bad_inv);
        }
    }

    printf("fog     : LUT matches fog_lerp at %d levels; %d separable pairs keep their order\n",
           FOG_LEVELS, checked);
    printf("fog     : worst inverting luminance gap %.3f (threshold %.2f)\n",
           worst_gap, (double)FOG_LUM_EPS);
    printf("fog     : %s\n", fails ? "FAIL" : "PASS");
    SDL_FreeSurface(fb);
    return fails;
}

/* ---- --font-test --------------------------------------------------------
 *
 * The font is the only art in the build that is NOT baked and NOT checked by
 * --sprite-test, so nothing else would notice a shifted row or a glyph that is
 * silently blank. Three properties: every character a HUD string can contain
 * renders something, rendering lights exactly the pixels the table declares,
 * and a character outside the range draws nothing at all rather than reading
 * off the end of the array. */
/* Lit pixels inside a box. A box rather than the whole surface because the HUD
 * test needs to look at the counters WITHOUT the minimap beside them - a
 * whole-surface count there would be dominated by 4,000 minimap pixels and
 * would pass whether or not the counters had rendered at all. */
static int count_lit(const SDL_Surface *s, int bx, int by, int bw, int bh)
{
    int x, y, n = 0;
    for (y = by; y < by + bh && y < s->h; y++) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch);
        for (x = bx; x < bx + bw && x < s->w; x++)
            if ((row[x] & 0x00FFFFFFu) != 0) n++;
    }
    return n;
}

static int font_bits(int ch, int stride)
{
    int idx = ch - FONT_FIRST, row, col, n = 0;
    if (idx < 0 || idx >= FONT_GLYPHS) return 0;
    for (row = 0; row < FONT_H; row++)
        for (col = 0; col < FONT_W; col++)
            if (FONT_5X7[idx * stride + row] & (1u << (FONT_W - 1 - col))) n++;
    return n;
}

static int font_selftest(void)
{
    /* Every character the shipping HUD strings can contain. If a string ever
     * grows a character that is not here, this list is where to add it - and
     * the blank-glyph check below is what would otherwise let it ship blank. */
    static const char *needed =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/.,:-";
    SDL_Surface *fb = test_surface(320, 32);
    Uint32 white;
    int fails = 0, i, blank = 0;

    if (!fb) { printf("FAIL  font: no surface\n"); return 1; }
    white = SDL_MapRGB(fb->format, 0xff, 0xff, 0xff);

    for (i = 0; needed[i]; i++)
        if (font_bits((unsigned char)needed[i], FONT_STRIDE) == 0) {
            printf("FAIL  font: '%c' (0x%02X) is blank\n", needed[i],
                   (unsigned)(unsigned char)needed[i]);
            blank++;
        }
    if (blank) fails++;
    printf("font    : %d required glyphs, %d blank\n", i, blank);

    /* Rendering lights exactly what the table declares. draw_text uses
     * FONT_SCALE px per font pixel, and no glyph in this string overlaps its
     * neighbour, so the count is exact rather than approximate. */
    {
        const char *s = "fragments 3/7";
        int want = 0;
        for (i = 0; s[i]; i++) want += font_bits((unsigned char)s[i], FONT_STRIDE);
        want *= FONT_SCALE * FONT_SCALE;
        SDL_FillRect(fb, NULL, 0);
        draw_text(fb, 2, 2, s, white);
        {
            int got = count_lit(fb, 0, 0, fb->w, fb->h);
            printf("font    : \"%s\" lit %d px, table says %d\n", s, got, want);
            if (got != want) {
                printf("FAIL  font: rendered pixel count disagrees with the table\n");
                fails++;
            }

            /* NEGATIVE CONTROL. Misread the glyph table with a wrong stride -
             * exactly the bug a flat array invites - and the count must change.
             * If it did not, the check above would be measuring nothing. */
            SDL_FillRect(fb, NULL, 0);
            for (i = 0; s[i]; i++)
                draw_glyph(fb, 2 + i * FONT_ADV, 2, (unsigned char)s[i], white,
                           FONT_STRIDE + 1, FONT_SCALE);
            {
                int bad = count_lit(fb, 0, 0, fb->w, fb->h);
                if (bad == got) {
                    printf("FAIL  font: negative control (stride %d) rendered the "
                           "same %d px - the checker proves nothing\n",
                           FONT_STRIDE + 1, bad);
                    fails++;
                } else {
                    printf("font    : negative control PASS (stride %d -> %d px, not %d)\n",
                           FONT_STRIDE + 1, bad, got);
                }
            }
        }
    }

    /* Out of range draws nothing rather than indexing past the table. */
    SDL_FillRect(fb, NULL, 0);
    draw_glyph(fb, 4, 4, 0x1F, white, FONT_STRIDE, FONT_SCALE);          /* below FONT_FIRST */
    draw_glyph(fb, 12, 4, FONT_LAST + 1, white, FONT_STRIDE, FONT_SCALE); /* above FONT_LAST */
    if (count_lit(fb, 0, 0, fb->w, fb->h) != 0) {
        printf("FAIL  font: an out-of-range character drew pixels\n");
        fails++;
    } else {
        printf("font    : out-of-range characters draw nothing\n");
    }

    printf("font    : %s\n", fails ? "FAIL" : "PASS");
    SDL_FreeSurface(fb);
    return fails;
}

/* ---- --map-test ---------------------------------------------------------
 *
 * The map fragment and the screen it unlocks, checked where each claim can
 * actually fail: WHERE the fragment lands, whether anything buries it, whether
 * the key that takes it agrees with the keycap that advertised it, and whether
 * the trails the screen draws are routes she could really walk.
 *
 * The trail checks are the load-bearing ones. A trail is a promise about the
 * world - "go this way and you will get there" - and it is drawn from a
 * distance field, so it can be wrong in exactly the way a picture cannot
 * argue back about: a chain of dots straight through a pond bank looks like a
 * route. Every step is therefore checked against tile_blocked, the same
 * function that stops her.
 */

/* Walk a BFS field back the way bm_trail walks it, checking each step instead
 * of drawing it. Returns the number of steps, or -1 if the route is not one
 * she could walk. Deliberately re-derives nothing: it re-uses the same field
 * bm_redraw hands bm_trail, so a broken field fails here too. */
static int trail_walk_ok(const World *w, Uint8 abilities, const int *dist,
                         int from, const char **why)
{
    static const int dxs[4] = { 1, -1, 0, 0 };
    static const int dys[4] = { 0, 0, 1, -1 };
    int cur = from, steps = 0, guard = WORLD_W * WORLD_H;

    *why = "";
    if (from < 0 || dist[from] < 0) { *why = "no route"; return -1; }
    if (tile_blocked(w, abilities, from % WORLD_W, from / WORLD_W)) {
        *why = "starts inside a wall"; return -1;
    }
    while (dist[cur] > 0 && guard-- > 0) {
        int x = cur % WORLD_W, y = cur / WORLD_W, d, next = -1;
        for (d = 0; d < 4; d++) {
            int nx = x + dxs[d], ny = y + dys[d], ni;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H) continue;
            ni = ny * WORLD_W + nx;
            if (dist[ni] == dist[cur] - 1) { next = ni; break; }
        }
        if (next < 0) { *why = "dead end short of the source"; return -1; }
        /* The two properties a drawn trail claims and could silently break. */
        if (dist[next] != dist[cur] - 1) { *why = "step did not close the distance"; return -1; }
        if (tile_blocked(w, abilities, next % WORLD_W, next / WORLD_W)) {
            *why = "route crosses a tile she cannot enter"; return -1;
        }
        cur = next;
        steps++;
    }
    if (dist[cur] != 0) { *why = "never reached the source"; return -1; }
    return steps;
}

/* Pixels of EXACTLY one colour in a box. count_lit cannot serve here: the map
 * screen paints its own opaque background, so "not black" is true of every
 * pixel in the frame and would report the same number whatever the trails did.
 * Exact-colour counting is what makes "a trail reached the screen" a claim
 * about the trail rather than about the backdrop. */
static int count_col(const SDL_Surface *s, Uint32 col, int bx, int by, int bw, int bh)
{
    int x, y, n = 0;

    if (bx < 0) { bw += bx; bx = 0; }
    if (by < 0) { bh += by; by = 0; }
    for (y = by; y < by + bh && y < s->h; y++) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch);
        for (x = bx; x < bx + bw && x < s->w; x++)
            if ((row[x] & 0x00FFFFFFu) == (col & 0x00FFFFFFu)) n++;
    }
    return n;
}

/* Does any sprite drawn AFTER the item at `at` cover the box at (bx,by,bw,bh)?
 * The visible property, asked of the finished draw list rather than of the
 * placement rule - which is the only way to catch a chart that is placed
 * perfectly and then buried, the bug this whole check exists because of. */
static int box_buried_after(const DrawList *dl, int at, int bx, int by, int bw, int bh)
{
    int k;

    for (k = at + 1; k < dl->n; k++) {
        const ArtSprite *sp;
        int x0, y0;
        if (dl->item[k].kind != DI_SPRITE) continue;
        sp = &ART_SPRITES[dl->item[k].art];
        x0 = dl->item[k].x - sp->anchor_x;
        y0 = dl->item[k].y - sp->anchor_y;
        if (x0 < bx + bw && x0 + sp->w > bx && y0 < by + bh && y0 + sp->h > by)
            return 1;
    }
    return 0;
}

static int map_selftest(int seeds, Uint64 base)
{
    Game *g = (Game *)SDL_malloc(sizeof(Game));
    Game *b = (Game *)SDL_malloc(sizeof(Game));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    DrawList *dl = (DrawList *)SDL_malloc(sizeof(DrawList));
    SDL_Surface *fb = test_surface(LOGICAL_W, LOGICAL_H);
    Audio audio;
    int fails = 0, s_i, area, i;

    if (!g || !b || !sc || !dl || !fb) {
        printf("FAIL  map: out of memory\n");
        SDL_free(g); SDL_free(b); SDL_free(sc); SDL_free(dl);
        if (fb) SDL_FreeSurface(fb);
        return 1;
    }
    if (!fogpal_build(fb)) {
        printf("FAIL  map: fog palette\n");
        SDL_free(g); SDL_free(b); SDL_free(sc); SDL_free(dl);
        SDL_FreeSurface(fb);
        return 1;
    }
    SDL_zero(audio);

    /* ---- Placement, over every seed and every area ---------------------- */
    for (s_i = 0; s_i < seeds; s_i++) {
        Uint64 seed = base + (Uint64)s_i;
        for (area = 1; area <= 3; area++) {
            Uint32 reach;
            int t, far_exists = 0, x, y;

            g->restored = 0;
            g->maps = 0;
            game_init_area(g, sc, seed, (Uint8)area);
            t = g->w.map_tile;
            reach = regions_reachable(&g->w, ABIL_NONE);
            if (reach == 0) reach = regions_reachable(&g->w, ABIL_ALL);

            if (t < 0) {
                printf("FAIL  map: seed %.0f area %d placed no map fragment\n",
                       (double)seed, area);
                fails++;
                continue;
            }
            /* The placer's own predicate, asked of what the placer chose. */
            if (!map_tile_ok(&g->w, g->ents, reach, t, 0)) {
                printf("FAIL  map: seed %.0f area %d put the fragment on an illegal"
                       " tile %d\n", (double)seed, area, t);
                fails++;
            }
            /* Reachable with NO abilities, checked independently of the region
             * bitmask the placer used - a walk, not a graph lookup. */
            bfs_gated(&g->w, ABIL_NONE, g->w.spawn_tile, sc->dist, sc->queue);
            if (sc->dist[t] < 0) {
                printf("FAIL  map: seed %.0f area %d sealed the map fragment behind"
                       " a gate she has nothing for\n", (double)seed, area);
                fails++;
            }
            /* The spawn-distance clause is a preference the placer may drop -
             * but only where nothing legal lay far enough out. */
            for (y = 0; y < WORLD_H && !far_exists; y++)
                for (x = 0; x < WORLD_W && !far_exists; x++)
                    if (map_tile_ok(&g->w, g->ents, reach, y * WORLD_W + x, 1))
                        far_exists = 1;
            if (far_exists && !map_tile_ok(&g->w, g->ents, reach, t, 1)) {
                printf("FAIL  map: seed %.0f area %d dropped the spawn-distance"
                       " clause with %d legal far tiles available\n",
                       (double)seed, area, far_exists);
                fails++;
            }
            /* Same seed, same fragment: it is drawn from its own rng stream, so
             * this must not depend on anything else the generator did. */
            b->restored = 0;
            b->maps = 0;
            game_init_area(b, sc, seed, (Uint8)area);
            if (b->w.map_tile != t) {
                printf("FAIL  map: seed %.0f area %d is not deterministic (%d then"
                       " %d)\n", (double)seed, area, t, b->w.map_tile);
                fails++;
            }
        }
    }
    printf("map     : %d seeds x 3 areas - placed, reachable with no abilities,"
           " clear of motes, deterministic\n", seeds);

    /* ---- NEGATIVE CONTROL for the placement predicate ------------------- */
    {
        Uint32 reach;
        int caught = 0, wall = -1, near_ent = -1, x, y;

        g->restored = 0; g->maps = 0;
        game_init_area(g, sc, base, 1);
        reach = regions_reachable(&g->w, ABIL_NONE);

        for (y = 0; y < WORLD_H && wall < 0; y++)
            for (x = 0; x < WORLD_W && wall < 0; x++)
                if (g->w.solid[y][x]) wall = y * WORLD_W + x;
        for (i = 0; i < ENTITY_COUNT && near_ent < 0; i++)
            if (g->ents[i].tile >= 0) near_ent = g->ents[i].tile;

        if (wall >= 0 && !map_tile_ok(&g->w, g->ents, reach, wall, 0)) caught++;
        else printf("  map control MISS: a solid tile was accepted\n");
        if (!map_tile_ok(&g->w, g->ents, reach, g->w.spawn_tile, 0)) caught++;
        else printf("  map control MISS: the spawn tile was accepted\n");
        if (near_ent >= 0 && !map_tile_ok(&g->w, g->ents, reach, near_ent, 0)) caught++;
        else printf("  map control MISS: an entity's own tile was accepted\n");
        if (!map_tile_ok(&g->w, g->ents, reach, -1, 0)) caught++;
        else printf("  map control MISS: tile -1 was accepted\n");
        /* An empty reach mask makes every tile illegal - the clause that keeps
         * the fragment inside what she can walk to. */
        if (!map_tile_ok(&g->w, g->ents, 0u, g->w.map_tile, 0)) caught++;
        else printf("  map control MISS: an unreachable region was accepted\n");

        if (caught != 5) {
            printf("FAIL  map: the placement predicate rejected only %d of 5"
                   " deliberately illegal tiles\n", caught);
            fails++;
        } else {
            printf("map     : negative control - the predicate rejects all 5"
                   " illegal tiles\n");
        }
    }

    /* ---- Nothing buries the chart --------------------------------------- */
    {
        int cam_x, cam_y, at = -1, ctl_hits = 0, tx, ty;

        g->restored = 0; g->maps = 0;
        game_init_area(g, sc, base, 1);
        reveal_all(&g->w);
        g->p.x = (float)(g->w.map_tile % WORLD_W) * TILE + TILE * 0.5f;
        g->p.y = (float)(g->w.map_tile / WORLD_W) * TILE + TILE * 0.5f;
        cam_x = 0; cam_y = 0;
        camera_follow(g->p.x, g->p.y, LOGICAL_W, LOGICAL_H, &cam_x, &cam_y);
        props_build(LOGICAL_W, LOGICAL_H, &g->w, base, cam_x, cam_y, g->ents,
                    &g->p, 0.0f, dl);
        for (i = 0; i < dl->n; i++)
            if (dl->item[i].kind == DI_MAP) { at = i; break; }
        if (at < 0) {
            printf("FAIL  map: the chart is not in the draw list with the camera"
                   " on it\n");
            fails++;
        } else if (box_buried_after(dl, at, dl->item[at].x - MAP_MOTE_W / 2 - 1,
                                    dl->item[at].y - MAP_MOTE_H - 1,
                                    MAP_MOTE_W + 2, MAP_MOTE_H + 2)) {
            printf("FAIL  map: a sprite drawn after the chart covers it - the"
                   " fragment is invisible where it lies\n");
            fails++;
        } else {
            printf("map     : no sprite drawn after the chart overlaps it\n");
        }
        /* NEGATIVE CONTROL for that checker. box_buried_after has to be capable
         * of saying yes, or "nothing buries the chart" only means "this
         * function returns 0". Swept over the same frame at the FRONT of the
         * list, where by construction almost everything draws later. */
        for (ty = cam_y / TILE; ty <= (cam_y + LOGICAL_H) / TILE && ctl_hits == 0; ty++)
            for (tx = cam_x / TILE; tx <= (cam_x + LOGICAL_W) / TILE && ctl_hits == 0; tx++) {
                if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H) continue;
                if (box_buried_after(dl, -1, tx * TILE - cam_x, ty * TILE - cam_y,
                                     MAP_MOTE_W + 2, MAP_MOTE_H + 2))
                    ctl_hits++;
            }
        if (!ctl_hits) {
            printf("  map control MISS: box_buried_after found nothing covered"
                   " anywhere in a full frame of forest\n");
            fails++;
        } else {
            printf("map     : negative control - the same checker does report"
                   " a covered box elsewhere in the frame\n");
        }
    }

    /* ---- Taking it, and the keycap that advertised it -------------------- */
    {
        int t;

        g->restored = 0; g->maps = 0;
        game_init_area(g, sc, base, 1);
        t = g->w.map_tile;

        /* Out of reach: three tiles away is well past INTERACT_RADIUS. */
        g->p.x = (float)(t % WORLD_W) * TILE + TILE * 0.5f + 3.0f * TILE;
        g->p.y = (float)(t / WORLD_W) * TILE + TILE * 0.5f;
        if (map_in_reach(&g->w, g->p.x, g->p.y) || try_take_map(g, &audio)) {
            printf("FAIL  map: the fragment was taken from three tiles away\n");
            fails++;
        } else if (g->maps != 0 || g->w.map_tile != t) {
            printf("FAIL  map: a refused pickup still changed the world\n");
            fails++;
        } else {
            printf("map     : negative control - out of reach takes nothing and"
                   " changes nothing\n");
        }

        /* Standing on it. */
        g->p.x = (float)(t % WORLD_W) * TILE + TILE * 0.5f;
        g->p.y = (float)(t / WORLD_W) * TILE + TILE * 0.5f;
        if (!map_in_reach(&g->w, g->p.x, g->p.y)) {
            printf("FAIL  map: standing on the fragment is not in reach\n");
            fails++;
        }
        if (!try_take_map(g, &audio)) {
            printf("FAIL  map: standing on the fragment did not take it\n");
            fails++;
        } else if (!game_has_map(g) || g->w.map_tile >= 0) {
            printf("FAIL  map: taking it left maps %d map_tile %d\n",
                   g->maps, g->w.map_tile);
            fails++;
        } else if (try_take_map(g, &audio) || map_in_reach(&g->w, g->p.x, g->p.y)) {
            printf("FAIL  map: the fragment could be taken twice\n");
            fails++;
        } else {
            printf("map     : taken once, exactly once, and only in reach\n");
        }

        /* The keycap and the key resolve the same thing. With a mote moved onto
         * the fragment's own tile, the mote must win BOTH - that is the order
         * prompt_draw draws and the order the interact key acts in. */
        g->restored = 0; g->maps = 0;
        game_init_area(g, sc, base, 1);
        t = g->w.map_tile;
        g->ents[0].tile = t;
        g->ents[0].restored = 0;
        g->p.x = (float)(t % WORLD_W) * TILE + TILE * 0.5f;
        g->p.y = (float)(t / WORLD_W) * TILE + TILE * 0.5f;
        if (entity_in_reach(&g->w, g->ents, g->p.x, g->p.y) != 0) {
            printf("FAIL  map: the mote on the shared tile was not the one in"
                   " reach\n");
            fails++;
        } else if (try_interact(g, &audio) != 0 || g->w.map_tile != t) {
            printf("FAIL  map: the interact key took the chart out from under a"
                   " mote sharing its tile\n");
            fails++;
        } else {
            printf("map     : mote outranks chart on the shared key, as the"
                   " prompt draws it\n");
        }
    }

    /* ---- The trails ------------------------------------------------------ */
    {
        int checked = 0, closed_none = 0;

        for (s_i = 0; s_i < seeds; s_i++) {
            Uint64 seed = base + (Uint64)s_i;
            g->restored = 0; g->maps = 0;
            game_init_area(g, sc, seed, 1);

            /* With everything held, every placed collectible must have a route
             * she could really walk - world_solvable already proves the world
             * is finishable, so a missing route here is the MAP being wrong,
             * not the world. */
            bfs_gated(&g->w, ABIL_ALL,
                      (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE),
                      sc->dist, sc->queue);
            for (i = 0; i < ENTITY_COUNT; i++) {
                const char *why;
                if (g->ents[i].tile < 0) continue;
                if (trail_walk_ok(&g->w, ABIL_ALL, sc->dist, g->ents[i].tile, &why) < 0) {
                    printf("FAIL  map: seed %.0f entity %d trail: %s\n",
                           (double)seed, i, why);
                    fails++;
                }
                checked++;
            }
            /* And with nothing held, something had better be out of reach
             * across the sweep - otherwise the ability gating is not gating
             * anything and the "beyond what you can cross" line is dead code. */
            bfs_gated(&g->w, ABIL_NONE,
                      (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE),
                      sc->dist, sc->queue);
            for (i = 0; i < ENTITY_COUNT; i++)
                if (g->ents[i].tile >= 0 && sc->dist[g->ents[i].tile] < 0)
                    closed_none++;
        }
        printf("map     : %d trails walked step by step - every step adjacent,"
               " closer, and enterable\n", checked);
        if (!closed_none) {
            printf("FAIL  map: not one collectible in %d seeds was out of reach"
                   " with no abilities - the trails are not gated at all\n", seeds);
            fails++;
        } else {
            printf("map     : %d collectible(s) across the sweep have no trail"
                   " until she earns the way in\n", closed_none);
        }

        /* NEGATIVE CONTROL for the step checker: a route the field does not
         * support must be rejected, or "every trail is walkable" only means
         * "trail_walk_ok returns >= 0". */
        {
            int broke = 0;
            g->restored = 0; g->maps = 0;
            game_init_area(g, sc, base, 1);
            bfs_gated(&g->w, ABIL_ALL,
                      (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE),
                      sc->dist, sc->queue);
            {
                const char *why;
                int wall = -1, x, y;
                for (y = 0; y < WORLD_H && wall < 0; y++)
                    for (x = 0; x < WORLD_W && wall < 0; x++)
                        if (g->w.solid[y][x]) wall = y * WORLD_W + x;
                /* A tile inside rock has no distance, so it has no route. */
                if (wall >= 0 && trail_walk_ok(&g->w, ABIL_ALL, sc->dist, wall, &why) < 0)
                    broke++;
                /* Punch a hole in the field: a step that closes no distance is
                 * exactly the corruption a drawn trail would show as a dot
                 * chain that wanders. */
                for (i = 0; i < WORLD_W * WORLD_H; i++)
                    if (sc->dist[i] == 1) sc->dist[i] = 900;
                if (trail_walk_ok(&g->w, ABIL_ALL, sc->dist,
                                  g->ents[0].tile >= 0 ? g->ents[0].tile : -1, &why) < 0)
                    broke++;
            }
            if (broke != 2) {
                printf("FAIL  map: the trail checker accepted %d of 2 broken"
                       " routes\n", 2 - broke);
                fails++;
            } else {
                printf("map     : negative control - the trail checker rejects an"
                       " unreachable target and a corrupted field\n");
            }
        }
    }

    /* ---- The screen ------------------------------------------------------ */
    {
        int lit_locked, water = -1, wall = -1, x, y;
        int found = 0, total_all = 0;

        /* The claim no BFS check above can make: that a trail reaches PIXELS,
         * and that it appears exactly when the thing it leads to becomes
         * reachable. Counted as pixels of the trail colour in a box around one
         * collectible - not as total lit pixels, which the screen's own opaque
         * background pins at the whole frame however the trails come out.
         *
         * Looked for over the sweep rather than assumed of one seed: the
         * candidate has to be a collectible that is CLOSED with no abilities
         * and open with all of them, and whose box no OTHER trail happens to
         * cross. The first seed that offers one settles it. */
        for (s_i = 0; s_i < seeds && !found; s_i++) {
            Uint64 seed = base + (Uint64)s_i;
            int ox, oy;

            g->restored = 0; g->maps = 0;
            game_init_area(g, sc, seed, 1);
            g->maps = 1;
            g->w.map_tile = -1;
            /* Freed BEFORE the zero: SDL_zero(hud) overwrites the surface
             * pointer, and a dropped 256 KB surface per seed is a leak the
             * sweep would multiply. */
            if (hud.bm) { SDL_FreeSurface(hud.bm); hud.bm = NULL; }
            SDL_zero(hud);
            bm_origin(fb, &ox, &oy);

            for (i = 0; i < ENTITY_COUNT && !found; i++) {
                Uint32 col;
                int bx, by, none_px, all_px;
                if (g->ents[i].tile < 0) continue;

                bx = ox + (g->ents[i].tile % WORLD_W) * BM_SCALE - 10;
                by = oy + (g->ents[i].tile / WORLD_W) * BM_SCALE - 10;

                g->p.abilities = ABIL_NONE;
                SDL_FillRect(fb, NULL, 0);
                hud.bm_dirty = 1;
                bigmap_draw(fb, g, sc);
                col = bm_trail_col(fb, g->ents[i].is_soul);
                none_px = count_col(fb, col, bx, by, 21, 21);

                g->p.abilities = ABIL_ALL;
                SDL_FillRect(fb, NULL, 0);
                hud.bm_dirty = 1;
                bigmap_draw(fb, g, sc);
                all_px = count_col(fb, col, bx, by, 21, 21);
                total_all = count_col(fb, col, ox, oy, BM_W, BM_H);

                if (none_px == 0 && all_px > 0) {
                    found = 1;
                    printf("map     : seed %.0f entity %d - no trail pixel while"
                           " it is closed, %d once she can reach it (%d on the"
                           " whole map)\n",
                           (double)seed, i, all_px, total_all);
                }
            }
        }
        if (!found) {
            printf("FAIL  map: no collectible anywhere in the sweep gained trail"
                   " pixels when its gate opened - the trails are not reaching"
                   " the screen\n");
            fails++;
        }

        /* Back to one known state for the checks below. */
        g->restored = 0; g->maps = 0;
        game_init_area(g, sc, base, 1);
        g->maps = 1;
        g->w.map_tile = -1;
        g->p.abilities = ABIL_ALL;
        if (hud.bm) { SDL_FreeSurface(hud.bm); hud.bm = NULL; }
        SDL_zero(hud);
        SDL_FillRect(fb, NULL, 0);
        hud.bm_dirty = 1;
        bigmap_draw(fb, g, sc);

        /* Water and wall must not be the same colour - the same claim, and the
         * same past bug, as the minimap's. */
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++) {
                if (water < 0 && g->w.terr[y][x] == GT_WATER) water = y * WORLD_W + x;
                if (wall < 0 && g->w.solid[y][x] && g->w.terr[y][x] != GT_WATER)
                    wall = y * WORLD_W + x;
            }
        if (water >= 0 && wall >= 0 &&
            bm_col(hud.bm, g, water % WORLD_W, water / WORLD_W) ==
            bm_col(hud.bm, g, wall % WORLD_W, wall / WORLD_W)) {
            printf("FAIL  map: water and rock draw the same colour on the map\n");
            fails++;
        } else if (water >= 0 && wall >= 0) {
            printf("map     : water and rock are distinguishable on the map\n");
        }

        /* NEGATIVE CONTROL for the "it draws" check: the map screen is only
         * reached through game_has_map, so an area whose fragment was never
         * found must leave the frame exactly as it found it. Driven through
         * the same predicate main() gates the call with. */
        g->maps = 0;
        SDL_FillRect(fb, NULL, 0);
        lit_locked = 0;
        if (game_has_map(g))
            bigmap_draw(fb, g, sc);
        lit_locked = count_lit(fb, 0, 0, fb->w, fb->h);
        if (lit_locked != 0) {
            printf("FAIL  map: a locked map still drew %d pixels\n", lit_locked);
            fails++;
        } else {
            printf("map     : negative control - with no fragment found, the map"
                   " screen draws nothing\n");
        }
        if (hud.bm) { SDL_FreeSurface(hud.bm); hud.bm = NULL; }
        if (hud.mm) { SDL_FreeSurface(hud.mm); hud.mm = NULL; }
    }

    printf("map     : %s\n", fails ? "FAIL" : "PASS");
    SDL_free(fogpal); fogpal = NULL;
    SDL_FreeSurface(fb);
    SDL_free(g); SDL_free(b); SDL_free(sc); SDL_free(dl);
    return fails;
}

/* ---- --hud-test ---------------------------------------------------------
 *
 * The HUD is the one subsystem whose bugs are invisible to every other test:
 * it draws on top of a finished frame, so nothing downstream can notice it
 * being wrong. Checked here: toast and banner lifetimes in ticks, the banner
 * firing exactly once, the counters actually reaching the screen, and the
 * minimap's stated sampling rule. */
static int hud_selftest(Uint64 base)
{
    Game *g = (Game *)SDL_malloc(sizeof(Game));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    SDL_Surface *fb = test_surface(LOGICAL_W, LOGICAL_H);
    int fails = 0, i;

    if (!g || !sc || !fb) {
        printf("FAIL  hud: out of memory\n");
        SDL_free(g); SDL_free(sc); if (fb) SDL_FreeSurface(fb);
        return 1;
    }
    if (!fogpal_build(fb)) {
        printf("FAIL  hud: fog palette\n");
        SDL_free(g); SDL_free(sc); SDL_FreeSurface(fb);
        return 1;
    }
    game_init(g, sc, base);
    SDL_zero(hud);

    /* Toast lifetime, in ticks, independent of frame rate. */
    hud_toast("a fragment returns");
    for (i = 0; i < HUD_TOAST_TICKS - 1; i++) hud_tick(g);
    if (hud.toast_left != 1) {
        printf("FAIL  hud: toast had %d ticks left after %d, expected 1\n",
               hud.toast_left, HUD_TOAST_TICKS - 1);
        fails++;
    }
    hud_tick(g);
    if (hud.toast_left != 0) {
        printf("FAIL  hud: toast did not expire at %d ticks\n", HUD_TOAST_TICKS);
        fails++;
    } else {
        printf("hud     : toast lives exactly %d ticks\n", HUD_TOAST_TICKS);
    }

    /* The banner announces completion once and does not re-announce. */
    for (i = 0; i < ENTITY_COUNT; i++)
        if (g->ents[i].tile >= 0) game_restore(g, i);
    if (!area_complete(g)) {
        printf("note    : seed %.0f placed only %d/%d entities; banner check skipped\n",
               (double)base, g->frags_restored + g->souls_restored, ENTITY_COUNT);
    } else {
        hud_tick(g);
        if (hud.win_left != HUD_WIN_TICKS || !hud.win_shown) {
            printf("FAIL  hud: banner did not fire on completion (%d left)\n", hud.win_left);
            fails++;
        }
        for (i = 0; i < HUD_WIN_TICKS; i++) hud_tick(g);
        if (hud.win_left != 0) {
            printf("FAIL  hud: banner outlived its %d ticks (%d left)\n",
                   HUD_WIN_TICKS, hud.win_left);
            fails++;
        } else {
            printf("hud     : banner fires once, lives exactly %d ticks\n", HUD_WIN_TICKS);
        }
    }

    /* The counters reach the screen. A HUD that silently drew nothing would
     * pass every lifetime check above. Counted inside the counter box only -
     * see count_lit. */
    {
        int a, b;
        g->frags_restored = 0; g->souls_restored = 0;
        SDL_FillRect(fb, NULL, 0);
        hud_draw(fb, g);
        a = count_lit(fb, 0, 0, 140, FONT_LINE * 3);
        g->frags_restored = 3; g->souls_restored = 2;
        SDL_FillRect(fb, NULL, 0);
        hud_draw(fb, g);
        b = count_lit(fb, 0, 0, 140, FONT_LINE * 3);
        if (a == 0) {
            printf("FAIL  hud: hud_draw lit no pixels in the counter box\n");
            fails++;
        } else if (a == b) {
            printf("FAIL  hud: the counters render identically at 0/0 and 3/2\n");
            fails++;
        } else {
            printf("hud     : counters reach the screen (%d px at 0/0, %d at 3/2)\n", a, b);
        }
    }

    /* The minimap's sampling rule, stated in mm_redraw and checked here: where
     * a block is mixed, an OPEN tile must win. Forced onto block (0,0) so the
     * check does not depend on what this seed generated. Reveal is forced too:
     * at reveal 0 every tile blends to the same fog colour, and the check would
     * be comparing two identical values. */
    {
        Uint32 open_c, wall_c, got;
        g->w.solid[0][0] = 1; g->w.solid[0][1] = 1; g->w.solid[1][0] = 1;
        g->w.solid[1][1] = 0;
        /* Terrain forced too, not just `solid`: this seed generated water in
         * the corner, and since water now takes its own colour branch both
         * tiles came out blue and the comparison compared nothing. */
        g->w.terr[0][0] = g->w.terr[0][1] = g->w.terr[1][0] = GT_ROCK;
        g->w.terr[1][1] = GT_GRASS;
        g->w.reveal[0][0] = g->w.reveal[0][1] = 255;
        g->w.reveal[1][0] = g->w.reveal[1][1] = 255;
        hud.mm_dirty = 1;
        mm_draw(fb, g);              /* allocates hud.mm and redraws it */
        if (!hud.mm) {
            printf("FAIL  hud: minimap surface never allocated\n");
            fails++;
        } else {
            open_c = mm_col(g, 1, 1);
            wall_c = mm_col(g, 0, 0);
            got = *(const Uint32 *)hud.mm->pixels;
            if (open_c == wall_c) {
                printf("FAIL  hud: open and wall minimap colours are identical, so "
                       "the sampling check below proves nothing\n");
                fails++;
            } else if (got != open_c) {
                printf("FAIL  hud: minimap sampled the wall (0x%08X), not the open "
                       "tile (0x%08X) - one-tile corridors would vanish\n",
                       (unsigned)got, (unsigned)open_c);
                fails++;
            } else {
                printf("hud     : minimap sampling prefers open over wall\n");
            }
            /* NEGATIVE CONTROL: the naive top-left sampler this rule replaced
             * must get the same block wrong. */
            if (wall_c == open_c || wall_c == got) {
                printf("FAIL  hud: negative control cannot distinguish the samplers\n");
                fails++;
            } else {
                printf("hud     : negative control PASS (top-left sampler would "
                       "have drawn 0x%08X)\n", (unsigned)wall_c);
            }
        }

        /* Water must not draw as rock. Both are solid, so the obvious ordering
         * inside mm_col makes the water branch unreachable and every pond
         * indistinguishable from a boulder - which is what shipped until a
         * screenshot showed a minimap with no ponds on it. */
        g->w.terr[0][0] = GT_ROCK;  g->w.solid[0][0] = 1;
        g->w.terr[3][3] = GT_WATER; g->w.solid[3][3] = 1;
        g->w.reveal[3][3] = 255;
        if (mm_col(g, 0, 0) == mm_col(g, 3, 3)) {
            printf("FAIL  hud: water and rock draw the same minimap colour, so "
                   "ponds are invisible on the map\n");
            fails++;
        } else {
            printf("hud     : water and rock are distinguishable on the minimap\n");
        }
    }

    /* ---- The interact prompt ------------------------------------------
     *
     * Three properties, and each fails silently on screen:
     *   (a) the bob stays inside its stated travel and does actually move;
     *   (b) NOTHING is drawn when nothing is in reach - a stale keycap left
     *       hanging over empty forest is worse than no keycap at all;
     *   (c) the cue and the KEY agree everywhere. A prompt driven from a
     *       different radius, or from the interpolated render position instead
     *       of the simulation one, promises an interact that does nothing.
     */
    {
        /* (a) The bob. Swept over four full cycles rather than evaluated at a
         * few points, because the failure mode is a sine that leaves its range
         * somewhere in the middle of the cycle. */
        int lo = 1 << 30, hi = -(1 << 30), ctl_out = 0, k;
        for (k = 0; k < 400; k++) {
            float t = (float)k * (4.0f / (PROMPT_HZ * 400.0f));
            int o = prompt_bob(t);
            int unclamped = (int)(SDL_sinf(t * PROMPT_HZ * (float)TWO_PI) *
                                  (float)(PROMPT_BOB * 3));
            if (o < lo) lo = o;
            if (o > hi) hi = o;
            /* NEGATIVE CONTROL: a bob three times the stated amplitude must be
             * caught by the same bound test. Without this, a check that only
             * ever saw a well-behaved function proves nothing about the bound. */
            if (unclamped > PROMPT_BOB || unclamped < -PROMPT_BOB) ctl_out++;
        }
        if (lo < -PROMPT_BOB || hi > PROMPT_BOB) {
            printf("FAIL  hud: prompt bob ranges [%d, %d], outside +/-%d\n",
                   lo, hi, PROMPT_BOB);
            fails++;
        } else if (lo == hi) {
            printf("FAIL  hud: prompt bob never moves - it is constant %d\n", lo);
            fails++;
        } else if (!ctl_out) {
            printf("FAIL  hud: prompt bob negative control - an over-amplitude bob"
                   " was not rejected, so the bound check proves nothing\n");
            fails++;
        } else {
            printf("hud     : prompt bob ranges [%d, %d] within +/-%d and does move;"
                   " control rejected %d/400 over-amplitude samples\n",
                   lo, hi, PROMPT_BOB, ctl_out);
        }
    }
    {
        /* (b) and (c), driven against a real generated world. The first placed
         * entity is used as the subject; its tile is force-revealed so the fog
         * condition cannot make the whole block vacuous. */
        int subj = -1, cam_x, cam_y, ex, ey;
        for (i = 0; i < ENTITY_COUNT; i++)
            if (g->ents[i].tile >= 0) { subj = i; break; }
        for (i = 0; i < ENTITY_COUNT; i++) g->ents[i].restored = 0;

        if (subj < 0) {
            printf("FAIL  hud: seed %.0f placed no entities - the prompt checks"
                   " cannot run\n", (double)base);
            fails++;
        } else {
            int near_px, far_px, glyph_px, restored_px, disagree = 0, ctl_disagree = 0, k;
            float cx, cy;
            ex = g->ents[subj].tile % WORLD_W;
            ey = g->ents[subj].tile / WORLD_W;
            g->w.reveal[ey][ex] = 255;
            cx = (float)ex * TILE + TILE * 0.5f;
            cy = (float)ey * TILE + TILE * 0.5f;
            cam_x = (int)cx - LOGICAL_W / 2;
            cam_y = (int)cy - LOGICAL_H / 2;

            SDL_FillRect(fb, NULL, 0);
            prompt_draw(fb, &g->w, g->ents, cx, cy, cam_x, cam_y, 0.0f);
            near_px = count_lit(fb, 0, 0, LOGICAL_W, LOGICAL_H);

            /* Well outside INTERACT_RADIUS of everything: half the map away. */
            SDL_FillRect(fb, NULL, 0);
            prompt_draw(fb, &g->w, g->ents, cx + WORLD_W * TILE * 0.5f, cy,
                        cam_x, cam_y, 0.0f);
            far_px = count_lit(fb, 0, 0, LOGICAL_W, LOGICAL_H);

            /* The glyph, counted by its own colour: a cap drawn as a blank
             * plate would satisfy "lit something" and say nothing to anyone. */
            SDL_FillRect(fb, NULL, 0);
            prompt_draw(fb, &g->w, g->ents, cx, cy, cam_x, cam_y, 0.0f);
            {
                Uint32 want = SDL_MapRGB(fb->format, 0xff, 0xf4, 0xd8);
                int px, py;
                glyph_px = 0;
                for (py = 0; py < LOGICAL_H; py++) {
                    const Uint32 *row = (const Uint32 *)((const Uint8 *)fb->pixels
                                                         + py * fb->pitch);
                    for (px = 0; px < LOGICAL_W; px++)
                        if ((row[px] & 0x00FFFFFFu) == (want & 0x00FFFFFFu)) glyph_px++;
                }
            }

            /* A restored entity is scenery. Prompting over it would promise an
             * interact that does nothing. */
            g->ents[subj].restored = 1;
            SDL_FillRect(fb, NULL, 0);
            prompt_draw(fb, &g->w, g->ents, cx, cy, cam_x, cam_y, 0.0f);
            restored_px = count_lit(fb, 0, 0, LOGICAL_W, LOGICAL_H);
            g->ents[subj].restored = 0;

            /* (c) Cue and key agree. Swept radially through the boundary, in
             * sub-pixel steps, so the exact tile where the two could disagree
             * is actually visited. */
            for (k = 0; k < 240; k++) {
                float d = (float)k * 0.25f;                 /* 0 .. 60 px */
                float qx = cx + d, qy = cy;
                int acts = (entity_in_reach(&g->w, g->ents, qx, qy) >= 0);
                int shown;
                SDL_FillRect(fb, NULL, 0);
                prompt_draw(fb, &g->w, g->ents, qx, qy, cam_x, cam_y, 0.0f);
                shown = count_lit(fb, 0, 0, LOGICAL_W, LOGICAL_H) > 0;
                if (acts != shown) disagree++;
                /* NEGATIVE CONTROL: a cue driven at twice the radius must be
                 * caught disagreeing with the key. Without it, "they agree"
                 * would pass on an implementation that always drew, or never
                 * did, at every sampled distance. */
                if ((d <= INTERACT_RADIUS * 2.0f) != acts) ctl_disagree++;
            }

            if (far_px != 0) {
                printf("FAIL  hud: the prompt drew %d px with nothing in reach\n", far_px);
                fails++;
            } else if (near_px == 0) {
                printf("FAIL  hud: the prompt drew nothing standing on an entity\n");
                fails++;
            } else if (glyph_px == 0) {
                printf("FAIL  hud: the prompt drew a blank cap - no E on it\n");
                fails++;
            } else if (restored_px != 0) {
                printf("FAIL  hud: the prompt drew %d px over a RESTORED entity\n",
                       restored_px);
                fails++;
            } else if (disagree) {
                printf("FAIL  hud: cue and key disagreed at %d of 240 distances\n",
                       disagree);
                fails++;
            } else if (!ctl_disagree) {
                printf("FAIL  hud: prompt negative control - a double-radius cue"
                       " agreed everywhere, so the sweep proves nothing\n");
                fails++;
            } else {
                printf("hud     : prompt %d px in reach (%d of them glyph), 0 px out"
                       " of reach, 0 px over a restored entity\n",
                       near_px, glyph_px);
                printf("hud     : cue and key agree at all 240 sampled distances;"
                       " control caught a double-radius cue at %d of them\n",
                       ctl_disagree);
            }
        }
    }

    /* Every HUD string must fit the 480 px frame. A toast wider than the screen
     * centres to a negative x and is clipped at both ends. */
    {
        static const char *lines[] = {
            "a soul is remembered  3/3", "you remember the light",
            "the water is too deep", "could not write the save file",
            "the forest remembers", "the portal opens",
            "the underworld remembers", "the lumiara remembers",
            "the lumiara opens", "all is restored"
        };
        for (i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); i++)
            if (text_w(lines[i]) > LOGICAL_W - 8) {
                printf("FAIL  hud: \"%s\" is %d px, wider than the frame\n",
                       lines[i], text_w(lines[i]));
                fails++;
            }
        printf("hud     : %d HUD strings all fit %d px\n", i, LOGICAL_W);
    }

    /* Seed cycling. The '[' and ']' keys route through game_reseed for the same
     * reason E routes through try_interact: so a test drives exactly what the
     * key runs, rather than a re-implementation of it that can agree with the
     * test while disagreeing with the game.
     *
     * Three things have to hold, and each one fails SILENTLY - the game keeps
     * running and looks fine, it is just showing the wrong world:
     *   - the world actually changes, so the key is not a no-op;
     *   - she is standing somewhere legal in the NEW world, not at coordinates
     *     that only meant something in the old one;
     *   - the latches a fresh start clears are cleared, or the completion
     *     banner never fires again for the rest of the session. */
    {
        Audio a;
        Uint64 t0, t1;
        int x, y;

        SDL_zero(a);
        game_init(g, sc, base);
        /* Stand-ins for a session in progress: a finished area and a stale map. */
        hud.win_shown = 1;
        hud.mm_dirty  = 0;
        for (t0 = 0, y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                t0 = t0 * 31u + g->w.terr[y][x];

        game_reseed(g, sc, &a, base + 1);

        for (t1 = 0, y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                t1 = t1 * 31u + g->w.terr[y][x];
        if (t0 == t1) {
            printf("FAIL  hud: reseed left the terrain identical - the key is a"
                   " no-op\n");
            fails++;
        }
        if (g->seed != base + 1) {
            printf("FAIL  hud: reseed left seed at %.0f, expected %.0f\n",
                   (double)g->seed, (double)(base + 1));
            fails++;
        }
        /* Feet on legal ground in the world she is now standing in. */
        if (player_blocked(&g->w, g->p.abilities, g->p.x, g->p.y)) {
            printf("FAIL  hud: reseed left her inside a wall at (%.2f, %.2f)\n",
                   (double)g->p.x, (double)g->p.y);
            fails++;
        }
        if (hud.win_shown || !hud.mm_dirty ||
            g->frags_restored != 0 || g->souls_restored != 0 ||
            g->p.abilities != ABIL_NONE) {
            printf("FAIL  hud: reseed carried session state into the new world"
                   " (win_shown %d, mm_dirty %d, frags %d, souls %d, abil %d)\n",
                   hud.win_shown, hud.mm_dirty, g->frags_restored,
                   g->souls_restored, g->p.abilities);
            fails++;
        }
        /* The audio callback owns its RNG, so the reseed must be handed over as
         * a request rather than written behind its back. */
        if (!SDL_AtomicGet(&a.rng_req) || !SDL_AtomicGet(&a.reset_req) ||
            a.rng_seed_req != base + 1) {
            printf("FAIL  hud: reseed did not post the audio reseed request\n");
            fails++;
        }

        /* Negative control: the same checks against a reseed that only sets the
         * seed - which is the obvious way to write this and leaves every latch
         * standing - must REJECT it. Without this the block above would pass
         * against a game_reseed that did almost nothing. */
        {
            int caught = 0;
            game_init(g, sc, base);
            hud.win_shown = 1;
            hud.mm_dirty  = 0;
            g->seed = base + 1;          /* the whole of the broken "reseed" */
            if (hud.win_shown)  caught++;
            if (!hud.mm_dirty)  caught++;
            if (caught == 0) {
                printf("  hud reseed negative control FAILED: a seed-only reseed"
                       " satisfied the state checks\n");
                fails++;
            } else {
                printf("hud     : negative control - a seed-only reseed misses %d"
                       " of the state resets\n", caught);
            }
        }
        printf("hud     : reseed rebuilds the world, lands her on open ground,"
               " and clears the session\n");
    }

    printf("hud     : %s\n", fails ? "FAIL" : "PASS");
    if (hud.mm) { SDL_FreeSurface(hud.mm); hud.mm = NULL; }
    SDL_free(fogpal); fogpal = NULL;
    SDL_FreeSurface(fb);
    SDL_free(g); SDL_free(sc);
    return fails;
}

/* ---- --menu-test --------------------------------------------------------
 *
 * The menu is the first screen a player ever sees, the only way out of the game
 * that is not the window's close button, and now the only thing standing
 * between six saves and the wrong one being destroyed. So the things that must
 * not break are: it draws when open and NOTHING when closed, the selection can
 * never leave the list or land on a row that does nothing, a row's action is
 * the action its label promises, "continue" takes the most recently written
 * save and not merely the first one, replacing a save is never done without
 * being asked, the settings file cannot poison the live settings, and every
 * label is made of characters the font actually fills.
 *
 * Every check below has a negative control, because a checker that has never
 * rejected anything proves nothing - and several of these controls are exactly
 * the bugs the design invites: a label containing '(' shipping as a blank hole,
 * a slot row lit by a file that cannot actually be loaded, a "continue" that
 * looks right only because the newest save happens to be in the lowest slot,
 * and a confirm whose "no" quietly does the same thing as its "yes".
 */

/* Slot files are written and deleted all over this test. Named through the
 * shipping save_slot_path so the test cannot drift onto a different set of
 * files from the ones the game reads. */
static void menu_test_clear_slots(void)
{
    char path[SAVE_PATH_MAX];
    int i;

    for (i = 0; i < SAVE_SLOTS; i++) {
        save_slot_path(path, sizeof(path), i);
        (void)remove(path);
    }
}

/* Write a real save into a slot and then force its timestamp, so the ordering
 * "continue" depends on can be set up deliberately instead of being whatever
 * two writes a millisecond apart happened to produce. Goes through game_save
 * first, so what is on disk is a genuine save and only the one field under
 * test is synthetic. */
static int menu_test_write_slot(const Game *g, int slot, Uint64 stamp)
{
    char path[SAVE_PATH_MAX];
    Uint8 buf[SAVE_SIZE];
    SDL_RWops *rw;

    save_slot_path(path, sizeof(path), slot);
    if (game_save(g, path) != 0)
        return -1;
    rw = SDL_RWFromFile(path, "rb");
    if (!rw) return -1;
    if (SDL_RWread(rw, buf, 1, SAVE_SIZE) != (size_t)SAVE_SIZE) {
        SDL_RWclose(rw); return -1;
    }
    SDL_RWclose(rw);
    save_put64(buf + 28, stamp);
    rw = SDL_RWFromFile(path, "wb");
    if (!rw) return -1;
    SDL_RWwrite(rw, buf, 1, SAVE_SIZE);
    SDL_RWclose(rw);
    return 0;
}

static int menu_selftest(void)
{
    SDL_Surface *fb = test_surface(LOGICAL_W, LOGICAL_H);
    const char *cfgpath = "wayfarer-menutest.tmp";
    Settings cfg, back;
    SaveSlot slots[SAVE_SLOTS];
    MenuRow  rows[MENU_ROWS_MAX];
    MenuCtx  ctx;
    Game    *g  = NULL;
    Scratch *sc = NULL;
    int fails = 0, count, i;

    if (!fb) {
        printf("FAIL  menu: could not create the test surface\n");
        return 1;
    }
    cfg_defaults(&cfg);
    SDL_zero(menu);
    SDL_memset(slots, 0, sizeof(slots));
    menu_test_clear_slots();

    g  = (Game *)SDL_malloc(sizeof(Game));
    sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    if (!g || !sc) {
        printf("FAIL  menu: could not allocate a world\n");
        SDL_free(g); SDL_free(sc); SDL_FreeSurface(fb);
        return 1;
    }
    game_init(g, sc, 1);

    /* ---- 1. an empty disk offers nothing to continue or load ------------- */
    if (save_scan(slots) != 0) {
        printf("FAIL  menu: slots reported as used with no files on disk\n");
        fails++;
    }
    if (save_newest(slots) != -1 || save_first_free(slots) != 0) {
        printf("FAIL  menu: newest %d / first free %d on an empty disk\n",
               save_newest(slots), save_first_free(slots));
        fails++;
    }
    menu.page = MENU_ROOT; menu.started = 0;
    ctx = menu_ctx(&cfg, slots);
    count = menu_build(&ctx, rows);
    if (count != 5 || rows[0].act != MA_CONTINUE || rows[0].enabled ||
        rows[1].act != MA_LOADPAGE || rows[1].enabled) {
        printf("FAIL  menu: a first launch does not offer new game with"
               " continue and load dim (%d rows)\n", count);
        fails++;
    } else {
        printf("menu    : first launch - \"%s\" and \"%s\" dim, \"%s\" ready\n",
               rows[0].text, rows[1].text, rows[2].text);
    }
    /* The caret must not START on a dim row - the very first thing the player
     * ever sees would otherwise be a selection that does nothing. */
    if (menu_first(rows, count) != 2) {
        printf("FAIL  menu: the caret opens on row %d, which is dim\n",
               menu_first(rows, count));
        fails++;
    }

    /* ---- 2. slots are read back as what was written into them ------------ */
    if (menu_test_write_slot(g, 2, 5000) != 0 ||
        menu_test_write_slot(g, 4, 9000) != 0 ||
        menu_test_write_slot(g, 0, 7000) != 0) {
        printf("FAIL  menu: could not write the test slots\n");
        fails++;
    }
    if (save_scan(slots) != 3) {
        printf("FAIL  menu: three slots written, %d scanned\n", save_scan(slots));
        fails++;
    }
    if (!slots[0].used || !slots[2].used || !slots[4].used ||
        slots[1].used || slots[3].used || slots[5].used) {
        printf("FAIL  menu: the wrong slots came back used\n");
        fails++;
    }
    if (save_first_free(slots) != 1) {
        printf("FAIL  menu: first free is %d, expected 1\n",
               save_first_free(slots));
        fails++;
    }

    /* ---- 3. continue takes the NEWEST save, not the lowest slot ---------- */
    if (save_newest(slots) != 4) {
        printf("FAIL  menu: newest is slot %d, but slot 5 was stamped latest\n",
               save_newest(slots) + 1);
        fails++;
    } else {
        printf("menu    : continue takes slot 5, the latest of 1, 3 and 5\n");
    }
    /* NEGATIVE CONTROL. Restamp so a DIFFERENT slot is newest. If the answer
     * does not move, save_newest is reporting slot order - which agrees with
     * the check above by accident and would send continue to the wrong save
     * the first time somebody played out of order. */
    if (menu_test_write_slot(g, 2, 20000) == 0) {
        (void)save_scan(slots);
        if (save_newest(slots) != 2) {
            printf("FAIL  menu: negative control - newest stayed at slot %d"
                   " after slot 3 was restamped latest; it is ordering by slot,"
                   " not by time\n", save_newest(slots) + 1);
            fails++;
        } else {
            printf("menu    : negative control PASS (restamping moves continue"
                   " to slot 3)\n");
        }
    }

    /* ---- 4. the root menu changes once there is something to continue ---- */
    menu.page = MENU_ROOT; menu.started = 0;
    ctx = menu_ctx(&cfg, slots);
    count = menu_build(&ctx, rows);
    if (!rows[0].enabled || rows[0].act != MA_CONTINUE ||
        rows[0].slot != save_newest(slots)) {
        printf("FAIL  menu: continue is not lit, or does not carry the newest"
               " slot (%d vs %d)\n", rows[0].slot, save_newest(slots));
        fails++;
    }
    if (!rows[1].enabled || rows[1].act != MA_LOADPAGE) {
        printf("FAIL  menu: load game is not offered with saves on disk\n");
        fails++;
    }
    if (menu_first(rows, count) != 0) {
        printf("FAIL  menu: the caret does not open on continue\n");
        fails++;
    } else {
        printf("menu    : with saves on disk the root offers"
               " \"%s\" / \"%s\" / \"%s\"\n",
               rows[0].text, rows[1].text, rows[2].text);
    }
    /* Paused, the same page must offer resume and save instead of continue -
     * continuing is meaningless while she is standing in the world. */
    menu.started = 1;
    ctx = menu_ctx(&cfg, slots);
    count = menu_build(&ctx, rows);
    if (rows[0].act != MA_START || rows[1].act != MA_SAVE) {
        printf("FAIL  menu: the pause menu does not lead with resume and save\n");
        fails++;
    } else {
        printf("menu    : paused, the root leads with \"%s\" and \"%s\"\n",
               rows[0].text, rows[1].text);
    }
    menu.started = 0;

    /* ---- 5. the slot list, in both of its modes -------------------------- */
    menu.page = MENU_SLOTS;
    menu.mode = SLOTS_LOAD;
    ctx = menu_ctx(&cfg, slots);
    count = menu_build(&ctx, rows);
    if (count != SAVE_SLOTS + 1) {
        printf("FAIL  menu: the slot list has %d rows, expected %d\n",
               count, SAVE_SLOTS + 1);
        fails++;
    }
    for (i = 0; i < SAVE_SLOTS; i++) {
        if (rows[i].slot != i) {
            printf("FAIL  menu: list row %d carries slot %d\n", i, rows[i].slot);
            fails++;
        }
        /* Reading needs something to read: an empty slot must be dead here. */
        if (rows[i].enabled != slots[i].used) {
            printf("FAIL  menu: load mode offers slot %d, which is %s\n",
                   i + 1, slots[i].used ? "used" : "empty");
            fails++;
        }
    }
    if (rows[SAVE_SLOTS].act != MA_BACK) {
        printf("FAIL  menu: the slot list has no way back\n");
        fails++;
    }
    /* NEGATIVE CONTROL for the mode. In SLOTS_NEW every slot is a legitimate
     * destination, empty or not. If the two modes produced the same rows the
     * mode would be doing nothing, and "replace which save" would refuse to
     * offer the saves it exists to replace. */
    menu.mode = SLOTS_NEW;
    ctx = menu_ctx(&cfg, slots);
    count = menu_build(&ctx, rows);
    {
        int all = 1;
        for (i = 0; i < SAVE_SLOTS; i++)
            if (!rows[i].enabled) all = 0;
        if (!all) {
            printf("FAIL  menu: negative control - replace mode still refuses"
                   " some slots, so the mode changes nothing\n");
            fails++;
        } else {
            printf("menu    : load mode offers 3 of 6 slots, replace mode"
                   " offers all 6\n");
        }
    }
    menu.mode = SLOTS_LOAD;

    /* ---- 6. a slot with an unreadable file is not offered ----------------- */
    {
        char path[SAVE_PATH_MAX];
        Uint8 buf[SAVE_SIZE];
        SDL_RWops *rw;

        save_slot_path(path, sizeof(path), 0);
        rw = SDL_RWFromFile(path, "rb");
        if (rw && SDL_RWread(rw, buf, 1, SAVE_SIZE) == (size_t)SAVE_SIZE) {
            SDL_RWclose(rw);
            /* NEGATIVE CONTROL. The file is still there and still the right
             * length - only the version is wrong, which is exactly what every
             * save on disk from before this change looks like. A list lit by
             * mere existence would keep offering it and then fail on it. */
            buf[2] = SAVE_VERSION - 1;
            rw = SDL_RWFromFile(path, "wb");
            if (rw) { SDL_RWwrite(rw, buf, 1, SAVE_SIZE); SDL_RWclose(rw); }
            (void)save_scan(slots);
            if (slots[0].used) {
                printf("FAIL  menu: negative control - a slot holding a"
                       " previous-version save is still offered\n");
                fails++;
            } else {
                printf("menu    : negative control PASS (a v%d file in slot 1"
                       " is not offered)\n", SAVE_VERSION - 1);
            }
            /* And put a good save back, so the rest of the test has three. */
            (void)menu_test_write_slot(g, 0, 7000);
            (void)save_scan(slots);
        } else {
            if (rw) SDL_RWclose(rw);
            printf("FAIL  menu: could not re-read slot 1\n");
            fails++;
        }
    }

    /* ---- 7. replacing a save is never done without being asked ----------- */
    menu.page = MENU_CONFIRM;
    menu.pending = 2;
    ctx = menu_ctx(&cfg, slots);
    count = menu_build(&ctx, rows);
    if (count != 2) {
        printf("FAIL  menu: the confirm page has %d rows, expected 2\n", count);
        fails++;
    }
    /* "no" must be FIRST, so the caret starts on it: the default answer to a
     * question nobody read has to be the one that destroys nothing. */
    if (rows[0].act != MA_BACK || rows[1].act != MA_YES) {
        printf("FAIL  menu: the confirm page does not default to keeping the"
               " save\n");
        fails++;
    } else if (menu_first(rows, count) != 0) {
        printf("FAIL  menu: the confirm caret does not start on \"%s\"\n",
               rows[0].text);
        fails++;
    } else {
        printf("menu    : replacing asks first, and opens on \"%s\"\n",
               rows[0].text);
    }
    /* NEGATIVE CONTROL: the two answers must not be the same action. A confirm
     * whose "no" also returns MA_YES would pass every check above. */
    if (menu_act(rows, count, 0) == menu_act(rows, count, 1)) {
        printf("FAIL  menu: negative control - yes and no resolve to the same"
               " action, so the question is decoration\n");
        fails++;
    } else {
        printf("menu    : negative control PASS (yes and no are different"
               " actions)\n");
    }
    if (rows[1].slot != 2) {
        printf("FAIL  menu: the confirm's yes carries slot %d, not the slot"
               " being replaced\n", rows[1].slot);
        fails++;
    }
    menu.pending = 0;

    /* ---- 8. it draws when open, and nothing at all when closed ----------- */
    {
        Uint32 ink_c = SDL_MapRGB(fb->format, 0x10, 0x12, 0x18);
        int open_px, filled, shut_px;

        SDL_zero(menu);
        menu.page = MENU_ROOT;
        ctx = menu_ctx(&cfg, slots);
        SDL_FillRect(fb, NULL, 0);
        menu_draw(fb, &ctx);
        /* Counted as pixels that are NOT the background, not as pixels that
         * are lit: the menu fills the whole surface with an ink that is itself
         * non-black, so count_lit alone returns all 129,600 whether or not a
         * single glyph rendered - it would pass on an empty screen. */
        filled  = count_col(fb, ink_c, 0, 0, fb->w, fb->h);
        open_px = fb->w * fb->h - filled;
        if (filled <= 0) {
            printf("FAIL  menu: an open menu did not fill its background\n");
            fails++;
        }
        if (open_px <= 0) {
            printf("FAIL  menu: an open menu drew a background and no text\n");
            fails++;
        }
        /* NEGATIVE CONTROL, driven through the SAME predicate main() gates the
         * draw with, so it is the shipping path being asserted about. */
        SDL_zero(menu);
        menu.page = MENU_NONE;
        ctx = menu_ctx(&cfg, slots);
        SDL_FillRect(fb, NULL, 0);
        if (menu.page != MENU_NONE)
            menu_draw(fb, &ctx);
        menu_draw(fb, &ctx);   /* and directly, to prove its own early out */
        shut_px = count_lit(fb, 0, 0, fb->w, fb->h);
        if (shut_px != 0) {
            printf("FAIL  menu: negative control - a closed menu drew %d px\n",
                   shut_px);
            fails++;
        } else {
            printf("menu    : draws %d px of text over a %d px ground when"
                   " open, 0 px of anything when closed\n", open_px, filled);
        }

        /* Every page must actually render something. A page that built rows
         * but drew nothing would slip past the root-only check above. */
        {
            static const MenuPage pages[5] = {
                MENU_ROOT, MENU_SLOTS, MENU_CONFIRM, MENU_SETTINGS, MENU_CONTROLS
            };
            int p, blank = 0;
            for (p = 0; p < 5; p++) {
                SDL_zero(menu);
                menu.page = pages[p];
                ctx = menu_ctx(&cfg, slots);
                SDL_FillRect(fb, NULL, 0);
                menu_draw(fb, &ctx);
                if (fb->w * fb->h - count_col(fb, ink_c, 0, 0, fb->w, fb->h) <= 0)
                    blank++;
            }
            if (blank) {
                printf("FAIL  menu: %d of 5 pages drew no text\n", blank);
                fails++;
            } else {
                printf("menu    : all 5 pages draw text\n");
            }
        }
    }

    /* ---- 9. a message raised while the menu is up is actually shown ------ */
    {
        Uint32 ink_c = SDL_MapRGB(fb->format, 0x10, 0x12, 0x18);
        int quiet, loud;
#define MENU_TEXT_PX() (fb->w * fb->h - count_col(fb, ink_c, 0, 0, fb->w, fb->h))

        SDL_zero(menu);
        menu.page = MENU_ROOT;
        ctx = menu_ctx(&cfg, slots);
        hud.toast_left = 0;
        SDL_FillRect(fb, NULL, 0);
        menu_draw(fb, &ctx);
        quiet = MENU_TEXT_PX();

        /* The menu replaces the HUD, so a toast posted here used to be handed
         * to a hud_draw that never runs. The failure paths that raise one - a
         * save that will not load, settings that will not write - both leave
         * the menu open, so this is the only place the player could see it. */
        hud_toast("that save could not be loaded");
        SDL_FillRect(fb, NULL, 0);
        menu_draw(fb, &ctx);
        loud = MENU_TEXT_PX();

        if (loud <= quiet) {
            printf("FAIL  menu: a toast raised over the menu drew nothing"
                   " (%d px with, %d px without)\n", loud, quiet);
            fails++;
        } else {
            printf("menu    : a toast over the menu adds %d px\n", loud - quiet);
        }
        /* NEGATIVE CONTROL: an EXPIRED toast must not be drawn, or a stale
         * message would sit on the title screen forever. */
        hud.toast_left = 0;
        SDL_FillRect(fb, NULL, 0);
        menu_draw(fb, &ctx);
        if (MENU_TEXT_PX() != quiet) {
            printf("FAIL  menu: negative control - an expired toast is still"
                   " drawn over the menu\n");
            fails++;
        } else {
            printf("menu    : negative control PASS (an expired toast draws"
                   " nothing)\n");
        }
#undef MENU_TEXT_PX
        SDL_zero(hud);
    }

    /* ---- 10. the selection cannot escape, and never rests on a dead row -- */
    {
        int sel, bad = 0, onto_disabled = 0, step;

        /* The load list with three of six slots empty: the disabled rows are
         * scattered through the MIDDLE of the list, which is the arrangement
         * that catches a skip implemented as a clamp. */
        SDL_zero(menu);
        menu.page = MENU_SLOTS;
        menu.mode = SLOTS_LOAD;
        ctx = menu_ctx(&cfg, slots);
        count = menu_build(&ctx, rows);
        sel = menu_first(rows, count);
        for (i = 0; i < 200; i++) {
            step = (i % 3 == 0) ? -1 : 1;
            sel = menu_step(rows, count, sel, step);
            if (sel < 0 || sel >= count) { bad++; break; }
            if (!rows[sel].enabled) onto_disabled++;
        }
        if (bad) {
            printf("FAIL  menu: selection left the list (%d of %d)\n", sel, count);
            fails++;
        }
        if (onto_disabled) {
            printf("FAIL  menu: selection landed on a disabled row %d times\n",
                   onto_disabled);
            fails++;
        }
        if (!bad && !onto_disabled)
            printf("menu    : 200 moves stayed inside %d rows and never landed"
                   " on an empty slot\n", count);

        /* It must WRAP, not stop. A clamp would pass everything above. */
        {
            int last = count - 1, first = menu_first(rows, count);
            if (menu_step(rows, count, first, -1) != last ||
                menu_step(rows, count, last, 1) != first) {
                printf("FAIL  menu: the selection clamps at the ends instead"
                       " of wrapping\n");
                fails++;
            } else {
                printf("menu    : wraps at both ends\n");
            }
        }
    }

    /* ---- 11. a bad index does nothing, on every page --------------------- */
    {
        static const MenuPage pages[5] = {
            MENU_ROOT, MENU_SLOTS, MENU_CONFIRM, MENU_SETTINGS, MENU_CONTROLS
        };
        int p, n_bad = 0;

        for (p = 0; p < 5; p++) {
            int idx[4];
            SDL_zero(menu);
            menu.page = pages[p];
            ctx = menu_ctx(&cfg, slots);
            count = menu_build(&ctx, rows);
            idx[0] = -1; idx[1] = count; idx[2] = count + 99; idx[3] = -1000;
            for (i = 0; i < 4; i++)
                if (menu_act(rows, count, idx[i]) != MA_NONE) n_bad++;
        }
        /* A disabled row is out of bounds in the same sense: an empty slot in
         * load mode must not resolve to MA_SLOT and try to read a file that is
         * not there. */
        SDL_zero(menu);
        menu.page = MENU_SLOTS;
        menu.mode = SLOTS_LOAD;
        ctx = menu_ctx(&cfg, slots);
        count = menu_build(&ctx, rows);
        for (i = 0; i < SAVE_SLOTS; i++)
            if (!slots[i].used && menu_act(rows, count, i) != MA_NONE) n_bad++;
        if (n_bad) {
            printf("FAIL  menu: negative control - %d out-of-range or disabled"
                   " rows resolved to a real action\n", n_bad);
            fails++;
        } else {
            printf("menu    : negative control PASS (out-of-range indices and"
                   " empty slots do nothing, on all 5 pages)\n");
        }
    }

    /* ---- 12. every label is drawable, and fits --------------------------- */
    {
        static const MenuPage pages[5] = {
            MENU_ROOT, MENU_SLOTS, MENU_CONFIRM, MENU_SETTINGS, MENU_CONTROLS
        };
        int checked = 0, blanks = 0, wide = 0, pi, ri, ci, pass;

        for (pi = 0; pi < 5; pi++) {
            /* Every combination that changes a label: paused or not, both slot
             * modes, both fullscreen states, and a full disk as well as this
             * one - so no label escapes because it only exists in one of them. */
            for (pass = 0; pass < 8; pass++) {
                Settings v = cfg;
                SaveSlot  sv[SAVE_SLOTS];
                int si;

                v.fullscreen = pass & 1;
                v.music = (pass & 2) ? 0 : VOL_MAX;
                v.sfx   = (pass & 2) ? 3 : VOL_MAX;
                SDL_memcpy(sv, slots, sizeof(sv));
                if (pass & 4)
                    for (si = 0; si < SAVE_SLOTS; si++) {
                        /* The widest a row can be: the longest area name, a
                         * two-digit total, and the newest marker. */
                        sv[si].used  = 1;
                        sv[si].area  = 2;   /* "underworld" */
                        sv[si].done  = ENTITY_COUNT;
                        sv[si].stamp = (Uint64)(si + 1);
                    }
                SDL_zero(menu);
                menu.page    = pages[pi];
                menu.started = pass & 1;
                menu.mode    = (pass & 2) ? SLOTS_NEW : SLOTS_LOAD;
                menu.pending = 0;
                ctx = menu_ctx(&v, sv);
                count = menu_build(&ctx, rows);
                for (ri = 0; ri < count; ri++) {
                    checked++;
                    if (text_w(rows[ri].text) > LOGICAL_W - 8) {
                        printf("FAIL  menu: \"%s\" is %d px, wider than the"
                               " frame\n", rows[ri].text, text_w(rows[ri].text));
                        wide++;
                    }
                    for (ci = 0; rows[ri].text[ci]; ci++)
                        if (rows[ri].text[ci] != ' ' &&
                            font_bits((unsigned char)rows[ri].text[ci],
                                      FONT_STRIDE) == 0) {
                            printf("FAIL  menu: \"%s\" contains '%c', which the"
                                   " font draws as nothing\n",
                                   rows[ri].text, rows[ri].text[ci]);
                            blanks++;
                        }
                }
            }
        }
        /* The headings and footers are drawn text too, and are the one place a
         * long phrase could quietly overflow. */
        {
            static const char *const extra[] = {
                "load game", "replace which save", "replace this save",
                "settings", "controls", "this cannot be undone."
            };
            int n = (int)(sizeof(extra) / sizeof(extra[0]));
            for (ri = 0; ri < n + MFOOT_COUNT + MENU_CONTROL_COUNT; ri++) {
                const char *t = ri < n ? extra[ri]
                              : ri < n + MFOOT_COUNT ? MENU_FOOTERS[ri - n]
                              : MENU_CONTROL_LINES[ri - n - MFOOT_COUNT];
                checked++;
                if (text_w(t) > LOGICAL_W - 8) {
                    printf("FAIL  menu: \"%s\" is %d px, wider than the frame\n",
                           t, text_w(t));
                    wide++;
                }
                for (ci = 0; t[ci]; ci++)
                    if (t[ci] != ' ' &&
                        font_bits((unsigned char)t[ci], FONT_STRIDE) == 0) {
                        printf("FAIL  menu: \"%s\" contains '%c', which the font"
                               " draws as nothing\n", t, t[ci]);
                        blanks++;
                    }
            }
        }
        fails += wide + blanks;
        if (!wide && !blanks)
            printf("menu    : %d labels all fit %d px and contain no blank"
                   " glyphs\n", checked, LOGICAL_W);

        /* NEGATIVE CONTROL. '(' IS in the table's range and IS all zeroes, so a
         * label using it would draw as a hole and every other check above would
         * still pass. If this finds nothing, the loop above measures nothing. */
        if (font_bits('(', FONT_STRIDE) != 0) {
            printf("FAIL  menu: negative control - '(' has bits, so the blank"
                   " glyph check cannot catch a label that uses it\n");
            fails++;
        } else {
            static const char *hole = "quit (really)";
            int found = 0;
            for (ci = 0; hole[ci]; ci++)
                if (hole[ci] != ' ' &&
                    font_bits((unsigned char)hole[ci], FONT_STRIDE) == 0)
                    found++;
            if (!found) {
                printf("FAIL  menu: negative control - \"%s\" passed the blank"
                       " glyph check it should fail\n", hole);
                fails++;
            } else {
                printf("menu    : negative control PASS (a label containing '('"
                       " is caught, %d blank glyphs)\n", found);
            }
        }
    }

    /* ---- 13. every row fits the array it is built into -------------------- */
    {
        static const MenuPage pages[5] = {
            MENU_ROOT, MENU_SLOTS, MENU_CONFIRM, MENU_SETTINGS, MENU_CONTROLS
        };
        int p, worst = 0;
        for (p = 0; p < 5; p++) {
            SDL_zero(menu);
            menu.page    = pages[p];
            menu.started = 1;    /* the root's longest form */
            ctx = menu_ctx(&cfg, slots);
            count = menu_build(&ctx, rows);
            if (count > worst) worst = count;
        }
        if (worst > MENU_ROWS_MAX) {
            printf("FAIL  menu: a page builds %d rows into an array of %d\n",
                   worst, MENU_ROWS_MAX);
            fails++;
        } else {
            printf("menu    : the longest page is %d rows of %d\n",
                   worst, MENU_ROWS_MAX);
        }
    }

    /* ---- 14. the settings file cannot poison the live settings ----------- */
    {
        Settings want;
        want.music = 3; want.sfx = 7; want.fullscreen = 1; want.scale = 4;
        if (cfg_save(&want, cfgpath) != 0) {
            printf("FAIL  menu: could not write the test settings file\n");
            fails++;
        }
        cfg_defaults(&back);
        if (cfg_load(&back, cfgpath) != 0 ||
            back.music != 3 || back.sfx != 7 ||
            back.fullscreen != 1 || back.scale != 4) {
            printf("FAIL  menu: settings did not round trip"
                   " (music %d sfx %d full %d scale %d)\n",
                   back.music, back.sfx, back.fullscreen, back.scale);
            fails++;
        } else {
            printf("menu    : settings round trip PASS\n");
        }

        /* NEGATIVE CONTROLS. Every field's illegal values, one at a time, and
         * each must be REFUSED with the live settings left exactly as they
         * were - the same rule game_load obeys, for the same reason. */
        {
            static const struct { int off; int val; const char *what; } bad[] = {
                { 0, 'X',           "bad magic" },
                { 2, CFG_VERSION+1, "wrong version" },
                { 3, VOL_MAX + 1,   "music above the scale" },
                { 4, 255,           "sound above the scale" },
                { 5, 2,             "fullscreen neither 0 nor 1" },
                { 6, 0,             "scale 0" },
                { 6, WIN_SCALE_MAX + 1, "scale past the maximum" },
                { 7, 1,             "nonzero reserved byte" }
            };
            int n = (int)(sizeof(bad) / sizeof(bad[0])), rejected = 0;

            for (i = 0; i < n; i++) {
                Uint8 buf[CFG_SIZE];
                SDL_RWops *rw;
                Settings live = back, before;

                if (cfg_save(&want, cfgpath) != 0) continue;
                rw = SDL_RWFromFile(cfgpath, "rb");
                if (!rw) continue;
                if (SDL_RWread(rw, buf, 1, CFG_SIZE) != CFG_SIZE) {
                    SDL_RWclose(rw); continue;
                }
                SDL_RWclose(rw);
                buf[bad[i].off] = (Uint8)bad[i].val;
                rw = SDL_RWFromFile(cfgpath, "wb");
                if (!rw) continue;
                SDL_RWwrite(rw, buf, 1, CFG_SIZE);
                SDL_RWclose(rw);

                before = live;
                if (cfg_load(&live, cfgpath) == 0) {
                    printf("FAIL  menu: accepted a settings file with %s\n",
                           bad[i].what);
                    fails++;
                } else if (SDL_memcmp(&live, &before, sizeof(Settings)) != 0) {
                    printf("FAIL  menu: a rejected settings file (%s) still"
                           " changed the live settings\n", bad[i].what);
                    fails++;
                } else {
                    rejected++;
                }
            }
            printf("menu    : rejected %d of %d malformed settings files,"
                   " live settings untouched every time\n", rejected, n);
        }

        /* A missing file is a first run, not a corruption: it must be refused
         * AND leave the defaults in place, which is what main() relies on. */
        {
            Settings live;
            cfg_defaults(&live);
            (void)remove(cfgpath);
            if (cfg_load(&live, cfgpath) == 0) {
                printf("FAIL  menu: loaded settings from a file that is not"
                       " there\n");
                fails++;
            } else if (live.music != VOL_MAX || live.sfx != VOL_MAX ||
                       live.fullscreen != 0 || live.scale != 0) {
                printf("FAIL  menu: a missing settings file disturbed the"
                       " defaults\n");
                fails++;
            } else {
                printf("menu    : a missing settings file leaves the defaults\n");
            }
        }
    }

    /* ---- 15. the volume steps are bounded --------------------------------- */
    {
        Settings v = cfg;
        for (i = 0; i < 50; i++) menu_adjust(&v, MADJ_MUSIC, -1);
        for (i = 0; i < 50; i++) menu_adjust(&v, MADJ_SFX, 1);
        for (i = 0; i < 50; i++) menu_adjust(&v, MADJ_SCALE, 1);
        if (v.music != 0 || v.sfx != VOL_MAX || v.scale != WIN_SCALE_MAX) {
            printf("FAIL  menu: adjust ran past its bounds"
                   " (music %d sfx %d scale %d)\n", v.music, v.sfx, v.scale);
            fails++;
        }
        /* NEGATIVE CONTROL: it must CLAMP, not wrap. Holding right to reach
         * full volume and falling back to silence is the bug this catches. */
        menu_adjust(&v, MADJ_SFX, 1);
        menu_adjust(&v, MADJ_MUSIC, -1);
        if (v.sfx != VOL_MAX || v.music != 0) {
            printf("FAIL  menu: negative control - adjust wrapped past a bound"
                   " (music %d sfx %d)\n", v.music, v.sfx);
            fails++;
        } else {
            printf("menu    : volume and scale clamp at both ends\n");
        }
    }

    /* ---- 16. the volume setting actually reaches the speakers ------------- */
    {
        /* The callback is driven DIRECTLY, with no device open: it is an
         * ordinary function, and calling it here measures the real mix on a
         * machine with no sound card - which is the same machine the game must
         * still run on. This is the end of the path the settings row starts:
         * row -> Settings -> cfg_apply_audio -> atomic -> audio_cb. */
        Audio a;
        float buf[512];
        int   nz_full = 0, nz_mute = 0, nz_sfx = 0;
        int   pass;

        for (pass = 0; pass < 3; pass++) {
            int n = 0;
            SDL_zero(a);
            a.rate     = AUDIO_RATE;
            a.channels = 2;
            a.synth.synth_on = 1;
            a.synth.layers   = (1 << NUM_LAYERS) - 1;
            a.synth.area     = BIOME_FOREST;
            SDL_AtomicSet(&a.music_vol, pass == 0 ? VOL_MAX : 0);
            SDL_AtomicSet(&a.sfx_vol,   VOL_MAX);
            if (pass == 2)
                sfx_fire(&a, SFX_CHIME);
            audio_cb(&a, (Uint8 *)buf, (int)sizeof(buf));
            for (i = 0; i < (int)(sizeof(buf) / sizeof(buf[0])); i++)
                if (buf[i] != 0.0f) n++;
            if (pass == 0) nz_full = n;
            if (pass == 1) nz_mute = n;
            if (pass == 2) nz_sfx  = n;
        }

        if (nz_mute != 0) {
            printf("FAIL  menu: music volume 0 still produced %d nonzero"
                   " samples\n", nz_mute);
            fails++;
        }
        /* NEGATIVE CONTROL. If full volume were ALSO silent, the check above
         * would pass on a mix that never produced anything - which is exactly
         * how a zeroed Audio reads, and exactly the bug the layers test caught
         * the first time these volumes were wired up. */
        if (nz_full == 0) {
            printf("FAIL  menu: negative control - full volume produced silence"
                   " too, so the mute check proves nothing\n");
            fails++;
        }
        if (nz_sfx == 0) {
            printf("FAIL  menu: silencing the music also silenced the SFX; the"
                   " two volumes are not independent\n");
            fails++;
        }
        if (!nz_mute && nz_full && nz_sfx)
            printf("menu    : music 0 is exactly silent, music %d is %d/%d"
                   " samples, and a chime still sounds under a silenced"
                   " score\n", VOL_MAX, nz_full,
                   (int)(sizeof(buf) / sizeof(buf[0])));
    }

    menu_test_clear_slots();
    (void)remove(cfgpath);
    SDL_zero(menu);
    SDL_free(g);
    SDL_free(sc);
    SDL_FreeSurface(fb);
    printf("menu    : %s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* ---- --save-test --------------------------------------------------------
 *
 * The round-trip claim, tested exactly: save, trash the live state, load, and
 * the result must be BIT-identical to the snapshot taken at save time - not
 * "close enough". Then the part that actually matters: a corrupt file must be
 * REJECTED and must leave the target game untouched, because a failed load
 * that half-applies itself is worse than no load at all. */
typedef struct {
    Uint64 seed;
    float  px, py;
    Uint8  abilities;
    /* Both, deliberately: `maps` is what the file carries and `map_tile` is
     * what regenerating plus replaying it must produce. Checking only the mask
     * would pass a load that remembered finding the map and then laid the
     * fragment back down in the world to be found again. */
    Uint8  maps;
    int    map_tile;
    int    frags, souls, region_count;
    Uint8  restored[ENTITY_COUNT];
    int    tile[ENTITY_COUNT];
    float  restore_to[REGION_COUNT];
    float  restoration[REGION_COUNT];
} SaveSnap;

static void save_snap(const Game *g, SaveSnap *s)
{
    int i;
    s->seed = g->seed;
    s->px = g->p.x; s->py = g->p.y;
    s->abilities = g->p.abilities;
    s->maps = g->maps;
    s->map_tile = g->w.map_tile;
    s->frags = g->frags_restored;
    s->souls = g->souls_restored;
    s->region_count = g->w.region_count;
    for (i = 0; i < ENTITY_COUNT; i++) {
        s->restored[i] = g->ents[i].restored;
        s->tile[i] = g->ents[i].tile;
    }
    for (i = 0; i < REGION_COUNT; i++) {
        s->restore_to[i] = g->w.regions[i].restore_to;
        s->restoration[i] = g->w.regions[i].restoration;
    }
}

static int save_snap_eq(const SaveSnap *a, const SaveSnap *b, const char **why)
{
    int i;
    if (a->seed != b->seed)                     { *why = "seed"; return 0; }
    if (a->px != b->px || a->py != b->py)       { *why = "position"; return 0; }
    if (a->abilities != b->abilities)           { *why = "abilities"; return 0; }
    if (a->maps != b->maps)                     { *why = "map mask"; return 0; }
    if (a->map_tile != b->map_tile)             { *why = "map fragment tile"; return 0; }
    if (a->frags != b->frags)                   { *why = "fragment count"; return 0; }
    if (a->souls != b->souls)                   { *why = "soul count"; return 0; }
    if (a->region_count != b->region_count)     { *why = "region count"; return 0; }
    for (i = 0; i < ENTITY_COUNT; i++) {
        if (a->restored[i] != b->restored[i])   { *why = "restored mask"; return 0; }
        if (a->tile[i] != b->tile[i])           { *why = "entity tiles"; return 0; }
    }
    for (i = 0; i < REGION_COUNT; i++) {
        if (a->restore_to[i] != b->restore_to[i])   { *why = "restore_to"; return 0; }
        if (a->restoration[i] != b->restoration[i]) { *why = "restoration"; return 0; }
    }
    return 1;
}

static int save_write_bytes(const char *path, const Uint8 *buf, size_t n)
{
    SDL_RWops *rw = SDL_RWFromFile(path, "wb");
    int ok;
    if (!rw) return -1;
    ok = (SDL_RWwrite(rw, buf, 1, n) == n) ? 0 : -1;
    if (SDL_RWclose(rw) != 0) ok = -1;
    return ok;
}

static int save_selftest(Uint64 base)
{
    const char *path = "wayfarer-savetest.sav";
    Game *g = (Game *)SDL_malloc(sizeof(Game));
    Game *loaded = (Game *)SDL_malloc(sizeof(Game));
    Game *again = (Game *)SDL_malloc(sizeof(Game));
    Scratch *sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    SaveSnap want, got, before;
    Uint8 good[SAVE_SIZE];
    Uint64 ls = 0;
    const char *why = "";
    int fails = 0, i;

    if (!g || !loaded || !again || !sc) {
        printf("FAIL  save: out of memory\n");
        SDL_free(g); SDL_free(loaded); SDL_free(again); SDL_free(sc);
        return 1;
    }
    game_init(g, sc, base);

    /* Deterministic in-play mutations through the SAME transition play uses. */
    for (i = 0; i < 3; i++)
        if (g->ents[i].tile >= 0) game_restore(g, i);
    /* And take this area's map, through the same key play uses - otherwise the
     * round trip would only ever be checked against maps == 0, which a save
     * that dropped byte 21 entirely would satisfy. */
    if (g->w.map_tile >= 0) {
        Audio quiet;
        SDL_zero(quiet);
        g->p.x = (float)(g->w.map_tile % WORLD_W) * TILE + TILE * 0.5f;
        g->p.y = (float)(g->w.map_tile / WORLD_W) * TILE + TILE * 0.5f;
        if (!try_take_map(g, &quiet)) {
            printf("FAIL  save: standing on the map fragment did not take it\n");
            fails++;
        }
    }
    /* Finish the ease, so the load's snap is checked against settled values -
     * exactly what a player who stood still for a second would leave. */
    for (i = 0; i < g->w.region_count; i++)
        g->w.regions[i].restoration = g->w.regions[i].restore_to;

    save_snap(g, &want);
    if (game_save(g, path) != 0) {
        printf("FAIL  save: game_save could not write %s\n", path);
        SDL_free(g); SDL_free(loaded); SDL_free(again); SDL_free(sc);
        return 1;
    }

    /* Trash the live state, so a load that did nothing could not pass. */
    g->p.x += 3.0f * TILE;
    g->p.y -= 2.0f * TILE;

    if (game_load(loaded, sc, path, &ls) != 0) {
        printf("FAIL  save: game_load rejected a file game_save just wrote\n");
        fails++;
    } else {
        if (ls != base) {
            printf("FAIL  save: load reported seed %.0f, saved %.0f\n",
                   (double)ls, (double)base);
            fails++;
        }
        save_snap(loaded, &got);
        if (!save_snap_eq(&want, &got, &why)) {
            printf("FAIL  save: loaded state differs from the saved state: %s\n", why);
            fails++;
        } else {
            printf("save    : round trip PASS, bit-identical to the snapshot\n");
        }
        /* And again: the load must be a pure function of the file. */
        if (game_load(again, sc, path, &ls) != 0) {
            printf("FAIL  save: second load of the same file failed\n");
            fails++;
        } else {
            save_snap(again, &got);
            if (!save_snap_eq(&want, &got, &why)) {
                printf("FAIL  save: second load differs from the first: %s\n", why);
                fails++;
            } else {
                printf("save    : determinism PASS, two loads of one file agree\n");
            }
        }
    }

    /* NEGATIVE CONTROLS. Each must be rejected AND leave `again` - which still
     * holds the last good load - bit-for-bit untouched. */
    {
        SDL_RWops *rw = SDL_RWFromFile(path, "rb");
        size_t n = 0;
        if (rw) { n = SDL_RWread(rw, good, 1, SAVE_SIZE); SDL_RWclose(rw); }
        if (n != SAVE_SIZE) {
            printf("FAIL  save: could not read the file back for the controls\n");
            fails++;
        } else {
            struct { const char *name; Uint8 buf[SAVE_SIZE]; size_t len; } ctl[15];
            int nctl = 0;

            save_snap(again, &before);

            /* Derived from the array, not a literal repeated beside it: the
             * count was hardcoded at 14 and adding a fifteenth control left it
             * with an uninitialised length, which showed up as "could not
             * write" rather than as the control silently not running - but
             * only because the length happened to be garbage rather than
             * SAVE_SIZE. */
            for (i = 0; i < (int)(sizeof(ctl) / sizeof(ctl[0])); i++) {
                SDL_memcpy(ctl[i].buf, good, SAVE_SIZE);
                ctl[i].len = SAVE_SIZE;
            }
            ctl[nctl].name = "truncated file";        ctl[nctl].len = SAVE_SIZE - 1; nctl++;
            ctl[nctl].name = "bad magic";             ctl[nctl].buf[0] = 'X'; nctl++;
            ctl[nctl].name = "wrong version";         ctl[nctl].buf[2] = SAVE_VERSION + 1; nctl++;
            /* 0 was never a legal area byte under v1 (SAVE_AREA was a fixed 1)
             * or v2 ({1,2}), so it stays a clean "not a real area" control. */
            ctl[nctl].name = "wrong area";            ctl[nctl].buf[3] = 0; nctl++;
            ctl[nctl].name = "nonzero reserved byte"; ctl[nctl].buf[22] = 1; nctl++;
            /* A zero timestamp is what a machine whose clock does not work
             * would write, and it is the one value that would sort this save
             * as older than every other one forever - so "continue" could
             * never reach it however recently it was made. Rejected at the
             * door rather than tolerated and sorted last: a save that cannot
             * be the newest one is a save the player will think was lost. */
            ctl[nctl].name = "zero timestamp";
            SDL_memset(ctl[nctl].buf + 28, 0, 8); nctl++;
            /* Both fall straight out of the gameplay invariant the portal
             * enforces live: you cannot BE in Area 2 unless Area 1 is fully
             * restored (only 3 of 10 area-1 bits are set on `good`, so this
             * area-2-labelled copy claims an Area 2 it could not have
             * reached), and Area 1 progress can never carry Area 2 bits. */
            ctl[nctl].name = "area 2 file, area 1 incomplete";
            ctl[nctl].buf[3] = 2; nctl++;
            ctl[nctl].name = "area 1 file, spurious area 2 bits";
            save_put32(ctl[nctl].buf + 24, save_get32(good + 24) | (1u << ENTITY_COUNT));
            nctl++;
            /* One area further: area 1 progress can no more carry Area 3 bits
             * than Area 2 ones - a save cannot hold progress in an area it
             * has not reached yet, regardless of which later area. */
            ctl[nctl].name = "area 1 file, spurious area 3 bits";
            save_put32(ctl[nctl].buf + 24, save_get32(good + 24) | (1u << SAVE_AREA3_SHIFT));
            nctl++;
            /* Same invariant as "area 2 file, area 1 incomplete" above, one
             * area further: you cannot BE in Area 3 unless BOTH earlier areas
             * are fully restored, and `good` has neither. */
            ctl[nctl].name = "area 3 file, area 1 and area 2 incomplete";
            ctl[nctl].buf[3] = 3; nctl++;
            /* Not a crash risk - collision always masks with & - but a
             * hand-edited save must not grant abilities never earned. */
            ctl[nctl].name = "abilities outside the legal mask";
            ctl[nctl].buf[20] |= 0xF8; nctl++;
            /* The checksum: legal bits, but not the set the restored mask
             * accounts for. This is the tamper the mask check exists to catch.
             * XOR rather than a literal, because the value that makes this a
             * real corruption depends on what the good file happens to hold -
             * writing ABIL_ALL here silently reproduced the good file on any
             * seed that had already granted all three, and the control passed
             * while testing nothing. */
            ctl[nctl].name = "abilities the restored mask does not account for";
            ctl[nctl].buf[20] = (Uint8)(good[20] ^ (Uint8)ABIL_KINDLE); nctl++;
            /* Byte 21 stopped being reserved when the map fragment landed, so
             * it needs the controls the reserved bytes used to give it for
             * free. Bits outside the three legal ones first. */
            ctl[nctl].name = "map bits outside the legal mask";
            ctl[nctl].buf[21] |= 0xF8; nctl++;
            /* And the same "progress in an area you have not reached" invariant
             * the restored mask is checked against: a map fragment lies in one
             * area, so an Area 1 save cannot have picked up Area 2's. */
            ctl[nctl].name = "area 1 file, map found in area 2";
            ctl[nctl].buf[21] |= 0x02; nctl++;
            {
                /* NaN, not merely out of bounds: it compares false against
                 * every bound, so a naive (x < 0 || x > max) test lets it in. */
                Uint32 u = 0x7FC00000u;
                ctl[nctl].name = "NaN position";
                save_put32(ctl[nctl].buf + 12, u); nctl++;
            }

            for (i = 0; i < nctl; i++) {
                if (save_write_bytes(path, ctl[i].buf, ctl[i].len) != 0) {
                    printf("FAIL  save: could not write the %s control\n", ctl[i].name);
                    fails++;
                    continue;
                }
                if (game_load(again, sc, path, &ls) == 0) {
                    printf("FAIL  save: %s - a corrupt file was ACCEPTED\n", ctl[i].name);
                    fails++;
                } else {
                    save_snap(again, &got);
                    if (!save_snap_eq(&before, &got, &why)) {
                        printf("FAIL  save: %s - the failed load mutated the game (%s)\n",
                               ctl[i].name, why);
                        fails++;
                    } else {
                        printf("save    : rejected %s, game untouched\n", ctl[i].name);
                    }
                }
            }

            remove(path);
            if (game_load(again, sc, path, &ls) == 0) {
                printf("FAIL  save: a missing file was accepted\n");
                fails++;
            } else {
                printf("save    : rejected missing file, game untouched\n");
            }
        }
    }

    /* The positive control for the checksum control above: a save with NO
     * abilities and an empty mask must still load, or "rejects tampering"
     * would just mean "rejects everything". */
    {
        Game *fresh = (Game *)SDL_malloc(sizeof(Game));
        if (fresh) {
            game_init(fresh, sc, base);
            if (game_save(fresh, path) != 0 || game_load(again, sc, path, &ls) != 0) {
                printf("FAIL  save: an untouched fresh game did not round-trip\n");
                fails++;
            } else {
                printf("save    : an untouched fresh game round-trips\n");
            }
            SDL_free(fresh);
        }
        remove(path);
    }

    /* Area 2 positive path: drive a fresh game to area_complete (Area 1 fully
     * restored, same as the portal itself requires), transition through it
     * exactly the way try_use_portal does, restore two of Area 2's own
     * entities, and confirm a save/load round trip keeps BOTH halves of the
     * mask intact - Area 1's bits frozen at "all restored", Area 2's bits
     * reflecting only what was actually restored there. */
    {
        Game *g2 = (Game *)SDL_malloc(sizeof(Game));
        if (g2) {
            game_init(g2, sc, base);
            for (i = 0; i < ENTITY_COUNT; i++)
                if (g2->ents[i].tile >= 0) game_restore(g2, i);
            if (!area_complete(g2)) {
                printf("FAIL  save: area 1 did not reach area_complete for the area 2 positive path\n");
                fails++;
            } else {
                game_transition_to_area(g2, sc, 2);
                for (i = 0; i < 2; i++)
                    if (g2->ents[i].tile >= 0) game_restore(g2, i);
                for (i = 0; i < g2->w.region_count; i++)
                    g2->w.regions[i].restoration = g2->w.regions[i].restore_to;
                if (game_save(g2, path) != 0 || game_load(again, sc, path, &ls) != 0) {
                    printf("FAIL  save: area 2 game did not round-trip\n");
                    fails++;
                } else if (again->area != 2) {
                    printf("FAIL  save: loaded area 2 save reports area %d\n", (int)again->area);
                    fails++;
                } else {
                    int area1_ok = (again->restored & SAVE_AREA1_BITS) == SAVE_AREA1_BITS;
                    int area2_ok = again->ents[0].restored && again->ents[1].restored;
                    int k;
                    for (k = 2; k < ENTITY_COUNT && area2_ok; k++)
                        if (again->ents[k].restored) area2_ok = 0;
                    if (!area1_ok || !area2_ok) {
                        printf("FAIL  save: area 2 round trip lost area 1 or area 2 progress\n");
                        fails++;
                    } else {
                        printf("save    : area 2 round trip PASS, both areas' progress intact\n");
                    }
                }
            }
            remove(path);
            SDL_free(g2);
        }
    }

    /* Area 3 positive path: the same shape as the Area 2 path above, one area
     * further - drive Area 1 AND Area 2 to area_complete, transition through
     * both portals exactly the way try_use_portal does, restore three of
     * Area 3's own entities, and confirm a save/load round trip keeps ALL
     * THREE slices of the mask intact. This is what actually exercises
     * game_save's generalized keep_mask (a naive 2-area-style "keep the other
     * one" ternary extended to 3 would corrupt Area 2's already-frozen bits
     * here specifically, since Area 2 is neither the area being left nor the
     * one being entered) and game_transition_to_area's generalized fold. */
    {
        Game *g3 = (Game *)SDL_malloc(sizeof(Game));
        if (g3) {
            game_init(g3, sc, base);
            for (i = 0; i < ENTITY_COUNT; i++)
                if (g3->ents[i].tile >= 0) game_restore(g3, i);
            if (!area_complete(g3)) {
                printf("FAIL  save: area 1 did not reach area_complete for the area 3 positive path\n");
                fails++;
            } else {
                game_transition_to_area(g3, sc, 2);
                for (i = 0; i < ENTITY_COUNT; i++)
                    if (g3->ents[i].tile >= 0) game_restore(g3, i);
                if (!area_complete(g3)) {
                    printf("FAIL  save: area 2 did not reach area_complete for the area 3 positive path\n");
                    fails++;
                } else {
                    game_transition_to_area(g3, sc, 3);
                    for (i = 0; i < 3; i++)
                        if (g3->ents[i].tile >= 0) game_restore(g3, i);
                    for (i = 0; i < g3->w.region_count; i++)
                        g3->w.regions[i].restoration = g3->w.regions[i].restore_to;
                    if (game_save(g3, path) != 0 || game_load(again, sc, path, &ls) != 0) {
                        printf("FAIL  save: area 3 game did not round-trip\n");
                        fails++;
                    } else if (again->area != 3) {
                        printf("FAIL  save: loaded area 3 save reports area %d\n", (int)again->area);
                        fails++;
                    } else {
                        int area1_ok = (again->restored & SAVE_AREA1_BITS) == SAVE_AREA1_BITS;
                        int area2_ok = (again->restored & (SAVE_AREA1_BITS << SAVE_AREA2_SHIFT))
                                     == (SAVE_AREA1_BITS << SAVE_AREA2_SHIFT);
                        int area3_ok = again->ents[0].restored && again->ents[1].restored && again->ents[2].restored;
                        int k;
                        for (k = 3; k < ENTITY_COUNT && area3_ok; k++)
                            if (again->ents[k].restored) area3_ok = 0;
                        if (!area1_ok || !area2_ok || !area3_ok) {
                            printf("FAIL  save: area 3 round trip lost area 1, area 2 or area 3 progress\n");
                            fails++;
                        } else {
                            printf("save    : area 3 round trip PASS, all three areas' progress intact\n");
                        }
                    }
                }
            }
            remove(path);
            SDL_free(g3);
        }
    }

    printf("save    : %s\n", fails ? "FAIL" : "PASS");
    SDL_free(g); SDL_free(loaded); SDL_free(again); SDL_free(sc);
    return fails;
}

/* ---- --audio-test -------------------------------------------------------
 *
 * The audio callback runs on a real-time thread against a hard deadline, and a
 * fault there is a release blocker rather than a glitch: NaN, clipping, a
 * partial write or an overrun all reach the speakers directly. Every one of
 * those is a pass/fail condition here, on every invocation.
 *
 * --layers runs the full five-layer engine so the callback is measured under
 * the load a finished area imposes, not an idle one. */

/* Is this frequency a semitone of Ab major - the Forest score's key?
 *
 * Two claims in one, and both matter: that the value is a REAL note rather
 * than an arbitrary float (a transposition bug, or a mistyped digit, lands
 * between semitones), and that it is one of the seven the transcription is
 * allowed to contain. Out-of-key detections were rejected during transcription
 * as intermodulation between the source's square-wave layers rather than notes
 * anybody played; this is what holds that line in the committed tables. */
static int freq_in_key(float hz)
{
    /* Ab major as pitch classes from C: C, Db, Eb, F, G, Ab, Bb. */
    static const int scale[7] = { 0, 1, 3, 5, 7, 8, 10 };
    double m, frac;
    int n, pc, k;

    if (!(hz > 0.0f)) return 0;
    m = 69.0 + 12.0 * (SDL_log((double)hz / 440.0) / SDL_log(2.0));
    n = (int)(m + 0.5);
    frac = m - (double)n;
    if (frac < 0.0) frac = -frac;
    if (frac > 0.02) return 0;          /* not sitting on a semitone at all */
    pc = ((n % 12) + 12) % 12;
    for (k = 0; k < 7; k++)
        if (scale[k] == pc) return 1;
    return 0;
}

/* What step `i` of a table SOUNDS at, expanding the "note on change, 0.0f to
 * hold" convention the tables are written in. */
static float pat_held(const float *pat, int n, int i)
{
    int k;
    for (k = i; k >= 0; k--)
        if (pat[k] > 0.0f) return pat[k];
    (void)n;
    return 0.0f;
}

static int audio_selftest(int argc, char **argv, int ms)
{
    Audio a;
    SDL_AudioSpec have;
    SDL_AudioDeviceID dev;
    double period_ms, max_ms;
    int i, layers, fails = 0;

    SDL_zero(a);
    /* SDL_zero left both volume atomics at 0, which is SILENCE. Set explicitly
     * rather than relying on a default, because what this test asserts - that
     * no sample ever reaches the clamp - is a claim about the WORST case, and
     * the worst case is full volume. Measuring it at anything less would be
     * measuring a mix the player can turn up, and the check would pass by
     * being quiet rather than by being correctly scaled. The first run of this
     * after the volumes landed reported 1.0000 headroom, which is exactly what
     * a silent mix looks like to a clamp counter. */
    SDL_AtomicSet(&a.music_vol, VOL_MAX);
    SDL_AtomicSet(&a.sfx_vol,   VOL_MAX);
    a.noise = arg_flag(argc, argv, "--noise");
    /* --rate exists to exercise the sample-rate-conversion path on a machine
     * whose device happens to match our request exactly. */
    a.req_rate = arg_int(argc, argv, "--rate", AUDIO_RATE);
    a.tone = 1;   /* the tone is what the plain run measures; the game is not tonal */
    rng_seed(&a.rng, (Uint64)arg_int(argc, argv, "--seed", 1), STREAM_AUDIO);

    layers = arg_flag(argc, argv, "--layers");
    if (layers) {
        a.tone = 0;
        a.synth.synth_on = 1;
        SDL_AtomicAdd(&a.layer_fire, FRAGMENT_COUNT);
        SDL_AtomicAdd(&a.voice_fire, SOUL_COUNT);
    }

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("FAIL  audio: SDL_Init(AUDIO): %s\n", SDL_GetError());
        return 1;
    }
    a.cap_cap = AUDIO_RATE * AUDIO_CHANNELS * (ms / 1000 + 2);
    a.cap = (float *)SDL_malloc((size_t)a.cap_cap * sizeof(float));
    if (!a.cap) {
        printf("FAIL  audio: capture buffer alloc\n");
        SDL_Quit();
        return 1;
    }
    dev = audio_open(&a, &have);
    if (!dev) {
        printf("FAIL  audio: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        SDL_free(a.cap);
        SDL_Quit();
        return 1;
    }

    printf("audio   : driver %s\n", SDL_GetCurrentAudioDriver());
    printf("audio   : requested %d Hz F32 %d ch %d frames; obtained %d Hz 0x%04X %d ch %d frames\n",
           a.req_rate, AUDIO_CHANNELS, AUDIO_SAMPLES,
           have.freq, (unsigned)have.format, have.channels, have.samples);
    printf("audio   : signal %s\n",
           layers ? "full 5-layer music + every SFX"
                  : (a.noise ? "seeded white noise" : "440 Hz sine"));

    SDL_PauseAudioDevice(dev, 0);
    if (layers) {
        /* Hammer the SFX triggers from this thread while the callback runs, so
         * the trigger path is exercised under real contention rather than in
         * isolation. Every 40 ms means each sound overlaps its own tail. */
        int left = ms;
        while (left > 0) {
            sfx_fire(&a, left % 120 < 40 ? SFX_CHIME
                                         : (left % 120 < 80 ? SFX_SOUL : SFX_DENY));
            SDL_Delay(40);
            left -= 40;
        }
    } else {
        SDL_Delay((Uint32)ms);
    }
    SDL_PauseAudioDevice(dev, 1);
    SDL_CloseAudioDevice(dev);   /* stops and joins the thread; state is safe to read */

    period_ms = 1000.0 * (double)have.samples / (double)have.freq;
    max_ms = 1000.0 * (double)a.max_ticks / (double)SDL_GetPerformanceFrequency();
    printf("audio   : %.0f calls, %.0f frames (expected ~%.0f for %d ms)\n",
           (double)a.calls, (double)a.frames, (double)have.freq * ms / 1000.0, ms);
    printf("audio   : deadline %.3f ms, worst case %.3f ms, %.1f%% used\n",
           period_ms, max_ms, 100.0 * max_ms / period_ms);
    printf("audio   : partial writes %d (must be 0)\n", a.partial_len);
    if (a.partial_len) fails++;
    if (max_ms > period_ms) {
        printf("FAIL  audio: the callback overran its deadline\n");
        fails++;
    }
    /* A capture that never happened would satisfy every "must be 0" below
     * vacuously. audio_open succeeding but the device never calling back is
     * itself the kind of fault this test exists to catch. */
    if (a.calls == 0 || a.cap_len == 0) {
        printf("FAIL  audio: the callback produced no samples - nothing was measured\n");
        fails++;
    }

    {
        double peak = 0.0, sumsq = 0.0;
        int nan = 0, clipped = 0, at_clamp = 0;
        for (i = 0; i < a.cap_len; i++) {
            float v = a.cap[i];
            if (v != v) { nan++; continue; }
            if (v > 1.0f || v < -1.0f) clipped++;
            /* MIX_GAIN caps the theoretical mix at 0.93, so a sample this close
             * to 1.0 can only be the output clamp engaging - which the "out of
             * range" count above can never see, because the clamp is precisely
             * what keeps it in range. This is the check that catches the mix
             * being too hot, and it caught it: the pre-MIX_GAIN build peaked at
             * exactly 1.0000 here. */
            if (v >= 0.999f || v <= -0.999f) at_clamp++;
            if (v > peak) peak = v;
            if (-v > peak) peak = -v;
            sumsq += (double)v * v;
        }
        printf("audio   : %d samples, peak %.4f, rms %.4f, NaN %d, out of range %d\n",
               a.cap_len, peak, a.cap_len ? SDL_sqrt(sumsq / a.cap_len) : 0.0,
               nan, clipped);
        if (nan)     { printf("FAIL  audio: NaN reached the output buffer\n"); fails++; }
        if (clipped) { printf("FAIL  audio: a sample left [-1, 1]\n"); fails++; }
        if (layers && at_clamp) {
            printf("FAIL  audio: %d samples reached the output clamp - the mix is "
                   "distorting, not just loud\n", at_clamp);
            fails++;
        } else if (layers) {
            printf("audio   : 0 samples reached the clamp (headroom %.4f)\n", 1.0 - peak);
        }
        if (a.cap_len && peak <= 0.01) {
            printf("FAIL  audio: the output is silent\n");
            fails++;
        }
    }

    /* Offline determinism. The music is a pure function of the sample counter,
     * so two fresh states must render bit-identically. The device capture above
     * cannot be compared across runs - its length is wall-clock driven - so
     * this is the repeatable check. */
    {
        Audio x, y;
        int k, det = 1;
        SDL_zero(x); SDL_zero(y);
        x.rate = y.rate = a.rate ? a.rate : AUDIO_RATE;
        for (k = 0; k < NUM_LAYERS; k++) {
            x.synth.layers |= 1 << k;
            y.synth.layers |= 1 << k;
        }
        for (k = 0; k < 96000; k++)
            if (synth_step(&x) != synth_step(&y)) { det = 0; break; }
        if (!det) {
            printf("FAIL  audio: two fresh states diverged at sample %d\n", k);
            fails++;
        } else {
            printf("audio   : determinism PASS, 96000 samples bit-identical\n");
        }

        /* NEGATIVE CONTROL. Two states that genuinely differ must NOT compare
         * equal - otherwise the check above would pass on a synth_step that
         * returned a constant. One layer removed and one sample of head start
         * are both real differences the comparison has to see. */
        {
            Audio p, q;
            int diff_layer = 0, diff_phase = 0;
            SDL_zero(p); SDL_zero(q);
            p.rate = q.rate = x.rate;
            for (k = 0; k < NUM_LAYERS; k++) p.synth.layers |= 1 << k;
            for (k = 0; k < NUM_LAYERS - 1; k++) q.synth.layers |= 1 << k;
            for (k = 0; k < 96000; k++)
                if (synth_step(&p) != synth_step(&q)) { diff_layer = 1; break; }

            SDL_zero(p); SDL_zero(q);
            p.rate = q.rate = x.rate;
            for (k = 0; k < NUM_LAYERS; k++) {
                p.synth.layers |= 1 << k;
                q.synth.layers |= 1 << k;
            }
            (void)synth_step(&q);   /* one sample of head start */
            for (k = 0; k < 96000; k++)
                if (synth_step(&p) != synth_step(&q)) { diff_phase = 1; break; }

            if (!diff_layer || !diff_phase) {
                printf("FAIL  audio: negative control - a missing layer (%s) or an "
                       "offset counter (%s) rendered identically\n",
                       diff_layer ? "seen" : "MISSED", diff_phase ? "seen" : "MISSED");
                fails++;
            } else {
                printf("audio   : negative control PASS, both differences detected\n");
            }
        }
    }

    /* The layer schedule, checked against the table rather than restated: every
     * fragment count from 0 to FRAGMENT_COUNT must produce the documented mask,
     * and layers must only ever be added. */
    {
        Audio s;
        int prev = 0, n;
        SDL_zero(s);
        s.rate = AUDIO_RATE;
        s.synth.synth_on = 1;
        for (n = 0; n <= FRAGMENT_COUNT; n++) {
            SDL_zero(s.synth);
            s.synth.synth_on = 1;
            SDL_AtomicSet(&s.layer_fire, n);
            s.synth.layer_seen = 0;
            synth_latch(&s);
            if ((s.synth.layers & prev) != prev) {
                printf("FAIL  audio: a layer went away between %d and %d fragments\n",
                       n - 1, n);
                fails++;
            }
            prev = s.synth.layers;
        }
        if (!(prev & (1 << LAYER_BELLS))) {
            printf("FAIL  audio: bells never unlock within %d fragments\n", FRAGMENT_COUNT);
            fails++;
        }
        if (prev & (1 << LAYER_VOICE)) {
            printf("FAIL  audio: fragments alone unlocked the Voice of Souls\n");
            fails++;
        }
        printf("audio   : layers only ever accumulate; mask at %d fragments is 0x%02X\n",
               FRAGMENT_COUNT, (unsigned)prev);
    }

    /* ---- The Fantasy Forest score ---------------------------------------
     *
     * The Forest is scored from a supplied track (see the tables). Four things
     * are checked, none of which needs an ear:
     *
     *   1. the loop is the source's own 16 bars at its own tempo;
     *   2. every layer speaks at the loop point, so loop N sounds like loop 1;
     *   3. every note in every table is a real semitone of Ab major;
     *   4. what the SYNTH ACTUALLY EMITS is the melody the table says - measured
     *      by rendering the voice layer through the real synth_step and counting
     *      zero crossings, not by re-reading the table.
     *
     * How it SOUNDS against the source is not asserted anywhere - that is an
     * ear's job, and this file records such claims as unverified. */
    {
        static const struct { const float *pat; const char *name; } forest[NUM_LAYERS] = {
            { SYNTH_BASE, "base" }, { SYNTH_STRINGS, "strings" },
            { SYNTH_PAD, "pad" }, { SYNTH_BELLS, "bells" }, { SYNTH_VOICE, "voice" }
        };
        int li, k, off_key = 0, silent_at_loop = 0, ctl_off_key = 0, strikes[NUM_LAYERS];
        double loop_s = (double)SYNTH_LOOP_AT[BIOME_FOREST] *
                        (double)SYNTH_STEP_S_AT[BIOME_FOREST];
        double uw_s   = (double)SYNTH_LOOP_AT[BIOME_UNDERWORLD] *
                        (double)SYNTH_STEP_S_AT[BIOME_UNDERWORLD];

        /* 1. The loop. 24 s is 16 bars at 160 BPM and divides the source's 96 s
         *    exactly, so the game's loop point is one of the track's own. The
         *    Underworld must NOT have been dragged along with it - a single
         *    global step clock is exactly the mistake this checks for. */
        if (loop_s < 23.999 || loop_s > 24.001) {
            printf("FAIL  audio: the Forest loop is %.4f s, not the source's 24 s\n", loop_s);
            fails++;
        } else if (uw_s < 7.999 || uw_s > 8.001) {
            printf("FAIL  audio: re-scoring the Forest changed the Underworld loop"
                   " to %.4f s (was 8 s)\n", uw_s);
            fails++;
        } else {
            printf("audio   : Forest loop %.2f s (%d x %.4f s = 16 bars at 160 BPM),"
                   " Underworld still %.2f s\n", loop_s, SYNTH_LOOP_AT[BIOME_FOREST],
                   (double)SYNTH_STEP_S_AT[BIOME_FOREST], uw_s);
        }

        /* 2 and 3. */
        for (li = 0; li < NUM_LAYERS; li++) {
            strikes[li] = 0;
            if (!(forest[li].pat[0] > 0.0f)) {
                printf("  the %s layer is silent at step 0 - it would not"
                       " re-articulate at the loop point\n", forest[li].name);
                silent_at_loop++;
            }
            for (k = 0; k < FOREST_STEPS; k++) {
                if (!(forest[li].pat[k] > 0.0f)) continue;
                strikes[li]++;
                if (!freq_in_key(forest[li].pat[k])) {
                    printf("  %s[%d] = %.2f Hz is not a semitone of Ab major\n",
                           forest[li].name, k, (double)forest[li].pat[k]);
                    off_key++;
                }
            }
        }
        /* NEGATIVE CONTROL for 3: notes that are out of key, and a note that is
         * not a semitone at all, must both be rejected. Without this, "nothing
         * off key" would pass just as well on a checker that accepted
         * everything. 440 (A) and 493.88 (B) are the two naturals Ab major does
         * not contain; 450 sits between semitones. */
        if (!freq_in_key(440.0f))   ctl_off_key++;
        if (!freq_in_key(493.88f))  ctl_off_key++;
        if (!freq_in_key(450.0f))   ctl_off_key++;
        if (!freq_in_key(466.16f))  ctl_off_key--;   /* Bb4 IS in key */

        if (off_key || silent_at_loop) {
            printf("FAIL  audio: Forest score - %d off-key note(s), %d layer(s)"
                   " silent at the loop point\n", off_key, silent_at_loop);
            fails++;
        } else if (ctl_off_key != 3) {
            printf("FAIL  audio: key negative control scored %d/3 - the in-key"
                   " check cannot reject, so it proves nothing\n", ctl_off_key);
            fails++;
        } else {
            printf("audio   : Forest score - %d/%d/%d/%d/%d notes across"
                   " base/strings/pad/bells/voice, all in Ab major, all five"
                   " layers speak at the loop point\n",
                   strikes[0], strikes[1], strikes[2], strikes[3], strikes[4]);
        }
        /* The sparkle is meant to be SPARSE. A pluck on every eighth for 24 s
         * is a rattle, and the source only plays it on part of the loop. */
        if (strikes[LAYER_BELLS] * 3 > FOREST_STEPS) {
            printf("FAIL  audio: the bells layer strikes %d of %d steps - too"
                   " dense to read as a sparkle\n", strikes[LAYER_BELLS], FOREST_STEPS);
            fails++;
        }
    }

    /* 4. What comes OUT. The voice layer alone, rendered through the real
     * synth_step at the real rate, its fundamental measured per step by
     * counting upward zero crossings. A table can say anything; this is what
     * the speakers would get.
     *
     * Two full loops are rendered and only the SECOND is measured, so the
     * envelope and the one-pole filter are settled - during the first 0.2 s the
     * attack ramp has the output near zero and there is nothing to count. */
    {
        Audio v;
        int step, pass, bad = 0, ctl_bad = 0;
        int n = (int)(FOREST_STEP_S * (float)AUDIO_RATE);   /* 9000, exactly */
        double win_s;

        SDL_zero(v);
        v.rate = AUDIO_RATE;
        v.synth.area = BIOME_FOREST;
        v.synth.layers = 1 << LAYER_VOICE;
        win_s = (double)(n - n / 5) / (double)AUDIO_RATE;

        for (pass = 0; pass < 2; pass++)
            for (step = 0; step < FOREST_STEPS; step++) {
                float prev = 0.0f, want;
                double got;
                int i2, cross = 0;
                for (i2 = 0; i2 < n; i2++) {
                    float x = synth_step(&v);
                    if (i2 >= n / 5 && prev < 0.0f && x >= 0.0f) cross++;
                    prev = x;
                }
                if (!pass) continue;
                want = pat_held(SYNTH_VOICE, FOREST_STEPS, step);
                got  = (double)cross / win_s;
                /* 3%: a semitone is 5.9%, and one miscounted crossing over a
                 * 0.15 s window is 1.4% at this register. */
                if (want > 0.0f && SDL_fabs(got - want) > 0.03 * want) {
                    if (bad < 4)
                        printf("  voice step %d: table says %.2f Hz, the synth"
                               " emitted %.1f Hz\n", step, (double)want, got);
                    bad++;
                }
                /* NEGATIVE CONTROL: the same comparison against the melody an
                 * octave up must FAIL. If it did not, the tolerance would be so
                 * wide that the check above could not see a wrong note. */
                if (want > 0.0f && SDL_fabs(got - want * 2.0f) > 0.03 * want * 2.0)
                    ctl_bad++;
            }

        if (bad) {
            printf("FAIL  audio: the synth emitted the wrong pitch on %d of %d"
                   " melody steps\n", bad, FOREST_STEPS);
            fails++;
        } else if (ctl_bad < FOREST_STEPS - 4) {
            printf("FAIL  audio: pitch negative control - an octave-transposed"
                   " melody matched at %d of %d steps, so the tolerance is too"
                   " wide to catch a wrong note\n",
                   FOREST_STEPS - ctl_bad, FOREST_STEPS);
            fails++;
        } else {
            printf("audio   : the rendered melody matches the score at all %d"
                   " steps within 3%%; control rejected the octave-up melody at"
                   " %d of them\n", FOREST_STEPS, ctl_bad);
        }
    }

    printf("audio   : %s (%d checks failed)\n", fails ? "FAIL" : "PASS", fails);
    SDL_free(a.cap);
    SDL_Quit();
    return fails ? 1 : 0;
}
#endif /* WAYFARER_SELFTEST */
#if WAYFARER_SELFTEST
/* ---- Art atlas ----------------------------------------------------------
 *
 * Self-test builds only, now that the real tile renderer has taken over the
 * shipping view. It stays because it is the only way to inspect every baked
 * sprite directly, and it costs the shipping binary nothing.
 *
 * It earns its bytes: it is the only way
 * to answer "did the bake and the decoder actually reproduce the delivered art"
 * by looking rather than by assertion. A passing --sprite-test proves a stream
 * decodes to the right PIXEL COUNT; only the screen proves it decodes to the
 * right picture. Replaced by the real tile renderer in phase 2.
 *
 * Pages: 1 tileset grid, 2 decorations, 3 character frames, 4 the fog ramp,
 * 5 Underworld art, 6 Lumiara art (page arg is -1-based; see atlas_page). */
static void draw_atlas(SDL_Surface *fb, int page, float t)
{
    Uint32 bg = SDL_MapRGB(fb->format, 0x2a, 0x2a, 0x32);
    Uint32 grid = SDL_MapRGB(fb->format, 0x3a, 0x3a, 0x46);
    Uint32 base = SDL_MapRGB(fb->format, 0x4a, 0x3a, 0x2a);
    int i, x, y, top = FOG_LEVELS - 1;

    fill_rect(fb, 0, 0, LOGICAL_W, LOGICAL_H, bg);

    if (page == 0) {
        /* The tileset exactly as authored: 8 x 15 cells of 16 px. Cells with no
         * content stay background, which is itself the check - the gaps should
         * be where the source PNG is empty. */
        int ox = (LOGICAL_W - ART_TILE_COLS * TILE) / 2;
        int oy = (LOGICAL_H - ART_TILE_ROWS * TILE) / 2;
        for (y = 0; y <= ART_TILE_ROWS; y++)
            fill_rect(fb, ox - 1, oy + y * TILE - 1, ART_TILE_COLS * TILE + 2, 1, grid);
        for (x = 0; x <= ART_TILE_COLS; x++)
            fill_rect(fb, ox + x * TILE - 1, oy - 1, 1, ART_TILE_ROWS * TILE + 2, grid);
        /* Indexed through ART_TILE_AT, not by enum order: 28 of the 120 cells are
         * empty, so the enum skips them and index != row*cols+col. Deriving the
         * cell from the index compacted the sheet upward by two rows, which is
         * exactly the sort of thing only the screen catches. */
        for (y = 0; y < ART_TILE_ROWS; y++)
            for (x = 0; x < ART_TILE_COLS; x++)
                if (ART_TILE_AT[y][x] != ART_NONE)
                    draw_sprite(fb, ART_TILE_AT[y][x], ox + x * TILE, oy + y * TILE, top);
    } else if (page == 1) {
        /* Decorations on a baseline, so the bottom-centre ground anchor is
         * visible: every object should sit ON the line, not float or sink. */
        int bx = 8, by = 130;
        fill_rect(fb, 0, by, LOGICAL_W, 1, base);
        fill_rect(fb, 0, by + 130, LOGICAL_W, 1, base);
        for (i = ART_LOG_A; i <= ART_PINE_B; i++) {
            const ArtSprite *sp = &ART_SPRITES[i];
            if (bx + sp->w > LOGICAL_W) { bx = 8; by += 130; }
            draw_sprite(fb, i, bx + sp->w / 2, by, top);
            bx += sp->w + 3;
        }
    } else if (page == 2) {
        /* Eight sheets, eight frames each, every frame drawn at its own anchor
         * on a shared baseline per row. If the cell-relative anchors are right,
         * each row's feet stay pinned and only the body moves. */
        for (i = 0; i < 64; i++) {
            int row = i / 8, col = i % 8;
            int gx = 30 + col * 56, gy = 34 + row * 30;
            fill_rect(fb, gx - 14, gy, 28, 1, grid);
            draw_sprite(fb, ART_CH_IDLE_DOWN_0 + i, gx, gy, top);
        }
    } else if (page == 3) {
        /* The fog ramp. Left to right is reveal 0 -> full. Shape and the
         * light-to-dark ordering must survive at every step; only colour goes. */
        int n = 8;
        for (i = 0; i < n; i++) {
            int lv = i * (FOG_LEVELS - 1) / (n - 1);
            int gx = 30 + i * 56;
            draw_sprite(fb, ART_TREE_A, gx, 120, lv);
            draw_sprite(fb, ART_ROCK_A, gx, 150, lv);
            draw_sprite(fb, ART_CH_WALK_DOWN_0, gx, 180, lv);
            draw_sprite(fb, ART_TILE_C1_R1, gx - 8, 190, lv);
            draw_sprite(fb, ART_MUSHROOM_BIG_A, gx, 230, lv);
        }
        /* A slow sweep of the whole scene through the ramp, so banding between
         * the 32 levels would show as a visible step rather than a guess. */
        {
            int lv = (int)(t * 6.0f) % (FOG_LEVELS * 2);
            if (lv >= FOG_LEVELS) lv = FOG_LEVELS * 2 - 1 - lv;
            draw_sprite(fb, ART_TREE_B, LOGICAL_W / 2, 100, lv);
            draw_sprite(fb, ART_PINE_A, LOGICAL_W / 2 + 80, 100, lv);
        }
    } else if (page == 4) {
        /* Underworld curated tiles, decorations and the portal, laid out the
         * same way pages 0/1 check Forest art - a visual check on curation
         * picked with tools/inspect-grid.ps1, not a gameplay path. */
        int gx = 8, gy = 8;
        for (i = ART_UW_ACID_NW; i <= ART_UW_ACIDFILL_D; i++) {
            draw_sprite(fb, i, gx, gy, top);
            gx += TILE + 2;
            if (gx > LOGICAL_W - TILE) { gx = 8; gy += TILE + 2; }
        }
        {
            int bx = 8, by = 90;
            fill_rect(fb, 0, by, LOGICAL_W, 1, base);
            for (i = ART_UW_TREE_1; i <= ART_UW_REED_3; i++) {
                const ArtSprite *sp = &ART_SPRITES[i];
                if (bx + sp->w > LOGICAL_W) { bx = 8; by += 46; }
                draw_sprite(fb, i, bx + sp->w / 2, by, top);
                bx += sp->w + 3;
            }
        }
        {
            int frame = (int)(t * 6.0f) % 6;
            draw_sprite(fb, ART_UW_PORTAL_A + frame, LOGICAL_W - 40, LOGICAL_H - 20, top);
        }
    } else {
        /* Lumiara curated tiles and decorations, same layout again. The
         * dreamgate (Area 2's exit to Area 3) is previewed bottom-right the
         * way page 4 previews the green swirl - one frame, not a cycle, so
         * there is no animation to step through here. */
        int gx = 8, gy = 8;
        for (i = ART_LUM_GRASS_A; i <= ART_LUM_VOIDBORDER_NE; i++) {
            draw_sprite(fb, i, gx, gy, top);
            gx += TILE + 2;
            if (gx > LOGICAL_W - TILE) { gx = 8; gy += TILE + 2; }
        }
        {
            int bx = 8, by = 90;
            fill_rect(fb, 0, by, LOGICAL_W, 1, base);
            for (i = ART_LUM_TREE; i <= ART_LUM_STAG; i++) {
                const ArtSprite *sp = &ART_SPRITES[i];
                if (bx + sp->w > LOGICAL_W) { bx = 8; by += 46; }
                draw_sprite(fb, i, bx + sp->w / 2, by, top);
                bx += sp->w + 3;
            }
        }
        draw_sprite(fb, ART_LUM_PORTAL, LOGICAL_W - 40, LOGICAL_H - 8, top);
    }
}

#endif /* WAYFARER_SELFTEST */

/* ---- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Surface *fb, *back, *draw;
    void *back_px = NULL;
    Game *g;
    Scratch *sc;
    DrawList *dl;
    Audio audio;
    SDL_AudioDeviceID dev = 0;
    SDL_AudioSpec have;
    int scale, running = 1, frame = 0, limit;
    Settings cfg;
    /* What is in each of the six slots. Refreshed when the menu opens and after
     * anything that writes a save, NOT every frame the menu is drawn: the rows
     * are rebuilt at 60 Hz and six file reads a frame, to answer a question
     * that cannot change without one of those events happening first, would be
     * a syscall storm in the render path. */
    SaveSlot slots[SAVE_SLOTS];
    char     slot_path[SAVE_PATH_MAX];
    /* The slot F5 writes and F9 re-reads: the one she is playing. Fixed when a
     * new game is begun or a save is loaded, -1 until then - which is where a
     * --frames run stays, since it never passes through the menu. */
    int cur_slot = -1;
    /* Has anything the settings file holds changed this session? Only then is
     * the file written on the way out. Without it, every first run would drop a
     * wayfarer.cfg recording whatever scale pick_scale happened to choose on
     * whatever display was attached - locking in a window size nobody asked
     * for, on the strength of having once started the game. */
    int cfg_dirty = 0;
    int cam_x = 0, cam_y = 0;
    Uint64 perf, prev, now, seed, frame_due = 0;
    float acc = 0.0f, clock = 0.0f;
    /* Where the player stood at the START of the most recent tick. The renderer
     * draws between this and where she stands now - see the interpolation
     * comment below the tick loop. */
    float prev_px = 0.0f, prev_py = 0.0f;
#if WAYFARER_SELFTEST
    const char *shot = NULL;
    int atlas_page = -1;
    int lit_mode;
    int cam_tx, cam_ty;
    int lean_x = 0, lean_y = 0;
    int i;

    /* Dispatched before any window or audio exists, and the process exit code IS
     * the failure count, so a harness needs no output parsing. */
    if (arg_flag(argc, argv, "--sprite-test"))   return sprite_selftest();
    if (arg_flag(argc, argv, "--decode-test"))   return decode_selftest();
    if (arg_flag(argc, argv, "--fog-test"))      return fog_selftest();
    if (arg_flag(argc, argv, "--autotile-test")) return autotile_selftest();
    if (arg_flag(argc, argv, "--tile-test"))     return tile_selftest();
    if (arg_flag(argc, argv, "--sort-test"))     return sort_selftest();
    if (arg_flag(argc, argv, "--mockup-test"))   return mockup_selftest();
    if (arg_flag(argc, argv, "--font-test"))     return font_selftest();
    if (arg_flag(argc, argv, "--menu-test"))     return menu_selftest();
    if (arg_flag(argc, argv, "--hud-test"))
        return hud_selftest((Uint64)arg_int(argc, argv, "--seed", 1));
    if (arg_flag(argc, argv, "--map-test"))
        return map_selftest(arg_int(argc, argv, "--seeds", 8),
                            (Uint64)arg_int(argc, argv, "--seed", 1));
    if (arg_flag(argc, argv, "--save-test"))
        return save_selftest((Uint64)arg_int(argc, argv, "--seed", 1));
    if (arg_flag(argc, argv, "--audio-test"))
        return audio_selftest(argc, argv, arg_int(argc, argv, "--audio-test", 600));
    if (arg_flag(argc, argv, "--move-test"))
        return move_selftest(arg_int(argc, argv, "--seeds", 20),
                             (Uint64)arg_int(argc, argv, "--seed", 1));
    if (arg_flag(argc, argv, "--gating-test"))
        return gating_selftest(arg_int(argc, argv, "--seeds", 20),
                               (Uint64)arg_int(argc, argv, "--seed", 1));
    if (arg_flag(argc, argv, "--reach-test"))
        return reach_selftest(arg_int(argc, argv, "--seeds", 30),
                              (Uint64)arg_int(argc, argv, "--seed", 1));
    if (arg_flag(argc, argv, "--play-test"))
        return play_selftest(arg_int(argc, argv, "--seeds", 20),
                             (Uint64)arg_int(argc, argv, "--seed", 1));

    for (i = 1; i < argc - 1; i++)
        if (SDL_strcmp(argv[i], "--shot") == 0)
            shot = argv[i + 1];
    if (arg_flag(argc, argv, "--atlas"))
        atlas_page = arg_int(argc, argv, "--atlas", 0);
    lit_mode = arg_flag(argc, argv, "--lit");
    /* --camx/--camy: park the camera on a named TILE instead of on her.
     *
     * She spawns in the middle of the largest open component, so a --frames run
     * can only ever photograph that one neighbourhood - and the map BORDER, the
     * one place the ground is guaranteed to be rock, is the furthest thing from
     * it. Terrain bugs at the edge of the map were therefore unphotographable,
     * which is the same "some states only a screenshot can settle" problem
     * --lit, --dev and --solidmap each exist to solve.
     *
     * The camera only, deliberately: nothing about the simulation, her
     * position, or what gets saved changes, so a shot taken this way is of the
     * same world an ordinary run would produce. -1 means "follow her". */
    cam_tx = arg_int(argc, argv, "--camx", -1);
    cam_ty = arg_int(argc, argv, "--camy", -1);
    /* --leanx/--leany: hold a walk direction for the whole run, so a --frames
     * shot can photograph something that only exists WHILE she is pushing into
     * it. The refusal toasts are exactly that, and --standon below is what puts
     * her next to the thing to push into. See the injection in the tick loop. */
    lean_x = arg_int(argc, argv, "--leanx", 0);
    lean_y = arg_int(argc, argv, "--leany", 0);
#endif

    limit = arg_int(argc, argv, "--frames", 0);
    seed  = (Uint64)arg_int(argc, argv, "--seed", 1);

    g  = (Game *)SDL_malloc(sizeof(Game));
    dl = (DrawList *)SDL_malloc(sizeof(DrawList));
    /* Scratch is ~224 KB and is only needed while generating - but game_load
     * regenerates, so it is kept for the session rather than freed after the
     * first world. Still heap, never static: see the rule at the top. */
    sc = (Scratch *)SDL_malloc(sizeof(Scratch));
    if (!g || !dl || !sc) {
        SDL_free(g); SDL_free(dl); SDL_free(sc);
        return fatal("Wayfarer could not allocate the world.", 6);
    }
    game_init(g, sc, seed);
#if WAYFARER_SELFTEST
    /* Jump straight to Area 2 or Area 3 without playing through the areas
     * before it - for looking at the Underworld or Lumiara biome itself, the
     * same reason --dev and --lit exist. g->restored stays 0 rather than
     * being pre-seeded with fake completion bits for the skipped areas: nothing
     * here checks area_complete or the portal for the jumped-to area, so an
     * empty mask is simplest and matches what --area2 has always done. */
    if (arg_flag(argc, argv, "--area2")) {
        g->restored = 0;
        game_init_area(g, sc, seed, 2);
    } else if (arg_flag(argc, argv, "--area3")) {
        g->restored = 0;
        game_init_area(g, sc, seed, 3);
    }
#endif
    prev_px = g->p.x;
    prev_py = g->p.y;
#if WAYFARER_SELFTEST
    if (arg_flag(argc, argv, "--dev"))
        g->p.abilities = ABIL_ALL;
    /* --lit exists so the minimap and the restored palette can be LOOKED AT.
     * Both are almost entirely a function of accumulated fog, so a fresh
     * --frames run shows neither, and every visual bug of consequence in this
     * project's predecessor was found by looking at the screen. lit_mode is
     * remembered (not just applied once here) and re-applied at every point
     * below that regenerates World.reveal - see reveal_all. */
    if (lit_mode)
        reveal_all(&g->w);
    /* --restored: every region fully restored, WITHOUT touching reveal.
     *
     * Separate from --lit on purpose, and the separation is the point. Fog has
     * two independent channels - sight, which walking accumulates and which
     * caps at SIGHT_MAX, and restoration, which a fragment sets region-wide -
     * and --lit saturates SIGHT, which pins tile_level at maximum for every
     * tile on the map and hides whatever the restoration channel is doing.
     * A whole round of pond screenshots was taken under --lit and could not
     * have shown the bug this flag exists to photograph: water and rock
     * staying dim in a fully restored region. Looking at the screen only
     * settles things if the flag you look through is not painting over the
     * thing you are looking for. */
    if (arg_flag(argc, argv, "--restored")) {
        int ri;
        for (ri = 0; ri < g->w.region_count; ri++) {
            g->w.regions[ri].restoration = 1.0f;
            g->w.regions[ri].restore_to  = 1.0f;
        }
    }
    /* --standon pond|edge|ent: stand her where a feature can be photographed.
     *
     * Same reason as --camx/--camy, one step further: the camera flag reaches
     * the map edge but leaves her at spawn, and the three things added this
     * round - the two refusal toasts and the interact prompt - are all
     * properties of where SHE is, not of where the camera is. A fresh --frames
     * run stands her in the middle of the largest clearing, which is by
     * construction the one place none of them can happen.
     *
     * Only her position moves. The world is the world the seed generated, and
     * everything downstream - refusal_at, the toast, the prompt - runs exactly
     * as it would if she had walked here. */
    {
        const char *where = NULL;
        for (i = 1; i < argc - 1; i++)
            if (SDL_strcmp(argv[i], "--standon") == 0) where = argv[i + 1];
        if (where) {
            int want_water = (SDL_strcmp(where, "pond") == 0);
            int want_edge  = (SDL_strcmp(where, "edge") == 0);
            int want_map   = (SDL_strcmp(where, "map") == 0);
            int best = -1, bestd = 1 << 30, tx, ty;
            int sx = (int)g->p.x / TILE, sy = (int)g->p.y / TILE;
            if (want_map) {
                best = g->w.map_tile;
            } else if (want_water || want_edge) {
                /* The open tile NEAREST HER that has the wanted wall to its
                 * east, so --leanx 1 walks into it. Nearest so the shot is of
                 * terrain she could plausibly have reached. */
                for (ty = 1; ty < WORLD_H - 1; ty++)
                    for (tx = 1; tx < WORLD_W - 1; tx++) {
                        int d = (tx - sx) * (tx - sx) + (ty - sy) * (ty - sy);
                        if (g->w.solid[ty][tx] || d >= bestd) continue;
                        if (want_edge  && tx != WORLD_W - 2) continue;
                        if (want_water && g->w.terr[ty][tx + 1] != GT_WATER) continue;
                        bestd = d; best = ty * WORLD_W + tx;
                    }
            } else {
                for (i = 0; i < ENTITY_COUNT; i++)
                    if (g->ents[i].tile >= 0) { best = g->ents[i].tile; break; }
            }
            if (best >= 0) {
                g->p.x = (float)(best % WORLD_W) * TILE + TILE * 0.5f;
                g->p.y = (float)(best / WORLD_W) * TILE + TILE * 0.5f;
                /* One tile WEST of the mote for the entity case, so the shot
                 * shows the keycap, the mote AND her as three separate things
                 * rather than her sprite covering the other two. 16 px is well
                 * inside INTERACT_RADIUS's 22. */
                if (!want_water && !want_edge) g->p.x -= TILE;
                prev_px = g->p.x;
                prev_py = g->p.y;
            }
        }
    }
    /* --map: hand her this area's map and open the screen, for the same reason
     * --lit, --restored and --standon exist. The map screen is unreachable from
     * a --frames run - it needs a fragment found somewhere across the biome and
     * then a keypress - so without this the one thing that can settle how it
     * LOOKS, a screenshot, could never be taken of it.
     *
     * Goes through the real g->maps bit and the real hud.map_open, so what is
     * photographed is the shipping screen, not a test-only drawing path. Worth
     * pairing with --dev: trails are routed with the abilities she HOLDS, so a
     * fresh player sees several of them stop dead at a gate.
     *
     * AFTER --standon, which has a `map` case of its own that stands her where
     * the fragment lies - this clears map_tile, so running first would leave
     * that case nothing to find. */
    if (arg_flag(argc, argv, "--map")) {
        g->maps |= (Uint8)(1u << (g->area - 1));
        g->w.map_tile = -1;
        hud.map_open = 1;
        hud.bm_dirty = 1;
    }
#endif

    /* Settings before SDL, because the window is sized from them. A missing or
     * malformed file is a first run, not an error: cfg_load leaves the defaults
     * in place and nothing is said about it. */
    cfg_defaults(&cfg);
    (void)cfg_load(&cfg, CFG_FILENAME);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return fatal("Wayfarer could not start SDL's video subsystem, so it cannot "
                     "open a window.", 1);

    /* AUDIO is a SEPARATE InitSubSystem call, and its failure is not fatal:
     * a machine with no sound device must still play the game silently rather
     * than refuse to start. That is load-bearing, not politeness - it is
     * exactly how the isometric build failed on one such machine. */
    SDL_zero(audio);
    audio.req_rate = AUDIO_RATE;
    rng_seed(&audio.rng, seed, STREAM_AUDIO);
    /* BEFORE audio_open, not after: SDL_zero left both volume atomics at 0, so
     * a device that started first would render a buffer of silence and the game
     * would open with a fade-in nobody asked for. */
    cfg_apply_audio(&audio, &cfg);
    if (!arg_flag(argc, argv, "--mute") && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        dev = audio_open(&audio, &have);
        if (dev) {
            audio.synth.synth_on = 1;
            SDL_PauseAudioDevice(dev, 0);
        }
    }

    /* --scale beats the stored setting, which beats pick_scale: an explicit
     * flag on the command line is the most specific thing anyone said. The
     * chosen value goes back into cfg so the settings row shows the scale
     * actually in use rather than a 0 nobody picked. */
    scale = arg_int(argc, argv, "--scale", 0);
    if (scale < 1 || scale > WIN_SCALE_MAX)
        scale = (cfg.scale >= 1 && cfg.scale <= WIN_SCALE_MAX) ? cfg.scale
                                                               : pick_scale();
    cfg.scale = scale;

    win = SDL_CreateWindow("Wayfarer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           LOGICAL_W * scale, LOGICAL_H * scale,
                           SDL_WINDOW_SHOWN |
                           (cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) {
        SDL_Quit();
        return fatal("Wayfarer could not create its window.", 2);
    }

    fb = SDL_GetWindowSurface(win);
    if (!fb) {
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fatal("Wayfarer could not get a drawing surface for its window.", 3);
    }
    if (fb->format->BytesPerPixel != 4) {
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fatal("Wayfarer needs a 32-bit-per-pixel display surface.", 4);
    }

    /* Allocated even at scale 1, where it is pure overhead: a rendering path
     * that only exists above a threshold is a path that only gets tested above
     * one. NULL is a real degraded mode, not a crash - render() then draws
     * straight into the window surface at native resolution. */
    back = backbuffer_new(fb, &back_px);

    /* Built against the surface we will actually draw into, because SDL_MapRGB
     * depends on its format. Fatal: with no palette there is nothing to draw. */
    if (!fogpal_build(back ? back : fb)) {
        if (back) SDL_FreeSurface(back);
        if (back_px) SDL_free(back_px);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return fatal("Wayfarer could not allocate its colour tables.", 5);
    }

    /* The title menu, unless --frames was given.
     *
     * --frames is the screenshot and smoke-test flag, and every recipe using it
     * - in CLAUDE.md, in README.md, in anything anyone has scripted - means
     * "photograph the world". Opening a menu in front of that would silently
     * change what every one of those commands captures, which is a worse bug
     * than having no menu: the shots would keep being taken and keep looking
     * fine. So a --frames run plays straight through, exactly as before, and
     * --menu below is how the menu itself gets photographed. */
    SDL_memset(slots, 0, sizeof(slots));
    if (!limit) {
        menu.started = 0;
        (void)save_scan(slots);
        /* On a very first launch the caret lands on "new game", because
         * "continue" above it is dim - a caret resting on a row that does
         * nothing is the menu looking broken before a key has been pressed. */
        menu_open(MENU_ROOT, &cfg, slots);
    }
#if WAYFARER_SELFTEST
    /* --menu: open the menu for a screenshot, the same favour --map does for
     * the map screen and for the same reason - it is otherwise unreachable
     * from a --frames run, which is the only run that can take a picture.
     * AFTER the --frames suppression above, so it wins over it. */
    if (arg_flag(argc, argv, "--menu")) {
        const char *pg = NULL;
        MenuPage page = MENU_ROOT;
        menu.started = arg_flag(argc, argv, "--paused");
        (void)save_scan(slots);
        for (i = 1; i < argc - 1; i++)
            if (SDL_strcmp(argv[i], "--menupage") == 0) pg = argv[i + 1];
        if (pg && SDL_strcmp(pg, "settings") == 0) page = MENU_SETTINGS;
        if (pg && SDL_strcmp(pg, "controls") == 0) page = MENU_CONTROLS;
        if (pg && SDL_strcmp(pg, "load")     == 0) { page = MENU_SLOTS;
                                                     menu.mode = SLOTS_LOAD; }
        if (pg && SDL_strcmp(pg, "replace")  == 0) { page = MENU_SLOTS;
                                                     menu.mode = SLOTS_NEW; }
        if (pg && SDL_strcmp(pg, "confirm")  == 0) { page = MENU_CONFIRM;
                                                     menu.pending = 0; }
        /* Through menu_open, like every other way in: the point of the flag is
         * to photograph what a player sees, and a selection set by hand here
         * would be photographing something only the flag can produce. */
        menu_open(page, &cfg, slots);
    }
#endif

    perf = SDL_GetPerformanceFrequency();
    prev = SDL_GetPerformanceCounter();

    while (running) {
        SDL_Event ev;
        double elapsed;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            /* The menu consumes the whole key event before the play switch ever
             * sees it, so nothing below can fire underneath an open menu.
             * Event-driven, unlike walking: the movement keys are polled from
             * SDL_GetKeyboardState once per tick, and a selection moved that
             * way would run the length of the list on a single press. */
            else if (ev.type == SDL_KEYDOWN && menu.page != MENU_NONE) {
                MenuRow    rows[MENU_ROWS_MAX];
                MenuCtx    ctx   = menu_ctx(&cfg, slots);
                int        count = menu_build(&ctx, rows);
                SDL_Keycode k = ev.key.keysym.sym;
                MenuAction act = MA_NONE;
                /* Which slot the chosen row was about, and which slot a new
                 * game should begin in. Collected here rather than acted on
                 * inside the switch because three different rows can decide to
                 * begin a new game, and one implementation of "begin" below is
                 * one place for it to be right. */
                int act_slot = -1, start_slot = -1, load_slot = -1;

                /* The rows are rebuilt from live state, so the caret can be
                 * left sitting on one that has since been disabled - a load
                 * that fails turns "load game" off underneath it. Normalised
                 * BEFORE the key is interpreted, so enter can never resolve
                 * against a row that is no longer offering anything. */
                if (count > 0 && (menu.sel < 0 || menu.sel >= count ||
                                  !rows[menu.sel].enabled))
                    menu.sel = menu_step(rows, count,
                                         (menu.sel < 0 || menu.sel >= count)
                                         ? 0 : menu.sel, 1);

                switch (k) {
                case SDLK_UP:
                case SDLK_w:
                    menu.sel = menu_step(rows, count, menu.sel, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                    menu.sel = menu_step(rows, count, menu.sel, 1);
                    break;
                case SDLK_LEFT:
                case SDLK_a:
                case SDLK_RIGHT:
                case SDLK_d:
                    if (menu.sel >= 0 && menu.sel < count &&
                        rows[menu.sel].enabled &&
                        rows[menu.sel].adjust != MADJ_NONE) {
                        int step = (k == SDLK_RIGHT || k == SDLK_d) ? 1 : -1;
                        menu_adjust(&cfg, rows[menu.sel].adjust, step);
                        /* Applied on the keypress rather than on leaving the
                         * page: a volume you cannot hear while you set it is a
                         * volume you have to set twice. */
                        cfg_apply_audio(&audio, &cfg);
                        cfg_apply_window(win, &cfg);
                        cfg_dirty = 1;
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    act = menu_act(rows, count, menu.sel);
                    if (menu.sel >= 0 && menu.sel < count)
                        act_slot = rows[menu.sel].slot;
                    break;
                case SDLK_ESCAPE:
                    /* Back out one page, and off the root only if there is a
                     * game to go back to. On the title screen escape does
                     * nothing: quitting is a row you choose, not a key that
                     * ends the process behind your back - which is exactly
                     * what it used to do. */
                    if (menu.page != MENU_ROOT)   act = MA_BACK;
                    else if (menu.started)        act = MA_START;
                    break;
                case SDLK_F11:
                    /* Still live inside the menu, and it goes through the
                     * setting rather than around it, so the fullscreen row and
                     * the key can never disagree about the current state. */
                    cfg.fullscreen = !cfg.fullscreen;
                    cfg_apply_window(win, &cfg);
                    cfg_dirty = 1;
                    break;
                default:
                    break;
                }

                switch (act) {
                case MA_START:
                    menu.page    = MENU_NONE;
                    menu.started = 1;
                    /* The interpolator draws between these and where she stands
                     * now; nothing moved while the menu was up, so this is the
                     * same statement every other state jump in here makes. */
                    prev_px = g->p.x;
                    prev_py = g->p.y;
                    break;

                /* Continue is a load of whichever slot was written last, and
                 * nothing more - the row carries that slot, worked out by the
                 * same save_newest the list marks "newest" with, so the two can
                 * never disagree about which save continue would take. */
                case MA_CONTINUE:
                    load_slot = act_slot;
                    break;

                case MA_NEWGAME:
                    /* The lowest free slot, without asking. Only when there is
                     * none does the player get a question, and then it is
                     * "which of these do you want to replace" rather than a
                     * choice made for them - nothing here overwrites a save
                     * that was not named. */
                    start_slot = save_first_free(slots);
                    if (start_slot < 0) {
                        menu.mode = SLOTS_NEW;
                        menu_open(MENU_SLOTS, &cfg, slots);
                    }
                    break;

                case MA_LOADPAGE:
                    /* The mode first: menu_open builds the rows, and in load
                     * mode which rows are pickable depends on it. */
                    menu.mode = SLOTS_LOAD;
                    menu_open(MENU_SLOTS, &cfg, slots);
                    break;

                case MA_SLOT:
                    if (menu.mode == SLOTS_LOAD) {
                        load_slot = act_slot;
                    } else if (act_slot >= 0 && act_slot < SAVE_SLOTS) {
                        /* An empty slot is simply taken. A full one is a save
                         * somebody made, so it gets a question first. */
                        if (slots[act_slot].used) {
                            menu.pending = act_slot;
                            menu_open(MENU_CONFIRM, &cfg, slots);
                        } else {
                            start_slot = act_slot;
                        }
                    }
                    break;

                case MA_YES:
                    start_slot = act_slot;
                    break;

                case MA_SAVE:
                    if (cur_slot < 0)
                        cur_slot = 0;
                    save_slot_path(slot_path, sizeof(slot_path), cur_slot);
                    hud_toast(game_save(g, slot_path) == 0
                              ? "saved" : "could not write the save file");
                    (void)save_scan(slots);
                    menu.page    = MENU_NONE;
                    menu.started = 1;
                    prev_px = g->p.x;
                    prev_py = g->p.y;
                    break;

                case MA_SETTINGS:
                    menu_open(MENU_SETTINGS, &cfg, slots);
                    break;
                case MA_CONTROLS:
                    menu_open(MENU_CONTROLS, &cfg, slots);
                    break;
                case MA_BACK:
                    if (menu.page == MENU_CONTROLS) {
                        menu_open(MENU_SETTINGS, &cfg, slots);
                    } else if (menu.page == MENU_CONFIRM) {
                        /* Answering "no" goes back to the list, not to the
                         * root: the player was choosing a slot and has only
                         * ruled one out. */
                        menu_open(MENU_SLOTS, &cfg, slots);
                    } else {
                        /* Written on the way out of settings: one moment, one
                         * place for the failure to be reported, instead of a
                         * file write on every arrow key. */
                        if (menu.page == MENU_SETTINGS && cfg_dirty) {
                            if (cfg_save(&cfg, CFG_FILENAME) != 0)
                                hud_toast("could not write the settings file");
                            else
                                cfg_dirty = 0;
                        }
                        menu_open(MENU_ROOT, &cfg, slots);
                    }
                    break;
                case MA_QUIT:
                    running = 0;
                    break;
                case MA_NONE:
                default:
                    break;
                }

                /* ---- the two things that actually change the world -------
                 *
                 * Both are here, once, rather than inside the switch: three
                 * different rows can begin a new game (new game with a slot
                 * free, an empty slot picked from the list, and a confirmed
                 * replacement) and two can load one (continue and a slot row).
                 * One copy of each is one place for them to be right. */
                if (start_slot >= 0 && start_slot < SAVE_SLOTS) {
                    /* The seed is the session's seed - the one --seed asked
                     * for, defaulting to 1 - not a fresh random one. A new game
                     * that reseeded would make --seed mean nothing the moment
                     * the menu was used, and every test and screenshot recipe
                     * in the project is anchored to it. Worlds are still
                     * changed with [ and ]. */
                    game_reseed(g, sc, &audio, seed);
#if WAYFARER_SELFTEST
                    if (lit_mode)
                        reveal_all(&g->w);
#endif
                    prev_px  = g->p.x;
                    prev_py  = g->p.y;
                    cur_slot = start_slot;
                    /* Written immediately, so that "used" means "there is a
                     * file" everywhere and nothing has to track a slot that is
                     * spoken for but empty. It is also what makes a confirmed
                     * replacement real at the moment it is confirmed, instead
                     * of leaving the old save on disk - and readable by
                     * continue - until the player happens to press F5. */
                    save_slot_path(slot_path, sizeof(slot_path), cur_slot);
                    if (game_save(g, slot_path) != 0)
                        hud_toast("could not write the save file");
                    (void)save_scan(slots);
                    menu.page    = MENU_NONE;
                    menu.started = 1;
                }
                else if (load_slot >= 0 && load_slot < SAVE_SLOTS) {
                    Uint64 ls = seed;
                    save_slot_path(slot_path, sizeof(slot_path), load_slot);
                    if (game_load(g, sc, slot_path, &ls) == 0) {
                        seed = ls;
#if WAYFARER_SELFTEST
                        if (lit_mode)
                            reveal_all(&g->w);
#endif
                        prev_px = g->p.x;
                        prev_py = g->p.y;
                        hud.mm_dirty  = 1;
                        hud.map_open  = 0;
                        hud.bm_dirty  = 1;
                        hud.win_shown = area_complete(g);
                        /* The music restarts with the world, resumed at the
                         * loaded counts and biome rather than back at silence
                         * or the wrong area's register. */
                        audio_request_reset(&audio, ls, g->frags_restored,
                                            g->souls_restored,
                                            g->area == 1 ? BIOME_FOREST
                                            : g->area == 2 ? BIOME_UNDERWORLD
                                            : BIOME_LUMIARA);
                        hud_toast("loaded");
                        cur_slot     = load_slot;
                        menu.page    = MENU_NONE;
                        menu.started = 1;
                    } else {
                        /* The row is only offered when save_scan said the
                         * header was sound, so arriving here means the file
                         * passed that and was still refused - it changed under
                         * us, or it stands her in a wall. Say so and re-scan,
                         * rather than leaving a row that cannot work. */
                        hud_toast("that save could not be loaded");
                        (void)save_scan(slots);
                        {
                            MenuCtx c = menu_ctx(&cfg, slots);
                            count = menu_build(&c, rows);
                            if (count > 0 && menu.sel < count &&
                                !rows[menu.sel].enabled)
                                menu.sel = menu_step(rows, count, menu.sel, 1);
                        }
                    }
                }
            }
            else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                /* Escape opens the menu instead of ending the process. It used
                 * to quit outright with no confirmation and no way back, which
                 * is the single thing this whole screen exists to fix. */
                case SDLK_ESCAPE:
                    menu.started = 1;
                    (void)save_scan(slots);
                    menu_open(MENU_ROOT, &cfg, slots);
                    break;
                case SDLK_F11:
                    /* Goes through the setting, so the key and the settings row
                     * can never disagree - and so it is remembered, which a
                     * toggle that only touched a local never was. */
                    cfg.fullscreen = !cfg.fullscreen;
                    cfg_apply_window(win, &cfg);
                    cfg_dirty = 1;
                    break;
                case SDLK_e:
                case SDLK_SPACE:
                    /* Mote, then map fragment, then portal - the same order
                     * prompt_draw resolves the first two in, so the keycap
                     * always names what the key is about to do. */
                    if (try_interact(g, &audio) < 0 && !try_take_map(g, &audio)) {
#if WAYFARER_SELFTEST
                        if (try_use_portal(g, sc, &audio) && lit_mode)
                            reveal_all(&g->w);
#else
                        (void)try_use_portal(g, sc, &audio);
#endif
                    }
                    break;
                /* M for the map, and only once this area's fragment has been
                 * found. The refusal is a toast rather than silence: a key that
                 * does nothing and says nothing is indistinguishable from a key
                 * that is broken. */
                case SDLK_m:
                    if (game_has_map(g)) {
                        hud.map_open = !hud.map_open;
                        hud.bm_dirty = 1;
                    } else {
                        hud_toast("you have no map of this place");
                    }
                    break;
                case SDLK_F5:
                    /* Writes the slot she is playing. cur_slot is only ever -1
                     * on a path that never passed through the menu - a --frames
                     * run - and slot 1 is then the honest default rather than a
                     * refusal: the key has always saved, and a run that reached
                     * here has a game worth saving. */
                    if (cur_slot < 0)
                        cur_slot = 0;
                    save_slot_path(slot_path, sizeof(slot_path), cur_slot);
                    hud_toast(game_save(g, slot_path) == 0
                              ? "saved" : "could not write the save file");
                    /* Re-scanned rather than assumed: a successful save is the
                     * one event that turns an empty slot into a full one, and
                     * asking the files is what keeps the list's answer and the
                     * disk's contents the same answer. */
                    (void)save_scan(slots);
                    break;
                /* Step to the next or previous world. Both spellings are here
                 * because both are the obvious one to somebody: ] and [ read as
                 * "forward" and "back" on a bracket pair, n and p as "next" and
                 * "previous". Neither collides with a movement or action key.
                 *
                 * Seed 1 is the floor rather than wrapping through 0: the seed
                 * is a Uint64, so decrementing past 1 would land on a world
                 * numbered 18446744073709551615, which the HUD prints through a
                 * double and cannot even render back correctly. */
                case SDLK_RIGHTBRACKET:
                case SDLK_n:
                case SDLK_LEFTBRACKET:
                case SDLK_p: {
                    char buf[32];
                    int fwd = (ev.key.keysym.sym == SDLK_RIGHTBRACKET ||
                               ev.key.keysym.sym == SDLK_n);
                    if (fwd)          seed++;
                    else if (seed > 1) seed--;
                    game_reseed(g, sc, &audio, seed);
#if WAYFARER_SELFTEST
                    if (lit_mode)
                        reveal_all(&g->w);
#endif
                    /* She is somewhere else entirely now, so the interpolator
                     * must not draw a frame on the way from where she was. */
                    prev_px = g->p.x;
                    prev_py = g->p.y;
                    SDL_snprintf(buf, sizeof(buf), "seed %.0f", (double)seed);
                    hud_toast(buf);
                    break;
                }
                case SDLK_F9: {
                    Uint64 ls = seed;
                    /* The pair to F5: it re-reads the slot she is playing, not
                     * whichever save is newest. A quick-load that jumped to
                     * some other slot because that one happened to be saved
                     * more recently would be a different game arriving under
                     * one keypress. */
                    if (cur_slot < 0)
                        cur_slot = 0;
                    save_slot_path(slot_path, sizeof(slot_path), cur_slot);
                    if (game_load(g, sc, slot_path, &ls) == 0) {
                        seed = ls;
#if WAYFARER_SELFTEST
                        if (lit_mode)
                            reveal_all(&g->w);
#endif
                        prev_px = g->p.x;
                        prev_py = g->p.y;
                        hud.mm_dirty = 1;
                        hud.map_open = 0;
                        hud.bm_dirty = 1;
                        hud.win_shown = area_complete(g);
                        /* The music restarts with the world, resumed at the
                         * loaded counts and biome rather than back at silence
                         * or the wrong area's register. */
                        audio_request_reset(&audio, ls, g->frags_restored, g->souls_restored,
                                            g->area == 1 ? BIOME_FOREST
                                            : g->area == 2 ? BIOME_UNDERWORLD : BIOME_LUMIARA);
                        hud_toast("loaded");
                        (void)save_scan(slots);
                    } else {
                        hud_toast("no save to load");
                    }
                    break;
                }
#if WAYFARER_SELFTEST
                case SDLK_TAB:
                    atlas_page = (atlas_page + 1) % 7 - 1;  /* -1 = the world */
                    break;
#endif
                default:
                    break;
                }
            }
        }

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - prev) / (double)perf;
        prev = now;
        /* Clamp: after a breakpoint or a window drag, a huge elapsed would
         * otherwise spin the catch-up loop for thousands of steps. */
        if (elapsed > 0.25)
            elapsed = 0.25;
        /* The menu PAUSES, where the map screen only holds her still. The map
         * is something she is reading while standing in the world, so fog,
         * restoration easing and toasts all keep running underneath it; the
         * menu is not in the world at all, and time passing behind it - a toast
         * expiring, a region finishing its ease - would be the game playing
         * itself while nobody is looking.
         *
         * The accumulator is HELD AT ZERO rather than left to fill and be
         * discarded on the way out. Two things fall out of that: closing the
         * menu cannot fire a burst of catch-up ticks and walk her out of the
         * pause, and the render interpolator's alpha stays 0 instead of
         * becoming the minutes-long ratio that would extrapolate the camera
         * somewhere absurd if the menu were opened mid-stride. */
        if (menu.page != MENU_NONE)
            acc = 0.0f;
        else
            acc += (float)elapsed;
        while (acc >= TICK_DT) {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            /* Captured before anything moves her, and captured on EVERY tick
             * including a catch-up one, so the pair always spans exactly the
             * tick the leftover accumulator is a fraction of. */
            prev_px = g->p.x;
            prev_py = g->p.y;
            float mx = (float)((keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) -
                               (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]));
            float my = (float)((keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) -
                               (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]));
            float len;
#if WAYFARER_SELFTEST
            /* --lean: hold a direction with no hands on the keyboard. The
             * refusal toasts only exist while she is PUSHING into something, so
             * a --frames screenshot run - which never presses a key - could
             * never photograph one. Injected here rather than faked further
             * down, so the shot goes through the real refusal_at, the real
             * rate limit and the real move_axis. */
            if (lean_x || lean_y) { mx = (float)lean_x; my = (float)lean_y; }
#endif
            /* The map covers the world, so walking underneath it - into a pond,
             * into a refusal toast she cannot see - is input she did not mean
             * to give. Her INTENT is held; the simulation is not paused, so
             * fog, restoration easing and toasts all keep running underneath.
             * Placed after the --lean injection so it holds against that too,
             * which is what lets a screenshot of the map screen be taken with
             * a direction held down. */
            if (hud.map_open) { mx = 0.0f; my = 0.0f; }
            len = SDL_sqrtf(mx * mx + my * my);
            int face;

            /* Normalised by the true vector length, so speed is the same in all
             * eight directions rather than sqrt(2) faster on the diagonals. */
            g->p.moving = (len > 0.0f);
            if (g->p.moving) {
                float sx = (mx / len) * PLAYER_SPEED * TICK_DT;
                float sy = (my / len) * PLAYER_SPEED * TICK_DT;
                /* Ask about the refusal BEFORE moving: afterwards she is standing
                 * next to it and the step that was refused is gone. Rate-limited
                 * to one report per second by the toast's own remaining time, so
                 * leaning on a ledge or a pond bank does not machine-gun the deny
                 * sound. refusal_at covers both the ability gates and the two
                 * Forest walls that have something to say - see wall_refusal. */
                if (hud.toast_left < HUD_TOAST_TICKS - 60) {
                    Uint8 miss = refusal_at(&g->w, g->p.abilities, g->p.x + sx, g->p.y);
                    if (!miss)
                        miss = refusal_at(&g->w, g->p.abilities, g->p.x, g->p.y + sy);
                    gate_report(miss, &audio);
                }
                /* Two independent axis calls, in this order, so a diagonal into
                 * a trunk slides along it instead of stopping dead. */
                move_axis(&g->w, &g->p, sx, 0.0f);
                move_axis(&g->w, &g->p, 0.0f, sy);
                face = facing4_from_intent(mx, my);
                if (face >= 0) g->p.facing = (Uint8)face;
                /* Wrapped rather than fmod'd: at most one cycle can elapse in a
                 * tick, and it is never reset - see the Player comment. */
                g->p.anim += TICK_DT;
                if (g->p.anim >= (float)WALK_FRAMES / WALK_FPS)
                    g->p.anim -= (float)WALK_FRAMES / WALK_FPS;
            }
            reveal_around(&g->w, g->p.x, g->p.y, TICK_DT);
            hud_tick(g);
            /* Region colour eases toward its target rather than snapping, so a
             * restore reads as the world coming back rather than as a palette
             * swap on one frame. */
            {
                int r;
                for (r = 0; r < g->w.region_count; r++) {
                    Region *rg = &g->w.regions[r];
                    if (rg->restoration < rg->restore_to) {
                        rg->restoration += RESTORE_RATE * TICK_DT;
                        if (rg->restoration > rg->restore_to)
                            rg->restoration = rg->restore_to;
                    }
                }
            }
            clock += TICK_DT;
            acc -= TICK_DT;
        }

        /* Re-fetch every frame: the surface is invalidated on resize. */
        fb = SDL_GetWindowSurface(win);
        if (!fb || fb->format->BytesPerPixel != 4)
            break;
        draw = back ? back : fb;

        /* SUB-TICK INTERPOLATION. The simulation is fixed-step and the renderer
         * is not, and the two are not phase-locked - nothing here waits on a
         * vsync, and SDL_Delay resolves to whole milliseconds against a 16.67 ms
         * frame. So the number of ticks that land in one frame is 1 most of the
         * time and 0 or 2 whenever the phases drift past each other, and drawing
         * at the last tick's position turns that into visible motion: at 72 px/s
         * the world scrolls 0 px, then 2.4 px, then 1.2 px again. It reads as
         * stutter even though the simulation is perfectly regular.
         *
         * Drawing BETWEEN the last two tick positions decouples the two rates:
         * `acc` is how far past the last tick this frame is, so alpha is the
         * fraction of the next tick already elapsed, and the drawn position
         * advances by the same amount every frame however the ticks fall.
         *
         * It is the camera this matters for, not the sprite: she is centred, so
         * what actually moves on screen is the world. Both are driven from the
         * same interpolated point so they cannot disagree by a pixel. */
        {
            float alpha = acc / TICK_DT;
            Player rp = g->p;
            rp.x = render_lerp(prev_px, g->p.x, alpha);
            rp.y = render_lerp(prev_py, g->p.y, alpha);

            camera_follow(rp.x, rp.y, draw->w, draw->h, &cam_x, &cam_y);
#if WAYFARER_SELFTEST
            /* Same clamp as following her, so a tile near an edge still frames
             * the way the game would ever show it. */
            if (cam_tx >= 0 || cam_ty >= 0)
                camera_follow(cam_tx >= 0 ? (float)(cam_tx * TILE + TILE / 2) : rp.x,
                              cam_ty >= 0 ? (float)(cam_ty * TILE + TILE / 2) : rp.y,
                              draw->w, draw->h, &cam_x, &cam_y);
            if (atlas_page >= 0) {
                draw_atlas(draw, atlas_page, clock);
            } else
#endif
            /* Opaque and full-surface, so like the map screen it REPLACES the
             * world rather than covering it - rendering a world underneath an
             * opaque menu would be a full frame of work nobody sees. */
            if (menu.page != MENU_NONE) {
                MenuCtx c = menu_ctx(&cfg, slots);
                menu_draw(draw, &c);
            } else {
                render_world(draw, &g->w, seed, cam_x, cam_y);
                props_build(draw->w, draw->h, &g->w, seed, cam_x, cam_y, g->ents,
                            &rp, clock, dl);
                props_draw(draw, dl);
#if WAYFARER_SELFTEST
                /* --solidmap: a dot on every blocking tile, so COLLISION can be
                 * compared against what is actually DRAWN.
                 *
                 * Kept because it is what found the invisible walls. The tests
                 * had proved every gate boundary was lined with solid tiles and
                 * the boundaries were still invisible, because a solid tile and
                 * a visible one are different claims; the dots made the gap
                 * obvious in one frame - markers sitting on open grass a full
                 * tile outside the rock they belonged to. Same reason --lit and
                 * --dev exist: some states only a screenshot can settle. */
                if (arg_flag(argc, argv, "--solidmap")) {
                    int mtx, mty;
                    Uint32 red = SDL_MapRGB(draw->format, 0xff, 0x20, 0x20);
                    for (mty = cam_y / TILE; mty <= (cam_y + draw->h) / TILE; mty++)
                        for (mtx = cam_x / TILE; mtx <= (cam_x + draw->w) / TILE; mtx++) {
                            if (mtx < 0 || mty < 0 || mtx >= WORLD_W || mty >= WORLD_H) continue;
                            if (!g->w.solid[mty][mtx]) continue;
                            fill_rect(draw, mtx * TILE - cam_x + 6, mty * TILE - cam_y + 6,
                                      4, 4, red);
                        }
                }
#endif
                /* After the sorted pass, so the keycap is never hidden by the
                 * mote it points at or by a trunk between them - it is UI, and
                 * the one thing on screen that must not be occluded. Before the
                 * HUD, which owns the frame's edges and outranks it. */
                /* Drawn against `draw`, whose dimensions are read from the
                 * surface rather than LOGICAL_*: with no backbuffer we render at
                 * native resolution, and a HUD placed by LOGICAL_* would land
                 * off screen.
                 *
                 * The map screen is opaque and takes the whole surface, so it
                 * REPLACES the prompt and the HUD rather than covering them -
                 * drawing either underneath would be work nobody sees. The
                 * has-map test is belt and braces: map_open can only be set
                 * through a key that already checked it, and every path that
                 * changes area clears it, but a map screen for an area whose
                 * map was never found is the one state this must not reach. */
                if (hud.map_open && game_has_map(g)) {
                    bigmap_draw(draw, g, sc);
                } else {
                    prompt_draw(draw, &g->w, g->ents, g->p.x, g->p.y,
                                cam_x, cam_y, clock);
                    hud_draw(draw, g);
                }
            }
        }
        present(win, fb, draw == back ? back : NULL);

#if WAYFARER_SELFTEST
        /* Grabbing the window from outside is unreliable; saving the surface we
         * just drew is exact. Needs --frames to know which frame is last. */
        if (shot && limit && frame + 1 >= limit)
            SDL_SaveBMP(draw, shot);
#endif

        frame++;
        if (limit && frame >= limit)
            running = 0;

        /* Cap the render rate. There is no vsync to lean on: SDL's render
         * subsystem (and with it PRESENTVSYNC) is compiled out, and
         * SDL_UpdateWindowSurface does not block on the display. Without this
         * the loop free-runs at thousands of fps and pegs a core to draw frames
         * nobody sees. Simulation is already fixed-step, so this affects only
         * how often we redraw.
         *
         * Paced against an ABSOLUTE deadline that advances by exactly one period
         * each frame, not against how long this frame took. The difference is
         * whether the error accumulates. Measuring the frame and sleeping the
         * remainder throws away the sub-millisecond part of every wait - the nap
         * is whole milliseconds against a 16.67 ms period - so the loop runs
         * persistently slow and the leftover drifts; carrying the deadline
         * forward instead means a nap that undershoots is paid back by the next
         * one, and the average period is exact. What is left is bounded jitter,
         * which is what the interpolation above absorbs.
         *
         * Resynchronised if we fall more than a period behind, so a stall (a
         * window drag, a breakpoint) does not leave the deadline in the past and
         * spin a burst of catch-up frames to no purpose. */
        {
            Uint64 period = (Uint64)((double)perf / FRAME_HZ);
            Uint64 t = SDL_GetPerformanceCounter();
            if (frame_due < t - period) frame_due = t;
            frame_due += period;
            if (t < frame_due) {
                Uint32 nap = (Uint32)(((frame_due - t) * 1000ULL) / perf);
                if (nap > 0)
                    SDL_Delay(nap);
            }
        }
    }

    /* Anything still unwritten - F11 during play, or a quit taken without
     * backing out of the settings page - goes to disk here. Silent on failure:
     * the window is on its way down and there is nowhere left to say it. */
    if (cfg_dirty)
        (void)cfg_save(&cfg, CFG_FILENAME);

    /* Close the device FIRST: it stops and joins the callback thread, so
     * nothing is still reading `audio` while the rest of this tears down. */
    if (dev) {
        SDL_PauseAudioDevice(dev, 1);
        SDL_CloseAudioDevice(dev);
    }
    SDL_free(g);
    SDL_free(dl);
    SDL_free(sc);
    if (hud.mm) SDL_FreeSurface(hud.mm);
    if (hud.bm) SDL_FreeSurface(hud.bm);
    if (fogpal) SDL_free(fogpal);
    if (back) SDL_FreeSurface(back);
    if (back_px) SDL_free(back_px);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
