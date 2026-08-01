/* Wayfarer — 2P Game Arcade 1.44MB Floppy Disk contest entry.
 *
 * PIPELINE PROOF ONLY. No game systems live here yet; the design is not
 * finalised (see design/Pending Team Discussion.md). This file proves the
 * toolchain end to end: SDL2 window, software framebuffer, procedural audio,
 * clean shutdown, static-linked stripped build, measurable byte size.
 *
 * Rendering is a hand-rolled software framebuffer written straight into the
 * window surface. SDL's render subsystem is compiled out entirely (see
 * build-sdl2.ps1) — it cost more than the whole rest of SDL, and the brief
 * calls for hand-rolled rendering regardless.
 *
 * Everything under WAYFARER_SELFTEST is verification scaffolding and is
 * compiled out of the shipping build. Release builds must never define it.
 */

#include <SDL.h>

#if WAYFARER_SELFTEST
#include <stdio.h>
#endif

#define WIN_W 640
#define WIN_H 360

/* Week 1 placeholder world. Larger than the view so exploration means moving
 * the camera, which is what makes the reveal read as discovery. */
#define TILE     16
#define WORLD_W  80
#define WORLD_H  45

#define PLAYER_SIZE  12
#define PLAYER_SPEED 110.0f /* world px/sec */

#define REVEAL_TILES 5     /* reveal radius, in tiles */
#define REVEAL_RATE  2.5f  /* reveal units/sec; ~0.4s to fully clear a tile */

#define FOG_TINT_R 44.0f
#define FOG_TINT_G 52.0f
#define FOG_TINT_B 68.0f

/* Fixed simulation step. Decoupling simulation from render rate keeps movement
 * frame-rate independent and, more importantly for QA, makes a given input
 * sequence produce an identical trajectory every run. */
#define TICK_HZ 60.0f
#define TICK_DT (1.0f / TICK_HZ)

/* Render cap, separate from the simulation rate on purpose. */
#define FRAME_HZ 60.0

#define AUDIO_RATE     48000
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES  1024 /* frames per callback; ~21 ms at 48 kHz */
#define TONE_HZ        440.0
#define TONE_AMP       0.20f

#define TWO_PI 6.283185307179586

/* ------------------------------------------------------------------ RNG -- */
/* PCG32. Every procedural generator draws from a seeded stream so any bad
 * output is reproducible from its seed alone (--seed N). Built before the
 * generators, deliberately: retrofitting determinism never works.
 *
 * Why PCG32 rather than the xorshift32 this started as: World Generation
 * requires terrain, entity and audio generation to be INDEPENDENT, so that
 * tuning one does not reshuffle another. PCG32's `inc` is a sequence selector
 * — each distinct odd value defines a different period-2^64 sequence. Seeding
 * one generator per stream from a shared master seed therefore gives streams
 * that are independent by construction, not merely started at different
 * offsets in one shared sequence (which is all a single xorshift could offer,
 * and which can silently overlap). */

typedef struct {
    Uint64 state;
    Uint64 inc; /* stream selector; always odd */
} Rng;

/* Stream ids. Adding a stream must not perturb the existing ones, so never
 * renumber these — append only. */
enum {
    STREAM_TERRAIN  = 1,
    STREAM_ENTITIES = 2,
    STREAM_AUDIO    = 3
};

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
    r->inc = (stream << 1) | 1u; /* forced odd: even inc degrades the period */
    (void)rng_next(r);
    r->state += seed;
    (void)rng_next(r);
}

/* Uniform in [0, 1). 24 bits is already finer than float can represent. */
static float rng_float(Rng *r)
{
    return (float)(rng_next(r) >> 8) * (1.0f / 16777216.0f);
}

/* Uniform in [-1, 1). */
static float rng_bipolar(Rng *r)
{
    return rng_float(r) * 2.0f - 1.0f;
}

/* Uniform in [0, n). Rejection-sampled: the naive % introduces modulo bias
 * that would skew fragment placement toward low indices.
 *
 * Scoped to the self-test until a real caller exists — fragment placement
 * (Week 3) will be the first. Move it out of the guard then; leaving it in the
 * shipping build now would be dead code and an unused-function warning. */
#if WAYFARER_SELFTEST
static Uint32 rng_below(Rng *r, Uint32 n)
{
    Uint32 threshold, v;
    if (n < 2)
        return 0;
    threshold = (0u - n) % n;
    for (;;) {
        v = rng_next(r);
        if (v >= threshold)
            return v % n;
    }
}
#endif

/* The project's three independent generation streams, all derived from one
 * master seed so a single --seed N reproduces an entire world. */
typedef struct {
    Uint64 seed;
    Rng    terrain;
    Rng    entities;
    Rng    audio;
} Rngs;

static void rngs_init(Rngs *g, Uint64 seed)
{
    g->seed = seed;
    rng_seed(&g->terrain, seed, STREAM_TERRAIN);
    rng_seed(&g->entities, seed, STREAM_ENTITIES);
    rng_seed(&g->audio, seed, STREAM_AUDIO);
}

/* ---------------------------------------------------------------- audio -- */

typedef struct {
    int    req_rate; /* rate we ask for; always AUDIO_RATE except under --rate */
    int    rate;     /* rate the device actually gave us */
    int    channels;
    double phase; /* cycles, in [0,1) — double so long runs don't drift */
    int    noise; /* 0 = sine tone, 1 = seeded white noise */
    Rng    rng;
#if WAYFARER_SELFTEST
    Uint64 calls;
    Uint64 frames;
    Uint64 max_ticks;   /* worst-case callback duration */
    int    partial_len; /* callbacks whose len wasn't a whole frame count */
    float *cap;         /* capture buffer for offline analysis */
    int    cap_cap;
    int    cap_len;
#endif
} Audio;

