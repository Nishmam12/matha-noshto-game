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

/* Render instrumentation rides with the self-test scaffolding, for the same
 * reason everything else does: the shipping binary must carry no measurement
 * code at all, and it has no stdout to report into (-mwindows, no console).
 * Both builds run the identical render path, so numbers measured in one apply
 * to the other. */
#ifndef WAYFARER_PERF
#define WAYFARER_PERF WAYFARER_SELFTEST
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

#define REVEAL_TILES 5     /* sight radius, in tiles */
#define REVEAL_RATE  2.5f  /* sight units/sec */

/* Walking somewhere reveals its SHAPE but not its colour — sight tops out well
 * below 1. Only restoring a memory takes a region to full colour.
 *
 * This splits Fog and Reveal's single "restoration%" into two contributions,
 * and that is an interpretation worth flagging: the note specifies restoration
 * drives the blend, but with nothing else the world would be pitch black until
 * the first fragment is restored — including the fragment you must find first.
 * Sight keeps exploration possible; restoration is still the only thing that
 * brings colour back. */
#define SIGHT_MAX    0.42f
#define RESTORE_RATE 0.9f  /* region restoration units/sec once triggered */

#define INTERACT_RADIUS 22.0f /* world px */

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

/* Restore confirm beat. A plain decaying sine for now; the real layered synth
 * is Week 4 (design/systems/Audio and Synth.md). */
#define SFX_HZ    660.0
#define SFX_AMP   0.28f
#define SFX_DECAY 3.2f /* envelope units/sec; ~0.3 s tail */

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
 * Now used by entity placement, so no longer self-test-only. */
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
    int    tone;  /* ambient test tone; off in the game, on in audio selftests */
    Rng    rng;

    /* Restore confirm beat. The main thread only ever bumps an atomic counter;
     * the callback owns everything else. No lock, no allocation, no shared
     * mutable state on the audio thread's critical path — the audio callback
     * has a hard deadline and Agent Prompt.md treats faults there as release
     * blockers. */
    SDL_atomic_t sfx_fire;
    int          sfx_seen;
    double       sfx_phase;
    float        sfx_env;
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

    /* Latch the trigger once per callback, not per sample. */
    {
        int fired = SDL_AtomicGet(&a->sfx_fire);
        if (fired != a->sfx_seen) {
            a->sfx_seen = fired;
            a->sfx_env = 1.0f;
            a->sfx_phase = 0.0;
        }
    }

    for (i = 0; i < frames; i++) {
        float v = 0.0f;
        if (a->noise) {
            v = rng_bipolar(&a->rng) * TONE_AMP;
        } else if (a->tone) {
            v = SDL_sinf((float)(a->phase * TWO_PI)) * TONE_AMP;
            a->phase += inc;
            if (a->phase >= 1.0)
                a->phase -= 1.0;
        }
        if (a->sfx_env > 0.0f) {
            v += SDL_sinf((float)(a->sfx_phase * TWO_PI)) * a->sfx_env * SFX_AMP;
            a->sfx_phase += SFX_HZ / (double)a->rate;
            if (a->sfx_phase >= 1.0)
                a->sfx_phase -= 1.0;
            a->sfx_env -= SFX_DECAY / (float)a->rate;
            if (a->sfx_env < 0.0f)
                a->sfx_env = 0.0f;
        }
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
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

/* --------------------------------------------------------------- regions -- */
/* The world is a region graph (design/systems/World Generation.md): each region
 * has a terrain type, an ability required to enter, a restoration state, and
 * (Week 3) the fragments it contains.
 *
 * REGION_COUNT is deliberately small. Open Decisions leaves landmass size
 * unresolved with the instruction to "start small, expand only if generation +
 * pacing tests support it", so this is the small end and is meant to be raised
 * against measurements, not guessed upward. */
#define REGION_COUNT 16
#define REGION_NONE  0xFF

enum {
    TERRAIN_NORMAL = 0,
    TERRAIN_WATER, /* Wade */
    TERRAIN_LEDGE, /* Climb */
    TERRAIN_DARK,  /* Kindle */
    TERRAIN_COUNT
};

/* Ability flags. Bitmask because "which abilities do you have" is the whole of
 * the ability system — see design/systems/Abilities.md: no trees, no levels. */
enum {
    ABIL_NONE   = 0,
    ABIL_WADE   = 1 << 0,
    ABIL_CLIMB  = 1 << 1,
    ABIL_KINDLE = 1 << 2
};

static const Uint8 terrain_requires[TERRAIN_COUNT] = {
    ABIL_NONE, ABIL_WADE, ABIL_CLIMB, ABIL_KINDLE
};

typedef struct {
    Uint8  terrain;
    Uint16 tiles;
    int    seed_tile;   /* representative tile, for debug draw and spawn */
    Uint32 adj;         /* bitmask of adjacent regions; caps REGION_COUNT at 32 */
    float  restoration; /* 0..1, drives Fog and Reveal */
    float  restore_to;  /* target; restoration eases toward it so the colour
                         * returning is a visible beat, not an instant swap */
} Region;

/* Fragments and Found Souls. Counts are the scope-safe defaults from
 * design/Overview.md (~12-16 fragments, 4-6 Found Souls), tracked separately
 * rather than as the mockup's combined 23/40 counter. Still awaiting formal
 * sign-off in design/Open Decisions.md — these are defaults, not a decision.
 *
 * Found Souls reuse the fragment restoration path entirely; is_soul only
 * changes how they draw and what they add to the mix. */
#define FRAGMENT_COUNT 14
#define SOUL_COUNT     5
#define ENTITY_COUNT   (FRAGMENT_COUNT + SOUL_COUNT) /* must stay <= 32 */

typedef struct {
    int   tile;
    Uint8 region;
    Uint8 grants;   /* ABIL_* this fragment restores, or 0 */
    Uint8 is_soul;  /* Found Soul rather than a plain fragment */
    Uint8 restored;
} Entity;

typedef struct {
    Uint8  solid[WORLD_H][WORLD_W];
    float  reveal[WORLD_H][WORLD_W]; /* 0 = fogged and colourless, 1 = restored */
    Uint8  region[WORLD_H][WORLD_W]; /* REGION_NONE where solid or unreachable */
    Region regions[REGION_COUNT];
    int    region_count;
    int    spawn_region;
} World;

/* Scratch buffers for generation. One struct so callers allocate it once as a
 * stack local — these must never be `static`, see the .data trap in
 * design/Toolchain Setup.md. */
typedef struct {
    Uint8 seen[WORLD_W * WORLD_H];
    int   stack[WORLD_W * WORLD_H];
    int   queue[WORLD_W * WORLD_H];
    int   dist[WORLD_W * WORLD_H];
    Uint8 owner[WORLD_W * WORLD_H];
} Scratch;

typedef struct {
    float x, y;      /* centre, in world pixels */
    Uint8 abilities; /* ABIL_* bitmask; the entire ability system, per Abilities.md */
} Player;

typedef struct {
    World  w;
    Player p;
    Entity ents[ENTITY_COUNT];
    int    gen_attempts;    /* >0 attempts used; <0 means gating was relaxed */
    int    frags_restored;  /* tracked separately from souls, per Fragments.md */
    int    souls_restored;
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

/* Multi-source BFS across open tiles. Fills dist (hop count, -1 unreachable)
 * and, when owner is non-NULL, which source claimed each tile. Because regions
 * grow outward from their seeds in lockstep, every region it produces is
 * connected by construction — there is no way to end up with an island of tiles
 * assigned to a region they cannot walk to. */
static void bfs_open(const World *w, const int *sources, int nsrc,
                     int *dist, Uint8 *owner, int *queue)
{
    int head = 0, tail = 0, i;

    for (i = 0; i < WORLD_W * WORLD_H; i++) {
        dist[i] = -1;
        if (owner)
            owner[i] = REGION_NONE;
    }
    for (i = 0; i < nsrc; i++) {
        dist[sources[i]] = 0;
        if (owner)
            owner[sources[i]] = (Uint8)i;
        queue[tail++] = sources[i];
    }

    while (head < tail) {
        int idx = queue[head++];
        int x = idx % WORLD_W, y = idx / WORLD_W, d;
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };

        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d], nidx;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H)
                continue;
            if (w->solid[ny][nx])
                continue;
            nidx = ny * WORLD_W + nx;
            if (dist[nidx] >= 0)
                continue;
            dist[nidx] = dist[idx] + 1;
            if (owner)
                owner[nidx] = owner[idx];
            queue[tail++] = nidx;
        }
    }
}

/* Partition the walkable area into REGION_COUNT connected regions.
 *
 * Seeds are chosen by farthest-point sampling — repeatedly take the walkable
 * tile furthest (in path distance, not straight line) from every seed chosen so
 * far. Path distance matters: two tiles either side of a wall are close in
 * space but far apart to walk, and sampling on straight-line distance produces
 * regions that straddle walls. */
static int regions_build(World *w, Scratch *sc, int spawn_tile)
{
    int sources[REGION_COUNT];
    int nsrc = 1, i, x, y, count;

    sources[0] = spawn_tile;

    while (nsrc < REGION_COUNT) {
        int best = -1, best_d = 0;
        bfs_open(w, sources, nsrc, sc->dist, NULL, sc->queue);
        for (i = 0; i < WORLD_W * WORLD_H; i++) {
            if (sc->dist[i] > best_d) {
                best_d = sc->dist[i];
                best = i;
            }
        }
        if (best < 0) /* component too small to hold another region */
            break;
        sources[nsrc++] = best;
    }

    bfs_open(w, sources, nsrc, sc->dist, sc->owner, sc->queue);

    count = nsrc;
    w->region_count = count;
    for (i = 0; i < count; i++) {
        w->regions[i].terrain = TERRAIN_NORMAL;
        w->regions[i].tiles = 0;
        w->regions[i].seed_tile = sources[i];
        w->regions[i].adj = 0;
        w->regions[i].restoration = 0.0f;
    }

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            Uint8 r = sc->owner[y * WORLD_W + x];
            w->region[y][x] = r;
            if (r != REGION_NONE)
                w->regions[r].tiles++;
        }
    }

    /* Adjacency. Recorded both ways so the graph is symmetric by construction
     * rather than by hoping both passes agree. */
    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            Uint8 a = w->region[y][x];
            if (a == REGION_NONE)
                continue;
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

    w->spawn_region = (int)w->region[spawn_tile / WORLD_W][spawn_tile % WORLD_W];
    return count;
}

/* Hop distance from the spawn region through the region graph, ignoring ability
 * gates. Used to bias where gated terrain goes: gates far from spawn produce a
 * sensible difficulty ramp, gates adjacent to spawn produce a wall in the
 * player's face on turn one. */
static void regions_depth(const World *w, int *depth)
{
    int queue[REGION_COUNT], head = 0, tail = 0, i;

    for (i = 0; i < w->region_count; i++)
        depth[i] = -1;
    if (w->spawn_region < 0 || w->spawn_region >= w->region_count)
        return;

    depth[w->spawn_region] = 0;
    queue[tail++] = w->spawn_region;
    while (head < tail) {
        int r = queue[head++];
        for (i = 0; i < w->region_count; i++) {
            if ((w->regions[r].adj & (1u << i)) && depth[i] < 0) {
                depth[i] = depth[r] + 1;
                queue[tail++] = i;
            }
        }
    }
}

/* Assign terrain, biased by depth so gating ramps outward from spawn. This only
 * has to be *plausible* — the reachability invariant is what makes it correct,
 * and it regenerates this if the layout turns out unsolvable. */
static void regions_assign_terrain(World *w, Rng *rng, const int *depth)
{
    int i;
    int max_depth = 0;

    for (i = 0; i < w->region_count; i++)
        if (depth[i] > max_depth)
            max_depth = depth[i];

    for (i = 0; i < w->region_count; i++) {
        w->regions[i].terrain = TERRAIN_NORMAL;
        if (i == w->spawn_region || depth[i] <= 1 || max_depth == 0)
            continue; /* spawn and its immediate neighbours stay open */
        {
            float t = (float)depth[i] / (float)max_depth;
            if (rng_float(rng) < t * 0.75f)
                w->regions[i].terrain =
                    (Uint8)(TERRAIN_WATER + (int)(rng_float(rng) * 3.0f) % 3);
        }
    }
}

/* ------------------------------------------------- reachability invariant -- */

/* Which regions can be entered with this ability set, walking out from spawn.
 * Bitmask over regions — REGION_COUNT is capped at 32 for exactly this. */
static Uint32 regions_reachable(const World *w, Uint8 abilities)
{
    Uint32 visited = 0;
    int queue[REGION_COUNT], head = 0, tail = 0, i;
    int start = w->spawn_region;

    if (start < 0 || start >= w->region_count)
        return 0;
    if (terrain_requires[w->regions[start].terrain] & ~abilities)
        return 0;

    visited = 1u << start;
    queue[tail++] = start;
    while (head < tail) {
        int r = queue[head++];
        for (i = 0; i < w->region_count; i++) {
            if (!(w->regions[r].adj & (1u << i)))
                continue;
            if (visited & (1u << i))
                continue;
            if (terrain_requires[w->regions[i].terrain] & ~abilities)
                continue;
            visited |= 1u << i;
            queue[tail++] = i;
        }
    }
    return visited;
}

/* THE invariant (design/systems/World Generation.md): at every ability tier the
 * player holds, at least one un-restored fragment must be reachable. Simulated
 * as an actual playthrough — repeatedly restore everything currently reachable,
 * bank any abilities that grants, and see if the frontier ever opens further.
 * If the loop stalls with entities left, the world has a dead end.
 *
 * Cheaper and stricter than eyeballing a map, and it is the only thing standing
 * between us and an unwinnable seed reaching a judge. */
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
            if (restored & (1u << i))
                continue;
            if (ents[i].region >= w->region_count)
                continue;
            if (!(reach & (1u << ents[i].region)))
                continue;
            restored |= 1u << i;
            abilities |= ents[i].grants;
            count++;
            progressed = 1;
        }
    }
    if (out_restored)
        *out_restored = count;
    return count == ENTITY_COUNT;
}

static int pick_region(Uint32 mask, int region_count, Rng *rng)
{
    int list[REGION_COUNT], n = 0, i;

    for (i = 0; i < region_count; i++)
        if (mask & (1u << i))
            list[n++] = i;
    if (n == 0)
        return -1;
    return list[rng_below(rng, (Uint32)n)];
}

/* Reservoir sampling: one pass, uniform, no temporary tile list. */
static int pick_tile_in_region(const World *w, int r, Rng *rng)
{
    int chosen = -1, seen = 0, x, y;

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            if (w->region[y][x] != r)
                continue;
            seen++;
            if (rng_below(rng, (Uint32)seen) == 0)
                chosen = y * WORLD_W + x;
        }
    }
    return chosen;
}

/* Place the three ability grants on the advancing frontier — each one inside
 * what is reachable *before* it is granted — then scatter the rest anywhere
 * reachable once everything is held. Placing by frontier rather than at random
 * makes solvable layouts the common case; world_solvable is still what proves
 * it, since this alone guarantees nothing. */
static void place_entities(World *w, Rng *rng, Entity *ents)
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

    for (i = 0; i < 3; i++) {
        Uint32 reach = regions_reachable(w, held);
        int r = pick_region(reach, w->region_count, rng);
        if (r < 0)
            break;
        ents[i].region = (Uint8)r;
        ents[i].tile = pick_tile_in_region(w, r, rng);
        ents[i].grants = grant_order[i];
        held |= grant_order[i];
    }

    {
        Uint32 reach = regions_reachable(w, held);
        for (i = 3; i < ENTITY_COUNT; i++) {
            int r = pick_region(reach, w->region_count, rng);
            if (r < 0)
                r = w->spawn_region;
            ents[i].region = (Uint8)r;
            ents[i].tile = pick_tile_in_region(w, r, rng);
        }
    }
}