/* Runs on SDL's real-time audio thread against a hard deadline. No allocation,
 * no locks, no syscalls, no unbounded work. */
static void SDLCALL audio_cb(void *userdata, Uint8 *stream, int len)
{
    Audio *a = (Audio *)userdata;
    float *out = (float *)(void *)stream;
    int nfloats = len / (int)sizeof(float);
    int frames = nfloats / a->channels;
    double inc = TONE_HZ / (double)a->rate;
    int i, c;
#if WAYFARER_SELFTEST
    Uint64 t0 = SDL_GetPerformanceCounter();
    if (nfloats % a->channels != 0)
        a->partial_len++;
#endif

    for (i = 0; i < frames; i++) {
        float v;
        if (a->noise) {
            v = rng_bipolar(&a->rng) * TONE_AMP;
        } else {
            v = SDL_sinf((float)(a->phase * TWO_PI)) * TONE_AMP;
            a->phase += inc;
            if (a->phase >= 1.0)
                a->phase -= 1.0;
        }
        for (c = 0; c < a->channels; c++)
            out[i * a->channels + c] = v;
    }

    /* SDL always asks for whole frames, but leaving even one byte unwritten
     * would play back as garbage. Cheap insurance against a real audio bug. */
    for (i = frames * a->channels; i < nfloats; i++)
        out[i] = 0.0f;

#if WAYFARER_SELFTEST
    if (a->cap && a->cap_len < a->cap_cap) {
        int n = nfloats;
        if (n > a->cap_cap - a->cap_len)
            n = a->cap_cap - a->cap_len;
        SDL_memcpy(a->cap + a->cap_len, out, (size_t)n * sizeof(float));
        a->cap_len += n;
    }
    a->calls++;
    a->frames += (Uint64)frames;
    {
        Uint64 dt = SDL_GetPerformanceCounter() - t0;
        if (dt > a->max_ticks)
            a->max_ticks = dt;
    }
#endif
}

static SDL_AudioDeviceID audio_open(Audio *a, SDL_AudioSpec *have)
{
    SDL_AudioSpec want;
    SDL_AudioDeviceID dev;

    SDL_zero(want);
    want.freq = a->req_rate ? a->req_rate : AUDIO_RATE;
    want.format = AUDIO_F32SYS;
    want.channels = AUDIO_CHANNELS;
    want.samples = AUDIO_SAMPLES;
    want.callback = audio_cb;
    want.userdata = a;

    /* Allow only a frequency change. Accepting the device's native rate costs
     * us one multiply; letting SDL resample would drag in its converter and
     * add latency. Format stays F32 — WASAPI's native format on Windows, so
     * in practice no conversion happens at all. */
    dev = SDL_OpenAudioDevice(NULL, 0, &want, have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!dev)
        return 0;

    a->rate = have->freq;
    a->channels = have->channels;
    return dev;
}

/* ----------------------------------------------------------------- args -- */

static const char *arg_val(int argc, char **argv, const char *key)
{
    int i;
    for (i = 1; i + 1 < argc; i++)
        if (SDL_strcmp(argv[i], key) == 0)
            return argv[i + 1];
    return NULL;
}

static int arg_int(int argc, char **argv, const char *key, int fallback)
{
    const char *v = arg_val(argc, argv, key);
    return v ? SDL_atoi(v) : fallback;
}

static int arg_flag(int argc, char **argv, const char *key)
{
    int i;
    for (i = 1; i < argc; i++)
        if (SDL_strcmp(argv[i], key) == 0)
            return 1;
    return 0;
}

/* ----------------------------------------------------------------- world -- */
/* WEEK 1 PLACEHOLDER. This is a tile grid so movement and the fog-to-color
 * reveal have something to act on. The real world is a region graph
 * (design/systems/World Generation.md) and lands in Week 2 — at which point
 * per-tile `reveal` becomes per-region restoration% that tiles inherit. */

typedef struct {
    Uint8 solid[WORLD_H][WORLD_W];
    float reveal[WORLD_H][WORLD_W]; /* 0 = fogged and colourless, 1 = restored */
} World;

typedef struct {
    float x, y; /* centre, in world pixels */
} Player;

typedef struct {
    World  w;
    Player p;
    int    cam_x, cam_y;
} Game;

typedef struct {
    int up, down, left, right;
} Input;

static int solid_at(const World *w, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
        return 1; /* outside the world is wall, so nothing can escape */
    return w->solid[ty][tx];
}

/* Cellular-automaton cave. Cheap, seeded, and produces rounded blobs that give
 * the reveal something worth walking around. Placeholder generation only. */
static void world_gen(World *w, Rng *rng)
{
    /* Deliberately a local, not a static. On PE/COFF, -fdata-sections emits
     * zero-initialised statics as .data$name COMDATs, which are stored in the
     * file — 40 KB of literal zeros measured in the executable before this was
     * moved to the stack. Keep world-sized scratch buffers off the static path. */
    Uint8 next[WORLD_H][WORLD_W];
    int x, y, pass;

    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->solid[y][x] = (x == 0 || y == 0 || x == WORLD_W - 1 || y == WORLD_H - 1)
                                 ? 1
                                 : (rng_float(rng) < 0.42f);

    for (pass = 0; pass < 4; pass++) {
        for (y = 0; y < WORLD_H; y++) {
            for (x = 0; x < WORLD_W; x++) {
                int n = 0, dx, dy;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++)
                        if (dx || dy)
                            n += solid_at(w, x + dx, y + dy);
                next[y][x] = (Uint8)(n >= 5 ? 1 : (n <= 2 ? 0 : w->solid[y][x]));
                if (x == 0 || y == 0 || x == WORLD_W - 1 || y == WORLD_H - 1)
                    next[y][x] = 1;
            }
        }
        SDL_memcpy(w->solid, next, sizeof(next));
    }

    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            w->reveal[y][x] = 0.0f;
}