/* Generate-then-verify, with a fallback that cannot fail. Returns attempts used
 * (positive), or a negative depth if it had to ungate regions to guarantee
 * solvability. design/Cut List.md lists the reachability guarantee as never
 * cuttable, so losing some gating is the correct trade against shipping a seed
 * that cannot be completed. */
static int world_place_and_verify(World *w, Rngs *rngs, const int *depth, Entity *ents)
{
    int attempt, d, i;

    for (attempt = 0; attempt < 64; attempt++) {
        regions_assign_terrain(w, &rngs->terrain, depth);
        place_entities(w, &rngs->entities, ents);
        if (world_solvable(w, ents, NULL))
            return attempt + 1;
    }

    /* Ungate outward, shallowest first, keeping as much gating as possible. */
    for (d = 1; d <= REGION_COUNT; d++) {
        for (i = 0; i < w->region_count; i++)
            if (depth[i] == d)
                w->regions[i].terrain = TERRAIN_NORMAL;
        place_entities(w, &rngs->entities, ents);
        if (world_solvable(w, ents, NULL))
            return -d;
    }

    for (i = 0; i < w->region_count; i++)
        w->regions[i].terrain = TERRAIN_NORMAL;
    place_entities(w, &rngs->entities, ents);
    return -100;
}

/* Can a tile be stood on with this ability set? Rock always blocks; open ground
 * blocks when its region demands an ability the player has not recovered.
 * This is the whole of terrain gating, per design/systems/Abilities.md. */
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

/* Does the player's AABB, centred here, overlap any blocked tile? */
static int player_blocked(const World *w, Uint8 abilities, float cx, float cy)
{
    float h = PLAYER_SIZE * 0.5f;
    int x0 = (int)SDL_floorf((cx - h) / TILE);
    int x1 = (int)SDL_floorf((cx + h - 0.001f) / TILE);
    int y0 = (int)SDL_floorf((cy - h) / TILE);
    int y1 = (int)SDL_floorf((cy + h - 0.001f) / TILE);
    int tx, ty;

    for (ty = y0; ty <= y1; ty++)
        for (tx = x0; tx <= x1; tx++)
            if (tile_blocked(w, abilities, tx, ty))
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
        if (player_blocked(&g->w, g->p.abilities, nx, ny))
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
            /* Tapers to nothing at the edge, so the boundary is a soft gradient
             * rather than a visible disc. Capped at SIGHT_MAX: walking past
             * something never fully restores it. */
            target = SIGHT_MAX * (1.0f - (float)d2 / (float)(r * r));
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

/* Nearest un-restored entity within reach, or -1. Proximity + a keypress is the
 * whole interaction — design/systems/Fragments.md is explicit that restoration
 * is "a short confirm beat, not a puzzle-minigame". */
static int entity_in_reach(const Game *g)
{
    int best = -1, i;
    float best_d2 = INTERACT_RADIUS * INTERACT_RADIUS;

    for (i = 0; i < ENTITY_COUNT; i++) {
        float ex, ey, dx, dy, d2;
        if (g->ents[i].restored || g->ents[i].tile < 0)
            continue;
        ex = (float)(g->ents[i].tile % WORLD_W) * TILE + TILE * 0.5f;
        ey = (float)(g->ents[i].tile / WORLD_W) * TILE + TILE * 0.5f;
        dx = ex - g->p.x;
        dy = ey - g->p.y;
        d2 = dx * dx + dy * dy;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = i;
        }
    }
    return best;
}

/* The loop the whole game is built around: restore a memory, the region's
 * colour returns, and sometimes an ability comes back with it and opens terrain
 * that was closed a moment ago. Returns the entity restored, or -1. */
static int try_restore(Game *g)
{
    int i = entity_in_reach(g);

    if (i < 0)
        return -1;
    g->ents[i].restored = 1;
    g->p.abilities |= g->ents[i].grants;
    if (g->ents[i].region < g->w.region_count)
        g->w.regions[g->ents[i].region].restore_to = 1.0f;
    if (g->ents[i].is_soul)
        g->souls_restored++;
    else
        g->frags_restored++;
    return i;
}

static int game_complete(const Game *g)
{
    return g->frags_restored + g->souls_restored >= ENTITY_COUNT;
}

/* The 4-stage world-growth read from design/Overview.md. Purely a display
 * bucketing of the same underlying per-region float. */
static int world_stage(const Game *g)
{
    float sum = 0.0f;
    int i;

    if (game_complete(g))
        return 3; /* Fully Restored */
    if (g->w.region_count <= 0)
        return 0;
    for (i = 0; i < g->w.region_count; i++)
        sum += g->w.regions[i].restoration;
    sum /= (float)g->w.region_count;
    if (sum <= 0.001f)
        return 0; /* Unexplored */
    if (sum < 0.5f)
        return 1; /* Partly Revealed */
    return 2;     /* Many Memories Restored */
}

static void sim_step(Game *g, const Input *in, float dt)
{
    float mx = (float)(in->right - in->left);
    float my = (float)(in->down - in->up);
    int i;

    /* Normalise diagonals, or moving corner-wise is 1.41x faster than straight. */
    if (mx != 0.0f && my != 0.0f) {
        mx *= 0.70710678f;
        my *= 0.70710678f;
    }

    move_axis(g, mx * PLAYER_SPEED * dt, 0.0f);
    move_axis(g, 0.0f, my * PLAYER_SPEED * dt);
    reveal_around(g, dt);

    /* Ease each region's colour back rather than snapping it. */
    for (i = 0; i < g->w.region_count; i++) {
        Region *r = &g->w.regions[i];
        if (r->restoration < r->restore_to) {
            r->restoration += RESTORE_RATE * dt;
            if (r->restoration > r->restore_to)
                r->restoration = r->restore_to;
        }
    }
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
    /* Scratch is a local, not a static — see the .data trap in
     * design/Toolchain Setup.md. ~47 KB of frame against a 2 MB stack. */
    Scratch sc;
    Uint8 *seen = sc.seen;
    int *stack = sc.stack;
    int depth[REGION_COUNT];
    int x, y, biggest = 0, biggest_first = -1;

    /* Wipe everything first. Leaving progress counters alone made restoration
     * totals accumulate across regenerations — pressing R would have carried
     * the previous world's fragment count into the new one, and
     * game_complete() would fire on a world nobody had touched. */
    SDL_zero(*g);

    world_gen(&g->w, &rngs->terrain);
    SDL_memset(seen, 0, sizeof(sc.seen));

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
        g->w.region_count = 0;
        g->w.spawn_region = -1;
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

        /* sizeof(sc.seen), NOT sizeof(seen): `seen` is a pointer, so sizeof
         * would be 8 and this memset would clear almost nothing — leaving pass
         * 1's marks in place, making this flood return immediately and letting
         * the centroid search below range over every component instead of this
         * one. That put spawns in tiny side pockets. */
        SDL_memset(seen, 0, sizeof(sc.seen));
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

        regions_build(&g->w, &sc, best_idx);
        regions_depth(&g->w, depth);
        g->gen_attempts = world_place_and_verify(&g->w, rngs, depth, g->ents);
    }

    g->cam_x = 0;
    g->cam_y = 0;
    return biggest; /* open tiles reachable from spawn */
}

/* ----------------------------------------------------------------- perf -- */
/* Render instrumentation.
 *
 * This exists because "~56 fps" was never evidence of anything. The loop sleeps
 * on purpose, and SDL_Delay's millisecond granularity sets the frame period by
 * itself — Sleep(14) routinely returns at 15-16 ms on Windows. So the one
 * number we had measured sleep, not drawing, and the render cost of the tile
 * loop has never been measured at all (design/Toolchain Setup.md lists it as
 * unprofiled). Separate the two before changing the renderer, so the isometric
 * work has a real baseline to be compared against rather than a guess. */
#if WAYFARER_PERF

/* Bumped by every fill_rect call, sampled and cleared once per frame. Scalars,
 * not buffers: the never-declare-it-static rule in design/Toolchain Setup.md is
 * about world-sized arrays landing in .data, and these are eight bytes. */
static Uint32 perf_px;
static Uint32 perf_calls;

#define PERF_COUNT(n) do { perf_px += (Uint32)(n); perf_calls++; } while (0)

typedef struct {
    double render_sum, render_max; /* rasterising, alone */
    double present_sum;            /* SDL_UpdateWindowSurface: the blit to the OS */
    double sleep_sum;              /* what the frame cap actually costs */
    double frame_sum;              /* whole loop period */
    double px_sum, calls_sum;
    int    frames;
} Perf;

static void perf_frame(Perf *p, double render_ms, double present_ms,
                       double sleep_ms, double frame_ms)
{
    if (render_ms > p->render_max)
        p->render_max = render_ms;
    p->render_sum  += render_ms;
    p->present_sum += present_ms;
    p->sleep_sum   += sleep_ms;
    p->frame_sum   += frame_ms;
    p->px_sum      += (double)perf_px;
    p->calls_sum   += (double)perf_calls;
    p->frames++;
    perf_px = 0;
    perf_calls = 0;
}

static void perf_report(const Perf *p, int w, int h)
{
    double n     = (double)(p->frames > 0 ? p->frames : 1);
    double frame = p->frame_sum / n;
    double px    = p->px_sum / n;

    printf("=== render perf: %d frames at %dx%d ===\n", p->frames, w, h);
    printf("render   mean %7.3f ms    max %7.3f ms\n", p->render_sum / n, p->render_max);
    printf("present  mean %7.3f ms\n", p->present_sum / n);
    printf("sleep    mean %7.3f ms    <- frame cap; SDL_Delay granularity lands here\n",
           p->sleep_sum / n);
    printf("frame    mean %7.3f ms    = %.1f fps\n", frame, frame > 0.0 ? 1000.0 / frame : 0.0);
    printf("pixels   mean %9.0f /frame  = %.2fx the %d-px screen\n",
           px, (w * h) > 0 ? px / (double)(w * h) : 0.0, w * h);
    printf("calls    mean %9.0f /frame\n", p->calls_sum / n);
}
#else
#define PERF_COUNT(n) ((void)0)
#endif /* WAYFARER_PERF */

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

    /* Counted after clipping, so this is real writes rather than requested
     * ones — an off-screen tile must not inflate the number. */
    PERF_COUNT(w * h);

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

/* True colour per terrain, before the fog blend. Flat-shaded and readable —
 * design/Overview.md is explicit that the painted mockup is pitch art and the
 * in-engine target is simple procedural geometry. */
static void terrain_colour(int terrain, int *r, int *g, int *b)
{
    switch (terrain) {
    case TERRAIN_WATER: *r = 0x3a; *g = 0x72; *b = 0xa8; break; /* Wade */
    case TERRAIN_LEDGE: *r = 0x8a; *g = 0x7a; *b = 0x5a; break; /* Climb */
    case TERRAIN_DARK:  *r = 0x4a; *g = 0x3a; *b = 0x6a; break; /* Kindle */
    default:            *r = 0x4e; *g = 0x9e; *b = 0x54; break;
    }
}

static void render(SDL_Surface *fb, Game *g, int overlay)
{
    int tx, ty, i;
    int tx0 = g->cam_x / TILE;
    int ty0 = g->cam_y / TILE;
    int tx1 = (g->cam_x + fb->w) / TILE + 1;
    int ty1 = (g->cam_y + fb->h) / TILE + 1;

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            Uint32 c;
            int cr, cg, cb;
            float rev;
            if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
                continue;
            {
                /* Sight shows shape; restoration brings colour. Whichever is
                 * stronger wins, so a restored region stays lit after you
                 * leave it — restoration is permanent, sight is not a memory
                 * of colour. */
                Uint8 rg = g->w.region[ty][tx];
                float restored = (rg == REGION_NONE) ? 0.0f
                                                     : g->w.regions[rg].restoration;
                rev = g->w.reveal[ty][tx];
                if (restored > rev)
                    rev = restored;
                if (overlay)
                    rev = 1.0f;
            }
            if (g->w.solid[ty][tx]) {
                cr = 0x5a; cg = 0x4a; cb = 0x3c;
            } else {
                Uint8 reg = g->w.region[ty][tx];
                terrain_colour(reg == REGION_NONE ? TERRAIN_NORMAL
                                                  : g->w.regions[reg].terrain,
                               &cr, &cg, &cb);
                /* Alternate brightness by region id so boundaries are visible
                 * without needing a font or an outline pass. */
                if (overlay && reg != REGION_NONE && (reg & 1)) {
                    cr = cr * 3 / 4; cg = cg * 3 / 4; cb = cb * 3 / 4;
                }
            }
            c = fog_lerp(fb, cr, cg, cb, rev);
            fill_rect(fb, tx * TILE - g->cam_x, ty * TILE - g->cam_y, TILE, TILE, c);
        }
    }

    /* Entities. Found Souls follow Lost -> Found -> Remembered from their design
     * note: unseen, then a pale grey silhouette, then coloured once restored.
     * Fragments glow warm, ability-granting ones brighter, and fade to a dim
     * marker once restored so a cleared region does not still look full of
     * things to do. */
    for (i = 0; i < ENTITY_COUNT; i++) {
        int t = g->ents[i].tile, ex, ey, s;
        int cr, cg, cb;
        if (t < 0)
            continue;
        ex = t % WORLD_W;
        ey = t / WORLD_W;
        if (!overlay && g->w.reveal[ey][ex] < 0.15f)
            continue; /* Lost: not yet discovered */
        if (g->ents[i].is_soul) {
            if (g->ents[i].restored) {
                cr = 0xf0; cg = 0xd0; cb = 0x90; s = 9; /* Remembered */
            } else {
                cr = 0x9a; cg = 0xa8; cb = 0xb8; s = 9; /* Found */
            }
        } else if (g->ents[i].restored) {
            cr = 0x6a; cg = 0x6a; cb = 0x62; s = 4;
        } else if (g->ents[i].grants) {
            cr = 0xff; cg = 0x9a; cb = 0x3c; s = 8;
        } else {
            cr = 0xff; cg = 0xd7; cb = 0x6a; s = 6;
        }
        fill_rect(fb, ex * TILE + (TILE - s) / 2 - g->cam_x,
                  ey * TILE + (TILE - s) / 2 - g->cam_y, s, s,
                  SDL_MapRGB(fb->format, (Uint8)cr, (Uint8)cg, (Uint8)cb));
    }

    /* A ring under the player when something is close enough to restore —
     * the only affordance telling you the interact key will do anything. */
    if (entity_in_reach(g) >= 0) {
        int px = (int)g->p.x - g->cam_x, py = (int)g->p.y - g->cam_y;
        Uint32 c = SDL_MapRGB(fb->format, 0xff, 0xf0, 0xc0);
        fill_rect(fb, px - 11, py - 13, 22, 2, c);
        fill_rect(fb, px - 11, py + 11, 22, 2, c);
        fill_rect(fb, px - 13, py - 11, 2, 22, c);
        fill_rect(fb, px + 11, py - 11, 2, 22, c);
    }

    fill_rect(fb, (int)g->p.x - PLAYER_SIZE / 2 - g->cam_x,
              (int)g->p.y - PLAYER_SIZE / 2 - g->cam_y, PLAYER_SIZE, PLAYER_SIZE,
              SDL_MapRGB(fb->format, 0xe0, 0x64, 0x28));
}

/* Grid view: several seeds at once, which is how you spot a generator that is
 * subtly biased far faster than by walking one world at a time. Rendered once
 * into a heap buffer rather than regenerating every frame — generation is far
 * too slow to run 12 worlds per frame. */
#define GRID_COLS 4
#define GRID_ROWS 3
#define GRID_CELLS (GRID_COLS * GRID_ROWS)