/* Does the player's AABB, centred here, overlap any solid tile? */
static int player_blocked(const World *w, float cx, float cy)
{
    float h = PLAYER_SIZE * 0.5f;
    int x0 = (int)SDL_floorf((cx - h) / TILE);
    int x1 = (int)SDL_floorf((cx + h - 0.001f) / TILE);
    int y0 = (int)SDL_floorf((cy - h) / TILE);
    int y1 = (int)SDL_floorf((cy + h - 0.001f) / TILE);
    int tx, ty;

    for (ty = y0; ty <= y1; ty++)
        for (tx = x0; tx <= x1; tx++)
            if (solid_at(w, tx, ty))
                return 1;
    return 0;
}

/* Advance along one axis in sub-pixel steps, stopping at the first blocked
 * step. Stepping rather than a single test-and-revert means a fast player can
 * never tunnel through a wall, and sliding along a wall still works because
 * the two axes are resolved independently. */
static void move_axis(Game *g, float dx, float dy)
{
    float remaining = (dx != 0.0f) ? dx : dy;

    while (SDL_fabsf(remaining) > 0.0001f) {
        float step = remaining;
        float nx, ny;
        if (step > 0.5f) step = 0.5f;
        if (step < -0.5f) step = -0.5f;
        remaining -= step;

        nx = g->p.x + (dx != 0.0f ? step : 0.0f);
        ny = g->p.y + (dy != 0.0f ? step : 0.0f);
        if (player_blocked(&g->w, nx, ny))
            return;
        g->p.x = nx;
        g->p.y = ny;
    }
}

/* Raise reveal toward 1 within a radius of the player. WEEK 1 PLACEHOLDER for
 * the trigger only — the shipped trigger is restoring a fragment, which sets a
 * whole region's restoration% (see design/systems/Fog and Reveal.md). The blend
 * maths below is the real thing; only what raises it is temporary. */
static void reveal_around(Game *g, float dt)
{
    int cx = (int)(g->p.x / TILE);
    int cy = (int)(g->p.y / TILE);
    int r = REVEAL_TILES;
    int tx, ty;

    for (ty = cy - r; ty <= cy + r; ty++) {
        for (tx = cx - r; tx <= cx + r; tx++) {
            int ddx, ddy, d2;
            float target, *cell;
            if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
                continue;
            ddx = tx - cx;
            ddy = ty - cy;
            d2 = ddx * ddx + ddy * ddy;
            if (d2 > r * r)
                continue;
            /* Full strength at the centre, tapering to nothing at the edge, so
             * the boundary is a soft gradient rather than a visible disc. */
            target = 1.0f - (float)d2 / (float)(r * r);
            cell = &g->w.reveal[ty][tx];
            if (*cell < target) {
                *cell += REVEAL_RATE * dt;
                if (*cell > target)
                    *cell = target;
            }
        }
    }
}

/* Single source of truth for the key mapping, so a test that drives this
 * exercises exactly what the game does rather than a copy that can drift. */
static void input_poll(Input *in)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);

    in->up = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    in->down = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    in->left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    in->right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
}

static void sim_step(Game *g, const Input *in, float dt)
{
    float mx = (float)(in->right - in->left);
    float my = (float)(in->down - in->up);

    /* Normalise diagonals, or moving corner-wise is 1.41x faster than straight. */
    if (mx != 0.0f && my != 0.0f) {
        mx *= 0.70710678f;
        my *= 0.70710678f;
    }

    move_axis(g, mx * PLAYER_SPEED * dt, 0.0f);
    move_axis(g, 0.0f, my * PLAYER_SPEED * dt);
    reveal_around(g, dt);
}

/* Flood-fill the open region containing (sx,sy). Returns its tile count and,
 * via *first, its top-left-most tile. Explicit stack, not recursion: a cave can
 * be 3500 tiles deep and blowing the stack in a generator is not a bug anyone
 * enjoys finding later.
 *
 * This is also the seed of Week 2's reachability invariant — the same fill
 * answers "can the player actually get to that fragment?". */
static int flood_open(const World *w, Uint8 *seen, int *stack, int sx, int sy,
                      int *first, int *sum_x, int *sum_y)
{
    int top = 0, count = 0;
    int best = WORLD_W * WORLD_H;

    *sum_x = 0;
    *sum_y = 0;
    if (solid_at(w, sx, sy) || seen[sy * WORLD_W + sx])
        return 0;

    stack[top++] = sy * WORLD_W + sx;
    seen[sy * WORLD_W + sx] = 1;

    while (top > 0) {
        int idx = stack[--top];
        int x = idx % WORLD_W, y = idx / WORLD_W;
        int d;
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };

        count++;
        *sum_x += x;
        *sum_y += y;
        if (idx < best)
            best = idx;

        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H)
                continue;
            if (w->solid[ny][nx] || seen[ny * WORLD_W + nx])
                continue;
            seen[ny * WORLD_W + nx] = 1;
            stack[top++] = ny * WORLD_W + nx;
        }
    }

    *first = best;
    return count;
}

/* Spawn in the LARGEST open region, not merely the nearest open tile.
 * Measured: nearest-tile spawning dropped the player into a sealed one-tile
 * pocket on 3 of 20 seeds, where movement and therefore the whole reveal
 * effect were untestable. */
static int game_init(Game *g, Rngs *rngs)
{
    /* Locals, not statics — see the note in world_gen. ~18 KB of frame, which
     * is nothing against the default 2 MB stack, and zero bytes in the file. */
    Uint8 seen[WORLD_W * WORLD_H];
    int stack[WORLD_W * WORLD_H];
    int x, y, biggest = 0, biggest_first = -1;

    world_gen(&g->w, &rngs->terrain);
    SDL_memset(seen, 0, sizeof(seen));

    /* Pass 1: find the largest open region. */
    for (y = 1; y < WORLD_H - 1; y++) {
        for (x = 1; x < WORLD_W - 1; x++) {
            int first = -1, sx = 0, sy = 0;
            int n = flood_open(&g->w, seen, stack, x, y, &first, &sx, &sy);
            if (n > biggest) {
                biggest = n;
                biggest_first = first;
            }
        }
    }

    if (biggest_first < 0) { /* pathological seed: carve rather than trap */
        g->w.solid[WORLD_H / 2][WORLD_W / 2] = 0;
        g->p.x = (float)(WORLD_W / 2) * TILE + TILE * 0.5f;
        g->p.y = (float)(WORLD_H / 2) * TILE + TILE * 0.5f;
        g->cam_x = 0;
        g->cam_y = 0;
        return 1;
    }

    /* Pass 2: re-flood only that region so `seen` marks exactly its tiles,
     * then spawn on the one nearest its centroid. Spawning on the region's
     * lowest tile index instead put the player hard against the world corner,
     * where the camera clamps and most of the view is wasted — which made the
     * reveal effect much harder to judge. */
    {
        int sum_x = 0, sum_y = 0, first = -1;
        int cx, cy, best_d = WORLD_W * WORLD_W + WORLD_H * WORLD_H, best_idx = biggest_first;

        SDL_memset(seen, 0, sizeof(seen));
        (void)flood_open(&g->w, seen, stack, biggest_first % WORLD_W,
                         biggest_first / WORLD_W, &first, &sum_x, &sum_y);
        cx = sum_x / biggest;
        cy = sum_y / biggest;

        for (y = 0; y < WORLD_H; y++) {
            for (x = 0; x < WORLD_W; x++) {
                int d;
                if (!seen[y * WORLD_W + x])
                    continue;
                d = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                if (d < best_d) {
                    best_d = d;
                    best_idx = y * WORLD_W + x;
                }
            }
        }
        g->p.x = (float)(best_idx % WORLD_W) * TILE + TILE * 0.5f;
        g->p.y = (float)(best_idx / WORLD_W) * TILE + TILE * 0.5f;
    }

    g->cam_x = 0;
    g->cam_y = 0;
    return biggest; /* open tiles reachable from spawn */
}

/* ------------------------------------------------------------- graphics -- */

/* Window surfaces are plain memory — never RLE-encoded — so SDL_MUSTLOCK is
 * false for them and no lock/unlock is needed. Caller guarantees 32bpp.
 * Clips, because tiles at the screen edge are partly off it. */
static void fill_rect(SDL_Surface *s, int x, int y, int w, int h, Uint32 colour)
{
    int iy, ix;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0)
        return;

    for (iy = 0; iy < h; iy++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (y + iy) * s->pitch);
        for (ix = 0; ix < w; ix++)
            row[x + ix] = colour;
    }
}

/* The core visual hook, and the one piece of this slice that is not a
 * placeholder: colour = lerp(drained grey, true colour, restoration%).
 * See design/systems/Fog and Reveal.md. */
static Uint32 fog_lerp(SDL_Surface *s, int r, int gr, int b, float reveal)
{
    /* Luminance, then pulled toward the fog tint and darkened: unrestored land
     * keeps its shape but loses its colour, which is the "drained" read. */
    float lum = (0.299f * r + 0.587f * gr + 0.114f * b) * 0.55f;
    float fr = lum + (FOG_TINT_R - lum) * 0.45f;
    float fg = lum + (FOG_TINT_G - lum) * 0.45f;
    float fb = lum + (FOG_TINT_B - lum) * 0.45f;

    if (reveal < 0.0f) reveal = 0.0f;
    if (reveal > 1.0f) reveal = 1.0f;

    return SDL_MapRGB(s->format,
                      (Uint8)(fr + ((float)r - fr) * reveal),
                      (Uint8)(fg + ((float)gr - fg) * reveal),
                      (Uint8)(fb + ((float)b - fb) * reveal));
}

static void render(SDL_Surface *fb, Game *g)
{
    int tx, ty;
    int tx0 = g->cam_x / TILE;
    int ty0 = g->cam_y / TILE;
    int tx1 = (g->cam_x + fb->w) / TILE + 1;
    int ty1 = (g->cam_y + fb->h) / TILE + 1;

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            Uint32 c;
            int solid;
            if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
                continue;
            solid = g->w.solid[ty][tx];
            c = solid ? fog_lerp(fb, 0x5a, 0x4a, 0x3c, g->w.reveal[ty][tx])
                      : fog_lerp(fb, 0x4e, 0x9e, 0x54, g->w.reveal[ty][tx]);
            fill_rect(fb, tx * TILE - g->cam_x, ty * TILE - g->cam_y, TILE, TILE, c);
        }
    }

    fill_rect(fb, (int)g->p.x - PLAYER_SIZE / 2 - g->cam_x,
              (int)g->p.y - PLAYER_SIZE / 2 - g->cam_y, PLAYER_SIZE, PLAYER_SIZE,
              SDL_MapRGB(fb->format, 0xe0, 0x64, 0x28));
}