static void render_grid(SDL_Surface *fb, Uint64 base_seed)
{
    int cell_w = fb->w / GRID_COLS;
    int cell_h = fb->h / GRID_ROWS;
    int c;

    fill_rect(fb, 0, 0, fb->w, fb->h, SDL_MapRGB(fb->format, 0x08, 0x0a, 0x0e));

    for (c = 0; c < GRID_CELLS; c++) {
        Game *g = (Game *)SDL_malloc(sizeof(Game));
        Rngs rngs;
        int px = (cell_w - 4) / WORLD_W;
        int py = (cell_h - 4) / WORLD_H;
        int ps = px < py ? px : py;
        int ox, oy, x, y, i;

        if (!g)
            return;
        if (ps < 1)
            ps = 1;

        /* Centre each thumbnail in its cell so the grid reads as a deliberate
         * layout rather than art stranded in the top-left of each box. */
        ox = (c % GRID_COLS) * cell_w + (cell_w - WORLD_W * ps) / 2;
        oy = (c / GRID_COLS) * cell_h + (cell_h - WORLD_H * ps) / 2;

        rngs_init(&rngs, base_seed + (Uint64)c);
        (void)game_init(g, &rngs);

        for (y = 0; y < WORLD_H; y++) {
            for (x = 0; x < WORLD_W; x++) {
                int cr, cg, cb;
                Uint8 reg = g->w.region[y][x];
                if (g->w.solid[y][x]) {
                    cr = 0x30; cg = 0x2a; cb = 0x24;
                } else {
                    terrain_colour(reg == REGION_NONE ? TERRAIN_NORMAL
                                                      : g->w.regions[reg].terrain,
                                   &cr, &cg, &cb);
                    if (reg != REGION_NONE && (reg & 1)) {
                        cr = cr * 3 / 4; cg = cg * 3 / 4; cb = cb * 3 / 4;
                    }
                }
                fill_rect(fb, ox + x * ps, oy + y * ps, ps, ps,
                          SDL_MapRGB(fb->format, (Uint8)cr, (Uint8)cg, (Uint8)cb));
            }
        }
        for (i = 0; i < ENTITY_COUNT; i++) {
            int t = g->ents[i].tile;
            if (t < 0)
                continue;
            fill_rect(fb, ox + (t % WORLD_W) * ps, oy + (t / WORLD_W) * ps,
                      ps + 1, ps + 1,
                      SDL_MapRGB(fb->format,
                                 g->ents[i].is_soul ? 0xb0 : 0xff,
                                 g->ents[i].is_soul ? 0xc4 : 0xd7,
                                 g->ents[i].is_soul ? 0xd8 : 0x6a));
        }
        /* Spawn marker. */
        fill_rect(fb, ox + (int)(g->p.x / TILE) * ps,
                  oy + (int)(g->p.y / TILE) * ps, ps + 2, ps + 2,
                  SDL_MapRGB(fb->format, 0xe0, 0x64, 0x28));
        SDL_free(g);
    }
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

    if (player_blocked(&g.w, g.p.abilities, g.p.x, g.p.y)) {
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
            if (player_blocked(&g.w, g.p.abilities, g.p.x, g.p.y))
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

/* Structural checks on the region graph. These are the properties everything in
 * Week 2 and 3 leans on; if any fails, fragment placement and the reachability
 * invariant are built on sand. */
static int region_selftest(Uint64 seed, int verbose)
{
    Game g;
    Rngs rngs;
    Scratch sc;
    int depth[REGION_COUNT];
    int i, x, y, fails = 0, open_tiles, assigned = 0;
    int counted[REGION_COUNT];
    int terrain_hist[TERRAIN_COUNT];

    rngs_init(&rngs, seed);
    open_tiles = game_init(&g, &rngs);
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            if (g.w.region[y][x] != REGION_NONE)
                assigned++;

    if (g.w.region_count < 2) {
        printf("  seed %.0f: only %d regions\n", (double)seed, g.w.region_count);
        return 1;
    }

    /* 1. Every region owns at least one tile, and tile counts agree. */
    for (i = 0; i < REGION_COUNT; i++)
        counted[i] = 0;
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            if (g.w.region[y][x] != REGION_NONE)
                counted[g.w.region[y][x]]++;
    for (i = 0; i < g.w.region_count; i++) {
        if (counted[i] == 0) {
            printf("  seed %.0f: region %d is empty\n", (double)seed, i);
            fails++;
        }
        if (counted[i] != g.w.regions[i].tiles) {
            printf("  seed %.0f: region %d tile count %d != recorded %d\n",
                   (double)seed, i, counted[i], g.w.regions[i].tiles);
            fails++;
        }
    }

    /* 2. Adjacency is symmetric and never self-referential. */
    for (i = 0; i < g.w.region_count; i++) {
        int j;
        if (g.w.regions[i].adj & (1u << i)) {
            printf("  seed %.0f: region %d adjacent to itself\n", (double)seed, i);
            fails++;
        }
        for (j = 0; j < g.w.region_count; j++) {
            int ij = (g.w.regions[i].adj >> j) & 1u;
            int ji = (g.w.regions[j].adj >> i) & 1u;
            if (ij != ji) {
                printf("  seed %.0f: adjacency asymmetric %d<->%d\n", (double)seed, i, j);
                fails++;
            }
        }
    }

    /* 3. Each region is CONTIGUOUS: flood one of its tiles staying inside the
     *    region and you must recover every tile it claims. A disconnected
     *    region would let a fragment sit somewhere the region's gate implies
     *    is reachable when it is not. */
    for (i = 0; i < g.w.region_count; i++) {
        int first = -1, n, sx = 0, sy = 0;
        SDL_memset(sc.seen, 0, sizeof(sc.seen));
        /* Mark every tile NOT in region i as already seen, so the fill cannot
         * leave the region. */
        for (y = 0; y < WORLD_H; y++)
            for (x = 0; x < WORLD_W; x++)
                if (g.w.region[y][x] != i)
                    sc.seen[y * WORLD_W + x] = 1;
        n = flood_open(&g.w, sc.seen, sc.stack,
                       g.w.regions[i].seed_tile % WORLD_W,
                       g.w.regions[i].seed_tile / WORLD_W, &first, &sx, &sy);
        if (n != counted[i]) {
            printf("  seed %.0f: region %d NOT CONTIGUOUS (%d of %d tiles)\n",
                   (double)seed, i, n, counted[i]);
            fails++;
        }
    }

    /* 4. Graph is connected ignoring ability gates — otherwise some regions
     *    could never be entered no matter what the player collects. */
    regions_depth(&g.w, depth);
    for (i = 0; i < g.w.region_count; i++) {
        if (depth[i] < 0) {
            printf("  seed %.0f: region %d unreachable in the graph\n", (double)seed, i);
            fails++;
        }
    }

    /* 5. COVERAGE: every tile the player can walk to must belong to a region.
     *    This is the check that matters, and the one whose absence let a badly
     *    broken partition pass everything else — the other tests are all
     *    relative (counts agreeing with counts), so a partition covering 5 of
     *    1585 walkable tiles satisfied them trivially. Assert the absolute
     *    property, not just internal consistency. */
    if (assigned != open_tiles) {
        printf("  seed %.0f: COVERAGE %d of %d walkable tiles assigned to regions\n",
               (double)seed, assigned, open_tiles);
        fails++;
    }

    /* 6. Spawn must be enterable with no abilities at all. */
    if (terrain_requires[g.w.regions[g.w.spawn_region].terrain] != ABIL_NONE) {
        printf("  seed %.0f: SPAWN REGION IS GATED\n", (double)seed);
        fails++;
    }

    for (i = 0; i < TERRAIN_COUNT; i++)
        terrain_hist[i] = 0;
    for (i = 0; i < g.w.region_count; i++)
        terrain_hist[g.w.regions[i].terrain]++;

    if (verbose) {
        int maxd = 0;
        for (i = 0; i < g.w.region_count; i++)
            if (depth[i] > maxd) maxd = depth[i];
        printf("  seed %-10.0f spawn-comp %4d  assigned %4d  regions %2d  depth %d  "
               "open/water/ledge/dark %d/%d/%d/%d  %s\n",
               (double)seed, open_tiles, assigned, g.w.region_count, maxd,
               terrain_hist[TERRAIN_NORMAL], terrain_hist[TERRAIN_WATER],
               terrain_hist[TERRAIN_LEDGE], terrain_hist[TERRAIN_DARK],
               fails ? "FAIL" : "PASS");
    }
    return fails;
}

/* Walk the world at tile granularity under a given ability set, using the SAME
 * blocking rule the movement code uses, and report which regions were actually
 * entered.
 *
 * 4-connected on purpose: move_axis resolves each axis separately, so a
 * diagonal squeeze between two blocked orthogonal neighbours is not passable in
 * the real game either. */
static Uint32 walk_regions(const World *w, Uint8 abilities, int spawn_tile,
                           Uint8 *seen, int *queue)
{
    Uint32 touched = 0;
    int head = 0, tail = 0, i;

    for (i = 0; i < WORLD_W * WORLD_H; i++)
        seen[i] = 0;
    if (tile_blocked(w, abilities, spawn_tile % WORLD_W, spawn_tile / WORLD_W))
        return 0;

    seen[spawn_tile] = 1;
    queue[tail++] = spawn_tile;
    while (head < tail) {
        int idx = queue[head++];
        int x = idx % WORLD_W, y = idx / WORLD_W, d;
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };
        Uint8 reg = w->region[y][x];

        if (reg != REGION_NONE)
            touched |= 1u << reg;

        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d], nidx;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H)
                continue;
            if (tile_blocked(w, abilities, nx, ny))
                continue;
            nidx = ny * WORLD_W + nx;
            if (seen[nidx])
                continue;
            seen[nidx] = 1;
            queue[tail++] = nidx;
        }
    }
    return touched;
}