static void camera_follow(Game *g, int view_w, int view_h)
{
    int max_x = WORLD_W * TILE - view_w;
    int max_y = WORLD_H * TILE - view_h;

    g->cam_x = (int)g->p.x - view_w / 2;
    g->cam_y = (int)g->p.y - view_h / 2;
    if (g->cam_x < 0) g->cam_x = 0;
    if (g->cam_y < 0) g->cam_y = 0;
    if (max_x > 0 && g->cam_x > max_x) g->cam_x = max_x;
    if (max_y > 0 && g->cam_y > max_y) g->cam_y = max_y;
}

/* ------------------------------------------------------------- selftest -- */
#if WAYFARER_SELFTEST

/* Drives the simulation headlessly with scripted input. The invariant that
 * matters: the player must never occupy a solid tile, on any seed, from any
 * direction, at any point during the run — not merely at the end. */
static int move_selftest(Uint64 seed, int verbose)
{
    Game g;
    Rngs rngs;
    Input in;
    int i, dir, fails = 0, overlaps = 0, steps = 0, open_tiles;
    float startx, starty, moved;

    rngs_init(&rngs, seed);
    open_tiles = game_init(&g, &rngs);
    startx = g.p.x;
    starty = g.p.y;

    /* A spawn region too small to explore makes the reveal untestable. */
    if (open_tiles < 200) {
        printf("  seed %.0f: spawn region only %d tiles - too small to explore\n",
               (double)seed, open_tiles);
        fails++;
    }

    if (player_blocked(&g.w, g.p.x, g.p.y)) {
        printf("  seed %.0f: SPAWNED INSIDE A WALL\n", (double)seed);
        fails++;
    }

    /* Zero input must mean zero movement. Trivial-looking, but it is the
     * assertion that separates "a stray keypress moved the player" from "the
     * simulation drifts on its own", and those have very different fixes. */
    {
        float bx = g.p.x, by = g.p.y;
        SDL_zero(in);
        for (i = 0; i < 300; i++)
            sim_step(&g, &in, TICK_DT);
        if (g.p.x != bx || g.p.y != by) {
            printf("  seed %.0f: DRIFTED with no input: %.4f,%.4f -> %.4f,%.4f\n",
                   (double)seed, (double)bx, (double)by, (double)g.p.x, (double)g.p.y);
            fails++;
        }
    }

    /* Push hard in all 8 directions, long enough to reach a wall each time. */
    for (dir = 0; dir < 8; dir++) {
        SDL_zero(in);
        if (dir == 0 || dir == 4 || dir == 5) in.left = 1;
        if (dir == 1 || dir == 6 || dir == 7) in.right = 1;
        if (dir == 2 || dir == 4 || dir == 6) in.up = 1;
        if (dir == 3 || dir == 5 || dir == 7) in.down = 1;
        for (i = 0; i < 240; i++) { /* 4 seconds per direction */
            sim_step(&g, &in, TICK_DT);
            steps++;
            if (player_blocked(&g.w, g.p.x, g.p.y))
                overlaps++;
        }
    }

    if (overlaps) {
        printf("  seed %.0f: player inside solid on %d of %d steps\n",
               (double)seed, overlaps, steps);
        fails++;
    }

    /* Must stay inside the world's walled border. */
    if (g.p.x < TILE || g.p.y < TILE ||
        g.p.x > (WORLD_W - 1) * TILE || g.p.y > (WORLD_H - 1) * TILE) {
        printf("  seed %.0f: ESCAPED THE WORLD at %.1f,%.1f\n",
               (double)seed, (double)g.p.x, (double)g.p.y);
        fails++;
    }

    /* A player that cannot move at all means a sealed spawn pocket — legal for
     * this placeholder generator, but worth surfacing rather than hiding. */
    moved = SDL_fabsf(g.p.x - startx) + SDL_fabsf(g.p.y - starty);

    if (verbose)
        printf("  seed %-10.0f open %5d tiles (%2d%%)  travelled %7.1f px  overlaps %d  %s\n",
               (double)seed, open_tiles,
               100 * open_tiles / (WORLD_W * WORLD_H), (double)moved, overlaps,
               fails ? "FAIL" : "PASS");

    return fails;
}

/* Speed must not depend on direction. Run in a deliberately empty world so
 * walls cannot mask the result: straight and diagonal movement over the same
 * number of ticks must cover the same distance. Without the 0.707 factor,
 * diagonal travel comes out 1.41x too fast — a bug that is easy to ship and
 * annoying to notice. */
static int speed_selftest(void)
{
    Game g;
    Input in;
    int i, fails = 0;
    float straight, diagonal, expect = PLAYER_SPEED; /* 60 ticks = 1 second */

    SDL_zero(g);
    g.p.x = (float)(WORLD_W / 2) * TILE;
    g.p.y = (float)(WORLD_H / 2) * TILE;

    SDL_zero(in);
    in.right = 1;
    for (i = 0; i < 60; i++)
        sim_step(&g, &in, TICK_DT);
    straight = g.p.x - (float)(WORLD_W / 2) * TILE;

    g.p.x = (float)(WORLD_W / 2) * TILE;
    g.p.y = (float)(WORLD_H / 2) * TILE;
    SDL_zero(in);
    in.right = 1;
    in.down = 1;
    for (i = 0; i < 60; i++)
        sim_step(&g, &in, TICK_DT);
    {
        float dx = g.p.x - (float)(WORLD_W / 2) * TILE;
        float dy = g.p.y - (float)(WORLD_H / 2) * TILE;
        diagonal = SDL_sqrtf(dx * dx + dy * dy);
    }

    printf("straight travel, 60 ticks : %.2f px (expected %.2f)\n",
           (double)straight, (double)expect);
    printf("diagonal travel, 60 ticks : %.2f px (must match straight)\n",
           (double)diagonal);

    if (SDL_fabsf(straight - expect) > 1.0f) fails++;
    if (SDL_fabsf(diagonal - straight) > 1.0f) fails++;
    printf("direction-independent speed: %s\n", fails ? "NO" : "yes");
    return fails;
}

/* Opens a real window and runs the real input path for `ms`, then reports where
 * the player actually went and which keys were actually seen. Screenshots turned
 * out to be a poor way to judge direction: with a follow camera the player stays
 * centred and only the scenery moves, so "which way did it go" is guesswork.
 * This answers it in numbers. */
static int input_selftest(Uint64 seed, int ms)
{
    Game g;
    Rngs rngs;
    SDL_Window *win;
    Input in, seen;
    float x0, y0;
    Uint32 end;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("FAIL  SDL_Init(VIDEO): %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("Wayfarer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) {
        SDL_Quit();
        return 1;
    }

    rngs_init(&rngs, seed);
    (void)game_init(&g, &rngs);
    x0 = g.p.x;
    y0 = g.p.y;
    SDL_zero(seen);

    printf("window open, driving real input path for %d ms\n", ms);
    printf("start position : %.2f, %.2f\n", (double)x0, (double)y0);

    end = SDL_GetTicks() + (Uint32)ms;
    while (SDL_GetTicks() < end) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                break;
        }
        input_poll(&in);
        seen.up |= in.up;
        seen.down |= in.down;
        seen.left |= in.left;
        seen.right |= in.right;
        sim_step(&g, &in, TICK_DT);
        SDL_Delay(16);
    }

    printf("end position   : %.2f, %.2f\n", (double)g.p.x, (double)g.p.y);
    printf("delta          : dx %+.2f  dy %+.2f\n",
           (double)(g.p.x - x0), (double)(g.p.y - y0));
    printf("keys seen down : up=%d down=%d left=%d right=%d\n",
           seen.up, seen.down, seen.left, seen.right);

    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* Same input sequence must produce a bit-identical trajectory. This is what
 * makes a movement bug reproducible from a seed instead of a ghost story. */
static int move_determinism_test(Uint64 seed)
{
    Game a, b;
    Rngs ra, rb;
    Input in;
    int i, mismatch = 0;

    rngs_init(&ra, seed);
    rngs_init(&rb, seed);
    (void)game_init(&a, &ra);
    (void)game_init(&b, &rb);

    for (i = 0; i < 600; i++) {
        SDL_zero(in);
        in.right = (i / 30) % 2;
        in.down = (i / 45) % 2;
        in.left = (i / 70) % 2;
        sim_step(&a, &in, TICK_DT);
        sim_step(&b, &in, TICK_DT);
        if (a.p.x != b.p.x || a.p.y != b.p.y)
            mismatch++;
    }
    printf("movement determinism (same seed+input) : %s\n",
           mismatch ? "MISMATCH" : "identical trajectory");
    return mismatch ? 1 : 0;
}

/* Checks the three properties World Generation actually depends on:
 * reproducibility, stream independence, and unbiased distribution. */