/* The model and the game must agree.
 *
 * Week 2's reachability guarantee is computed on the region graph, but what a
 * player can actually reach is decided by tile-level collision. If those two
 * ever disagree, the guarantee is worthless — the generator would certify a
 * world as completable that the movement code makes impossible. So compare them
 * directly at every ability tier. */
static int gating_selftest(Uint64 seed, int verbose)
{
    Game g;
    Rngs rngs;
    Scratch sc;
    static const Uint8 tiers[4] = {
        ABIL_NONE,
        ABIL_WADE,
        ABIL_WADE | ABIL_CLIMB,
        ABIL_WADE | ABIL_CLIMB | ABIL_KINDLE
    };
    int t, fails = 0, spawn_tile;
    int gated_seen = 0;

    rngs_init(&rngs, seed);
    (void)game_init(&g, &rngs);
    if (g.w.region_count < 2)
        return 0;

    spawn_tile = (int)(g.p.y / TILE) * WORLD_W + (int)(g.p.x / TILE);

    for (t = 0; t < 4; t++) {
        Uint32 walked = walk_regions(&g.w, tiers[t], spawn_tile, sc.seen, sc.queue);
        Uint32 modelled = regions_reachable(&g.w, tiers[t]);
        if (walked != modelled) {
            printf("  seed %.0f tier %d: walk 0x%X != graph 0x%X\n",
                   (double)seed, t, (unsigned)walked, (unsigned)modelled);
            fails++;
        }
        /* With no abilities, at least one region must be out of reach on some
         * seed, or gating is decorative and the test proves nothing. */
        if (t == 0 && walked != regions_reachable(&g.w, 0xFF))
            gated_seen = 1;
    }

    if (verbose)
        printf("  seed %-10.0f tiers agree  gating actually blocks: %-3s  %s\n",
               (double)seed, gated_seen ? "yes" : "no", fails ? "FAIL" : "PASS");
    return fails;
}

/* BFS over tiles standable under `abilities` — the same rule the movement code
 * enforces, so paths it produces are paths a player could actually walk. */
static void bfs_gated(const World *w, Uint8 abilities, int start, int *dist, int *queue)
{
    int head = 0, tail = 0, i;

    for (i = 0; i < WORLD_W * WORLD_H; i++)
        dist[i] = -1;
    if (tile_blocked(w, abilities, start % WORLD_W, start / WORLD_W))
        return;

    dist[start] = 0;
    queue[tail++] = start;
    while (head < tail) {
        int idx = queue[head++];
        int x = idx % WORLD_W, y = idx / WORLD_W, d;
        static const int dx[4] = { 1, -1, 0, 0 };
        static const int dy[4] = { 0, 0, 1, -1 };

        for (d = 0; d < 4; d++) {
            int nx = x + dx[d], ny = y + dy[d], nidx;
            if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H)
                continue;
            if (tile_blocked(w, abilities, nx, ny))
                continue;
            nidx = ny * WORLD_W + nx;
            if (dist[nidx] >= 0)
                continue;
            dist[nidx] = dist[idx] + 1;
            queue[tail++] = nidx;
        }
    }
}

/* One tick of an autopilot that plays the real game: restores anything in
 * reach, otherwise walks one step along a genuine shortest path to the nearest
 * reachable un-restored entity. Uses the real collision, the real ability
 * flags and the real restore call — nothing is teleported or shortcut, because
 * the point is to catch a divergence between the model and the game.
 *
 * Returns 1 if it restored something, 0 if it moved, -1 if nothing is
 * reachable (a dead end). */
static int autopilot_tick(Game *g, Scratch *sc)
{
    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };
    int here, target = -1, best = 1 << 30, i, d, next = -1;
    Input in;

    if (try_restore(g) >= 0)
        return 1;

    here = (int)(g->p.y / TILE) * WORLD_W + (int)(g->p.x / TILE);

    /* Nearest un-restored entity we can actually walk to right now. */
    bfs_gated(&g->w, g->p.abilities, here, sc->dist, sc->queue);
    for (i = 0; i < ENTITY_COUNT; i++) {
        int t = g->ents[i].tile;
        if (g->ents[i].restored || t < 0 || sc->dist[t] < 0)
            continue;
        if (sc->dist[t] < best) {
            best = sc->dist[t];
            target = i;
        }
    }
    if (target < 0)
        return -1;

    /* Re-root the field at the target so we can descend it from where we
     * stand — that gives the next step directly, with no path buffer and no
     * greedy steering to wedge in a concave corner. */
    bfs_gated(&g->w, g->p.abilities, g->ents[target].tile, sc->dist, sc->queue);
    if (sc->dist[here] < 0)
        return -1;

    for (d = 0; d < 4; d++) {
        int nx = (here % WORLD_W) + dx[d], ny = (here / WORLD_W) + dy[d], nidx;
        if (nx < 0 || ny < 0 || nx >= WORLD_W || ny >= WORLD_H)
            continue;
        nidx = ny * WORLD_W + nx;
        if (sc->dist[nidx] >= 0 && sc->dist[nidx] == sc->dist[here] - 1) {
            next = nidx;
            break;
        }
    }

    SDL_zero(in);
    {
        /* Aim at the next tile centre, or the entity itself on the last leg. */
        float wx, wy, ddx, ddy;
        if (next >= 0) {
            wx = (float)(next % WORLD_W) * TILE + TILE * 0.5f;
            wy = (float)(next / WORLD_W) * TILE + TILE * 0.5f;
        } else {
            wx = (float)(g->ents[target].tile % WORLD_W) * TILE + TILE * 0.5f;
            wy = (float)(g->ents[target].tile / WORLD_W) * TILE + TILE * 0.5f;
        }
        ddx = wx - g->p.x;
        ddy = wy - g->p.y;
        if (ddx > 0.6f) in.right = 1;
        else if (ddx < -0.6f) in.left = 1;
        if (ddy > 0.6f) in.down = 1;
        else if (ddy < -0.6f) in.up = 1;
    }
    sim_step(g, &in, TICK_DT);
    return 0;
}

/* Play the game to completion, headlessly.
 *
 * Week 2 proved worlds are solvable *in the model*. This proves it through the
 * real code: real collision, real ability flags, real proximity radius, real
 * restore. It walks to each reachable entity by BFS over walkable tiles, steps
 * the actual simulation along that path, and presses the actual interact
 * function. If the model and the game ever diverge, this stalls where a player
 * would. */