static int rng_selftest(Uint64 seed)
{
    enum { N = 100000, BUCKETS = 16 };
    Rngs a, b;
    Uint32 counts[BUCKETS];
    int i, fails = 0;

    printf("=== RNG selftest (PCG32, master seed %.0f) ===\n\n", (double)seed);

    /* 1. Reproducibility: same seed, same sequence. */
    {
        int same = 1;
        rngs_init(&a, seed);
        rngs_init(&b, seed);
        for (i = 0; i < N; i++)
            if (rng_next(&a.terrain) != rng_next(&b.terrain))
                same = 0;
        printf("reproducible (same seed -> same sequence) : %s\n", same ? "yes" : "NO");
        if (!same) fails++;
    }

    /* 2. Different seeds diverge. */
    {
        int diff = 0;
        rngs_init(&a, seed);
        rngs_init(&b, seed + 1);
        for (i = 0; i < 1000; i++)
            if (rng_next(&a.terrain) != rng_next(&b.terrain))
                diff++;
        printf("different seed -> different sequence      : %d/1000 differ\n", diff);
        if (diff < 990) fails++;
    }

    /* 3. THE property this design needs: draining one stream must not perturb
     *    another. Run 'a' with terrain drawn heavily, 'b' with terrain never
     *    touched, then compare entities and audio. Must match exactly. */
    {
        int ent_same = 1, aud_same = 1;
        rngs_init(&a, seed);
        rngs_init(&b, seed);
        for (i = 0; i < N; i++)
            (void)rng_next(&a.terrain); /* b's terrain is deliberately untouched */
        for (i = 0; i < 1000; i++) {
            if (rng_next(&a.entities) != rng_next(&b.entities)) ent_same = 0;
            if (rng_next(&a.audio) != rng_next(&b.audio)) aud_same = 0;
        }
        printf("streams independent (terrain -> entities) : %s\n", ent_same ? "yes" : "NO");
        printf("streams independent (terrain -> audio)    : %s\n", aud_same ? "yes" : "NO");
        if (!ent_same || !aud_same) fails++;
    }

    /* 4. Streams must not be trivially correlated with each other. */
    {
        int collisions = 0;
        rngs_init(&a, seed);
        for (i = 0; i < N; i++) {
            Uint32 t = rng_next(&a.terrain);
            if (t == rng_next(&a.entities) || t == rng_next(&a.audio))
                collisions++;
        }
        printf("cross-stream value collisions             : %d / %d\n", collisions, N);
        if (collisions > 10) fails++;
    }

    /* 5. Distribution of rng_below, which places fragments. Modulo bias here
     *    would quietly clump entities toward low indices. */
    {
        double expected = (double)N / BUCKETS, worst = 0.0;
        SDL_memset(counts, 0, sizeof(counts));
        rngs_init(&a, seed);
        for (i = 0; i < N; i++)
            counts[rng_below(&a.entities, BUCKETS)]++;
        for (i = 0; i < BUCKETS; i++) {
            double dev = 100.0 * ((double)counts[i] - expected) / expected;
            if (dev < 0) dev = -dev;
            if (dev > worst) worst = dev;
        }
        printf("rng_below(%d) worst bucket deviation      : %.2f%% (want < 5%%)\n",
               BUCKETS, worst);
        if (worst >= 5.0) fails++;
    }

    /* 6. Float range sanity for the audio stream. */
    {
        float lo = 2.0f, hi = -2.0f;
        double sum = 0.0;
        rngs_init(&a, seed);
        for (i = 0; i < N; i++) {
            float v = rng_bipolar(&a.audio);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            sum += v;
        }
        printf("rng_bipolar range                         : [%.5f, %.5f], mean %.5f\n",
               (double)lo, (double)hi, sum / N);
        if (lo < -1.0f || hi >= 1.0f) fails++;
        if (sum / N > 0.01 || sum / N < -0.01) fails++;
    }

    printf("\n%s (%d checks failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

/* Runs audio with no window for `ms`, then reports whether the callback met
 * its deadline and dumps raw samples for independent offline analysis. */
static int audio_selftest(int argc, char **argv, int ms)
{
    Audio a;
    SDL_AudioSpec have;
    SDL_AudioDeviceID dev;
    const char *dump = arg_val(argc, argv, "--dump");
    double period_ms, max_ms, freq;
    int i;

    SDL_zero(a);
    a.noise = arg_flag(argc, argv, "--noise");
    /* --rate exists purely to exercise the sample-rate-conversion path on a
     * machine whose device happens to match our request exactly. */
    a.req_rate = arg_int(argc, argv, "--rate", AUDIO_RATE);
    rng_seed(&a.rng, (Uint64)arg_int(argc, argv, "--seed", 1), STREAM_AUDIO);

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("FAIL  SDL_Init(AUDIO): %s\n", SDL_GetError());
        return 1;
    }

    a.cap_cap = AUDIO_RATE * AUDIO_CHANNELS * (ms / 1000 + 2);
    a.cap = (float *)SDL_malloc((size_t)a.cap_cap * sizeof(float));
    if (!a.cap) {
        printf("FAIL  capture buffer alloc\n");
        SDL_Quit();
        return 1;
    }

    dev = audio_open(&a, &have);
    if (!dev) {
        printf("FAIL  SDL_OpenAudioDevice: %s\n", SDL_GetError());
        SDL_free(a.cap);
        SDL_Quit();
        return 1;
    }

    printf("device   : %s\n", SDL_GetCurrentAudioDriver());
    printf("requested: %d Hz, F32, %d ch, %d frames/cb\n",
           a.req_rate, AUDIO_CHANNELS, AUDIO_SAMPLES);
    printf("obtained : %d Hz, format 0x%04X, %d ch, %d frames/cb\n",
           have.freq, (unsigned)have.format, have.channels, have.samples);
    printf("converted: %s\n",
           (have.freq == a.req_rate && have.format == AUDIO_F32SYS &&
            have.channels == AUDIO_CHANNELS)
               ? "no - device matched the request exactly"
               : "YES - SDL inserted a conversion");
    printf("signal   : %s, seed %d\n", a.noise ? "seeded white noise" : "440 Hz sine",
           arg_int(argc, argv, "--seed", 1));

    SDL_PauseAudioDevice(dev, 0);
    SDL_Delay((Uint32)ms);
    SDL_PauseAudioDevice(dev, 1);
    SDL_CloseAudioDevice(dev); /* callback is stopped and joined; state is safe to read */

    period_ms = 1000.0 * (double)have.samples / (double)have.freq;
    max_ms = 1000.0 * (double)a.max_ticks / (double)SDL_GetPerformanceFrequency();

    printf("\n--- callback ---\n");
    printf("calls          : %.0f\n", (double)a.calls);
    printf("frames         : %.0f  (expected ~%.0f for %d ms)\n",
           (double)a.frames, (double)have.freq * ms / 1000.0, ms);
    printf("deadline       : %.3f ms per callback\n", period_ms);
    printf("worst case     : %.3f ms\n", max_ms);
    printf("headroom       : %.1f%% of deadline used\n", 100.0 * max_ms / period_ms);
    printf("partial writes : %d  (must be 0)\n", a.partial_len);

    /* Independent-ish signal check on the captured samples. */
    {
        double peak = 0.0, sumsq = 0.0;
        int nan = 0, clipped = 0, crossings = 0;
        float prev = 0.0f;
        for (i = 0; i < a.cap_len; i++) {
            float v = a.cap[i];
            if (v != v) { nan++; continue; }
            if (v > 1.0f || v < -1.0f) clipped++;
            if (v > peak) peak = v;
            if (-v > peak) peak = -v;
            sumsq += (double)v * v;
            if (i % a.channels == 0) { /* left channel only */
                if ((prev < 0.0f && v >= 0.0f) || (prev >= 0.0f && v < 0.0f))
                    crossings++;
                prev = v;
            }
        }
        freq = a.cap_len > 0
                   ? (double)crossings * (double)have.freq / (2.0 * (double)a.cap_len / a.channels)
                   : 0.0;
        printf("\n--- signal (%d samples captured) ---\n", a.cap_len);
        printf("peak           : %.4f  (amplitude is %.2f)\n", peak, (double)TONE_AMP);
        printf("rms            : %.4f\n",
               a.cap_len ? SDL_sqrt(sumsq / a.cap_len) : 0.0);
        printf("NaN            : %d  (must be 0)\n", nan);
        printf("out of range   : %d  (must be 0)\n", clipped);
        printf("zero crossings : %d -> %.1f Hz%s\n", crossings, freq,
               a.noise ? " (meaningless for noise)" : " (expected 440.0)");
    }

    if (dump) {
        FILE *f = fopen(dump, "wb");
        if (f) {
            fwrite(a.cap, sizeof(float), (size_t)a.cap_len, f);
            fclose(f);
            printf("\ndumped %d floats to %s\n", a.cap_len, dump);
        } else {
            printf("\nFAIL  could not open %s for writing\n", dump);
        }
    }

    SDL_free(a.cap);
    SDL_Quit();
    return 0;
}
#endif /* WAYFARER_SELFTEST */

/* ----------------------------------------------------------------- main -- */

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Surface *fb;
    SDL_Event ev;
    Audio audio;
    Rngs rngs;
    SDL_AudioSpec have;
    SDL_AudioDeviceID dev;
    Game game; /* local, not static — see the note in world_gen */
    Uint64 prev;
    double perf;
    float acc = 0.0f;
    int limit = arg_int(argc, argv, "--frames", 0);
    int frame = 0;
    int running = 1;

#if WAYFARER_SELFTEST
    {
        int ms = arg_int(argc, argv, "--audio-test", 0);
        if (ms > 0)
            return audio_selftest(argc, argv, ms);
        if (arg_flag(argc, argv, "--rng-test"))
            return rng_selftest((Uint64)arg_int(argc, argv, "--seed", 1));
        {
            int ims = arg_int(argc, argv, "--input-test", 0);
            if (ims > 0)
                return input_selftest((Uint64)arg_int(argc, argv, "--seed", 1), ims);
        }
        if (arg_flag(argc, argv, "--move-test")) {
            int n = arg_int(argc, argv, "--seeds", 20);
            int base = arg_int(argc, argv, "--seed", 1);
            int s, bad = 0;
            printf("=== movement/collision selftest, %d seeds ===\n", n);
            for (s = 0; s < n; s++)
                bad += move_selftest((Uint64)(base + s), 1);
            printf("\n");
            bad += speed_selftest();
            bad += move_determinism_test((Uint64)base);
            printf("\n%s (%d failures across %d seeds)\n", bad ? "FAIL" : "PASS", bad, n);
            return bad ? 1 : 0;
        }
    }
#endif

    /* Video is required; audio is not. These must be separate calls — SDL_Init
     * fails if ANY requested subsystem fails, so asking for VIDEO|AUDIO here
     * would refuse to start the game on a machine with no working sound
     * device. Verified: with SDL_AUDIODRIVER set to a bogus value the combined
     * call returned -1 and the game exited. */
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 1;

    win = SDL_CreateWindow("Wayfarer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WIN_W, WIN_H, SDL_WINDOW_SHOWN);
    if (!win) {
        SDL_Quit();
        return 2;
    }

    SDL_zero(audio);
    audio.noise = arg_flag(argc, argv, "--noise");
    audio.req_rate = AUDIO_RATE;
    rngs_init(&rngs, (Uint64)arg_int(argc, argv, "--seed", 1));
    audio.rng = rngs.audio;

    /* Silence is an acceptable degraded mode; failing to launch is not. */
    dev = 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        dev = audio_open(&audio, &have);
        if (dev)
            SDL_PauseAudioDevice(dev, 0);
    }

    (void)game_init(&game, &rngs);

    prev = SDL_GetPerformanceCounter();
    perf = (double)SDL_GetPerformanceFrequency();

    while (running) {
        Uint64 now;
        double elapsed;
        Input in;

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
        }

        input_poll(&in);

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - prev) / perf;
        prev = now;
        /* Clamp: after a breakpoint or a window drag, a huge elapsed would
         * otherwise spin the catch-up loop for thousands of steps. */
        if (elapsed > 0.25)
            elapsed = 0.25;
        acc += (float)elapsed;

        while (acc >= TICK_DT) {
            sim_step(&game, &in, TICK_DT);
            acc -= TICK_DT;
        }

        /* Re-fetch every frame: the surface is invalidated on resize. */
        fb = SDL_GetWindowSurface(win);
        if (!fb || fb->format->BytesPerPixel != 4) {
            if (dev)
                SDL_CloseAudioDevice(dev);
            SDL_DestroyWindow(win);
            SDL_Quit();
            return fb ? 4 : 3;
        }

        camera_follow(&game, fb->w, fb->h);
        render(fb, &game);
        SDL_UpdateWindowSurface(win);

        /* Cap the render rate. There is no vsync to lean on: SDL's render
         * subsystem (and with it PRESENTVSYNC) is compiled out, and
         * SDL_UpdateWindowSurface does not block on the display. Without this
         * the loop free-runs at thousands of fps and pegs a core to draw
         * frames nobody sees. Simulation is already fixed-step, so this
         * affects only how often we redraw. */
        {
            double spent = (double)(SDL_GetPerformanceCounter() - now) / perf;
            double budget = 1.0 / FRAME_HZ;
            if (spent < budget) {
                Uint32 nap = (Uint32)((budget - spent) * 1000.0);
                if (nap > 0)
                    SDL_Delay(nap);
            }
        }

        frame++;
        if (limit && frame >= limit)
            running = 0;
    }

    if (dev)
        SDL_CloseAudioDevice(dev);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