static int playthrough_selftest(Uint64 seed, int verbose)
{
    Game g;
    Rngs rngs;
    Scratch sc;
    int steps = 0, restores = 0;
    const char *reason = "complete";

    rngs_init(&rngs, seed);
    (void)game_init(&g, &rngs);
    if (g.w.region_count < 2)
        return 0;

    while (!game_complete(&g) && steps < 200000) {
        int r = autopilot_tick(&g, &sc);
        if (r < 0) {
            reason = "dead end: nothing reachable";
            break;
        }
        if (r == 1)
            restores++;
        else
            steps++;
    }
    if (!game_complete(&g) && steps >= 200000)
        reason = "autopilot made no progress (step cap)";


    if (verbose)
        printf("  seed %-10.0f restored %2d/%2d  frags %2d  souls %d  abilities %d/3  "
               "steps %6d  %s\n",
               (double)seed, restores, ENTITY_COUNT, g.frags_restored,
               g.souls_restored,
               ((g.p.abilities & ABIL_WADE) ? 1 : 0) +
                   ((g.p.abilities & ABIL_CLIMB) ? 1 : 0) +
                   ((g.p.abilities & ABIL_KINDLE) ? 1 : 0),
               steps, game_complete(&g) ? "COMPLETE" : reason);

    return game_complete(&g) ? 0 : 1;
}

/* The same autopilot, but in a real window with real rendering. Exists so the
 * restoration visual can be seen and captured deterministically — hunting for a
 * fragment by hand with synthetic keystrokes is luck, and the core hook of this
 * game is what the screen does when a memory comes back. */
static int autoplay_selftest(Uint64 seed, int ms)
{
    Game g;
    Rngs rngs;
    Scratch sc;
    SDL_Window *win;
    SDL_Surface *fb;
    Uint32 end;
    int restores = 0;

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
    printf("autoplay seed %.0f for %d ms\n", (double)seed, ms);

    end = SDL_GetTicks() + (Uint32)ms;
    while (SDL_GetTicks() < end && !game_complete(&g)) {
        SDL_Event ev;
        int r;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                end = 0;
        }
        r = autopilot_tick(&g, &sc);
        if (r == 1)
            restores++;
        else if (r < 0)
            break;

        fb = SDL_GetWindowSurface(win);
        if (!fb || fb->format->BytesPerPixel != 4)
            break;
        camera_follow(&g, fb->w, fb->h);
        render(fb, &g, 0);
        SDL_UpdateWindowSurface(win);
        SDL_Delay(4); /* faster than real time; this is a capture aid */
    }

    printf("restored %d  fragments %d/%d  souls %d/%d  stage %d\n",
           restores, g.frags_restored, FRAGMENT_COUNT, g.souls_restored,
           SOUL_COUNT, world_stage(&g));
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* Negative control for the invariant checker.
 *
 * Every seed passing on the first attempt is good news about the generator but
 * says nothing about the verifier: a checker hardwired to return "solvable"
 * would produce identical output. So build worlds that are unwinnable on
 * purpose and confirm it rejects them. A guarantee you have never seen fail is
 * not a guarantee. */
static int solvable_negative_test(Uint64 seed)
{
    Game g;
    Rngs rngs;
    int i, fails = 0, restored = 0;

    rngs_init(&rngs, seed);
    (void)game_init(&g, &rngs);
    if (g.w.region_count < 3) {
        printf("negative control: seed too degenerate, skipped\n");
        return 0;
    }

    /* Positive control: everything ungated must be solvable. */
    for (i = 0; i < g.w.region_count; i++)
        g.w.regions[i].terrain = TERRAIN_NORMAL;
    if (!world_solvable(&g.w, g.ents, &restored)) {
        printf("negative control: FAILED positive case - ungated world called unsolvable\n");
        fails++;
    }

    /* Case 1: seal every region behind Kindle and put the Kindle grant behind
     * the seal. Nothing outside spawn can ever be entered. */
    for (i = 0; i < g.w.region_count; i++)
        if (i != g.w.spawn_region)
            g.w.regions[i].terrain = TERRAIN_DARK;
    for (i = 0; i < ENTITY_COUNT; i++) {
        int r = (g.w.spawn_region + 1) % g.w.region_count;
        g.ents[i].region = (Uint8)r;
    }
    if (world_solvable(&g.w, g.ents, &restored)) {
        printf("negative control: FAILED - sealed world reported solvable\n");
        fails++;
    } else if (restored != 0) {
        printf("negative control: sealed world let %d entities through\n", restored);
        fails++;
    }

    /* Case 2: a solvable chain with exactly one entity stranded behind a gate
     * whose granting fragment is itself stranded. Catches a checker that only
     * looks at the first tier instead of iterating to a fixed point. */
    for (i = 0; i < g.w.region_count; i++)
        g.w.regions[i].terrain = TERRAIN_NORMAL;
    {
        int stranded = (g.w.spawn_region + 1) % g.w.region_count;
        g.w.regions[stranded].terrain = TERRAIN_WATER; /* needs Wade */
        for (i = 0; i < ENTITY_COUNT; i++) {
            g.ents[i].region = (Uint8)g.w.spawn_region;
            g.ents[i].grants = 0;
        }
        /* The only Wade grant sits behind the Wade gate. */
        g.ents[0].region = (Uint8)stranded;
        g.ents[0].grants = ABIL_WADE;
        if (world_solvable(&g.w, g.ents, &restored)) {
            printf("negative control: FAILED - self-locked gate reported solvable\n");
            fails++;
        } else if (restored != ENTITY_COUNT - 1) {
            printf("negative control: expected %d of %d restored, got %d\n",
                   ENTITY_COUNT - 1, ENTITY_COUNT, restored);
            fails++;
        }
    }

    printf("negative control (verifier rejects unwinnable worlds): %s\n",
           fails ? "FAIL" : "PASS");
    return fails;
}

/* The reachability invariant, checked independently of the generator that is
 * supposed to enforce it. Also confirms every entity actually sits on a
 * walkable tile in the region it claims — a fragment placed inside rock is
 * unreachable no matter what the graph says. */
static int reach_selftest(Uint64 seed, int verbose, int *relaxed)
{
    Game g;
    Rngs rngs;
    int i, fails = 0, restored = 0, souls = 0, grants = 0;

    rngs_init(&rngs, seed);
    (void)game_init(&g, &rngs);

    if (g.w.region_count < 2)
        return 0; /* degenerate world, covered by the region test */

    for (i = 0; i < ENTITY_COUNT; i++) {
        int t = g.ents[i].tile;
        int x, y;
        if (t < 0) {
            printf("  seed %.0f: entity %d unplaced\n", (double)seed, i);
            fails++;
            continue;
        }
        x = t % WORLD_W;
        y = t / WORLD_W;
        if (g.w.solid[y][x]) {
            printf("  seed %.0f: entity %d is inside rock\n", (double)seed, i);
            fails++;
        }
        if (g.w.region[y][x] != g.ents[i].region) {
            printf("  seed %.0f: entity %d region mismatch (%d vs tile's %d)\n",
                   (double)seed, i, g.ents[i].region, g.w.region[y][x]);
            fails++;
        }
        if (g.ents[i].is_soul)
            souls++;
        if (g.ents[i].grants)
            grants++;
    }

    if (souls != SOUL_COUNT) {
        printf("  seed %.0f: %d Found Souls, expected %d\n", (double)seed, souls, SOUL_COUNT);
        fails++;
    }
    if (grants != 3) {
        printf("  seed %.0f: %d ability grants, expected 3\n", (double)seed, grants);
        fails++;
    }

    /* The invariant itself. */
    if (!world_solvable(&g.w, g.ents, &restored)) {
        printf("  seed %.0f: UNWINNABLE - only %d of %d entities reachable\n",
               (double)seed, restored, ENTITY_COUNT);
        fails++;
    }

    if (g.gen_attempts < 0)
        (*relaxed)++;

    if (verbose)
        printf("  seed %-10.0f entities %2d/%2d reachable  attempts %3d%s  %s\n",
               (double)seed, restored, ENTITY_COUNT,
               g.gen_attempts < 0 ? -g.gen_attempts : g.gen_attempts,
               g.gen_attempts < 0 ? " (gating relaxed)" : "",
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
    a.tone = 1; /* the audio selftests measure this tone; the game is silent */
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
    if (arg_flag(argc, argv, "--sfx")) {
        /* Hammer the restore beat from this thread while the callback runs, so
         * the trigger path is exercised under real contention rather than in
         * isolation. Every 40 ms means beats overlap their own 300 ms tail. */
        int left = ms;
        printf("signal   : + restore beat every 40 ms (real-time safety probe)\n");
        while (left > 0) {
            SDL_AtomicAdd(&a.sfx_fire, 1);
            SDL_Delay(40);
            left -= 40;
        }
    } else {
        SDL_Delay((Uint32)ms);
    }
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
    int overlay = 0, grid = 0, dirty = 1, title_dirty = 1;
    int seed = arg_int(argc, argv, "--seed", 1);
#if WAYFARER_PERF
    Perf pf;
    int show_perf = arg_flag(argc, argv, "--perf");
#endif

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
        {
            int ams = arg_int(argc, argv, "--autoplay", 0);
            if (ams > 0)
                return autoplay_selftest((Uint64)arg_int(argc, argv, "--seed", 1), ams);
        }
        if (arg_flag(argc, argv, "--play-test")) {
            int n = arg_int(argc, argv, "--seeds", 20);
            int base = arg_int(argc, argv, "--seed", 1);
            int s, bad = 0;
            printf("=== headless playthrough to completion, %d seeds ===\n", n);
            for (s = 0; s < n; s++)
                bad += playthrough_selftest((Uint64)(base + s), 1);
            printf("\n%s (%d seeds could not be completed)\n", bad ? "FAIL" : "PASS", bad);
            return bad ? 1 : 0;
        }
        if (arg_flag(argc, argv, "--gating-test")) {
            int n = arg_int(argc, argv, "--seeds", 20);
            int base = arg_int(argc, argv, "--seed", 1);
            int s, bad = 0;
            printf("=== ability gating: walk vs graph, %d seeds ===\n", n);
            for (s = 0; s < n; s++)
                bad += gating_selftest((Uint64)(base + s), 1);
            printf("\n%s (%d failures across %d seeds)\n", bad ? "FAIL" : "PASS", bad, n);
            return bad ? 1 : 0;
        }
        if (arg_flag(argc, argv, "--reach-test")) {
            int n = arg_int(argc, argv, "--seeds", 20);
            int base = arg_int(argc, argv, "--seed", 1);
            int s, bad = 0, relaxed = 0;
            printf("=== reachability invariant, %d seeds ===\n", n);
            for (s = 0; s < n; s++)
                bad += reach_selftest((Uint64)(base + s), 1, &relaxed);
            printf("\n");
            bad += solvable_negative_test((Uint64)base);
            printf("gating relaxed on %d of %d seeds\n", relaxed, n);
            printf("%s (%d failures across %d seeds)\n", bad ? "FAIL" : "PASS", bad, n);
            return bad ? 1 : 0;
        }
        if (arg_flag(argc, argv, "--region-test")) {
            int n = arg_int(argc, argv, "--seeds", 20);
            int base = arg_int(argc, argv, "--seed", 1);
            int s, bad = 0;
            printf("=== region graph selftest, %d seeds ===\n", n);
            for (s = 0; s < n; s++)
                bad += region_selftest((Uint64)(base + s), 1);
            printf("\n%s (%d failures across %d seeds)\n", bad ? "FAIL" : "PASS", bad, n);
            return bad ? 1 : 0;
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

#if WAYFARER_PERF
    SDL_zero(pf);
#endif

    SDL_zero(audio);
    audio.noise = arg_flag(argc, argv, "--noise");
    audio.req_rate = AUDIO_RATE;
    rngs_init(&rngs, (Uint64)seed);
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
#if WAYFARER_PERF
        /* Four separate stopwatches, because the one number we had ("~56 fps")
         * conflated all of them and told us nothing about any. */
        Uint64 t_a, t_b, t_c;
        double ms_render = 0.0, ms_present = 0.0, ms_sleep = 0.0, ms_frame;
#endif

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = 0;
                    break;
                case SDLK_F1: /* region/terrain overlay, ignores fog */
                    overlay = !overlay;
                    break;
                case SDLK_F2: /* multi-seed grid view */
                    grid = !grid;
                    dirty = 1;
                    break;
                case SDLK_e:
                case SDLK_SPACE:
                    if (!grid && try_restore(&game) >= 0)
                        SDL_AtomicAdd(&audio.sfx_fire, 1);
                    break;
                case SDLK_r: /* regenerate with the next seed */
                    seed++;
                    rngs_init(&rngs, seed);
                    (void)game_init(&game, &rngs);
                    audio.rng = rngs.audio;
                    dirty = 1;
                    break;
                default:
                    break;
                }
                title_dirty = 1;
            }
        }

        input_poll(&in);

        now = SDL_GetPerformanceCounter();
        elapsed = (double)(now - prev) / perf;
        prev = now;
#if WAYFARER_PERF
        /* Sampled before the clamp below: the frame period is what it is, and a
         * stall we hid from the simulation is exactly the thing worth seeing. */
        ms_frame = elapsed * 1000.0;
#endif
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

#if WAYFARER_PERF
        t_a = SDL_GetPerformanceCounter();
#endif
        if (grid) {
            /* Regenerate only when dirty — 12 worlds per frame would crawl —
             * but always re-present, so a repaint after the surface is
             * invalidated does not leave a blank window. */
            if (dirty) {
                render_grid(fb, seed);
                dirty = 0;
            }
        } else {
            camera_follow(&game, fb->w, fb->h);
            render(fb, &game, overlay);
        }
#if WAYFARER_PERF
        t_b = SDL_GetPerformanceCounter();
#endif
        SDL_UpdateWindowSurface(win);
#if WAYFARER_PERF
        t_c = SDL_GetPerformanceCounter();
        ms_render  = (double)(t_b - t_a) / perf * 1000.0;
        ms_present = (double)(t_c - t_b) / perf * 1000.0;
#endif

        /* No bitmap font until Week 5 (design/systems/Save and UI.md), so debug
         * stats go in the title bar. Costs nothing and needs no glyph data. */
        if (title_dirty) {
            /* Confirmed 4-stage naming from design/Overview.md. */
            static const char *const stage_name[4] = {
                "Unexplored", "Partly Revealed", "Many Memories Restored", "Fully Restored"
            };
            char t[224];
            int n = SDL_snprintf(t, sizeof(t),
                         "Wayfarer  seed %d  fragments %d/%d  souls %d/%d  %s%s%s",
                         (int)seed, game.frags_restored, FRAGMENT_COUNT,
                         game.souls_restored, SOUL_COUNT, stage_name[world_stage(&game)],
                         overlay ? "  [F1 overlay]" : "",
                         grid ? "  [F2 grid]" : "");
#if WAYFARER_PERF
            /* Live readout while developing. The title bar is the only text
             * channel that exists before the Week 5 bitmap font. */
            if (show_perf && n > 0 && n < (int)sizeof(t))
                SDL_snprintf(t + n, sizeof(t) - (size_t)n,
                             "  |  rnd %.2fms  pre %.2fms  %.0fkpx",
                             ms_render, ms_present, (double)perf_px / 1000.0);
#else
            (void)n;
#endif
            SDL_SetWindowTitle(win, t);
            title_dirty = 0;
        }

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
                if (nap > 0) {
#if WAYFARER_PERF
                    Uint64 t_d = SDL_GetPerformanceCounter();
                    SDL_Delay(nap);
                    ms_sleep = (double)(SDL_GetPerformanceCounter() - t_d) / perf * 1000.0;
#else
                    SDL_Delay(nap);
#endif
                }
            }
        }

#if WAYFARER_PERF
        perf_frame(&pf, ms_render, ms_present, ms_sleep, ms_frame);
#endif

        frame++;
        /* Restoration eases in over time, so the stage can change with no input
         * at all. Refresh periodically rather than only on keypress. */
        if ((frame % 15) == 0)
            title_dirty = 1;
        if (limit && frame >= limit)
            running = 0;
    }

#if WAYFARER_PERF
    /* Report before tearing the window down, so fb->w/h are still the real
     * surface dimensions rather than remembered constants. */
    if (show_perf)
        perf_report(&pf, fb ? fb->w : WIN_W, fb ? fb->h : WIN_H);
#endif

    if (dev)
        SDL_CloseAudioDevice(dev);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
