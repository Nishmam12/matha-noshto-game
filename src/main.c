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

/* We rasterise at LOGICAL_W x LOGICAL_H and hard-double into the window. The
 * doubling is the point, not a shortcut: a 1 px highlight on a trunk is a
 * hairline at native 1080p and a visible 2 px band when drawn at 960x540 and
 * scaled, which is the difference between "procedural shapes" and "pixel art".
 * It also keeps TILE, PLAYER_SIZE and INTERACT_RADIUS meaning exactly what they
 * always meant, and makes window size a single constant.
 *
 * The scale is chosen at startup against the desktop size rather than baked in,
 * because a 1920x1080 window does not fit on a 1920x1080 desktop once the title
 * bar and taskbar are counted. --scale N overrides it. */
#define LOGICAL_W 960
#define LOGICAL_H 540
#define WIN_SCALE_MAX 3

/* Larger than the view so exploration means moving the camera, which is what
 * makes the reveal read as discovery.
 *
 * TILE went 16 -> 32 for the isometric pass: a 64x32 diamond gives procedural
 * props and buildings room to show their layering, where a 32x16 one did not.
 * Everything below that is expressed as a fraction of TILE was scaled with it,
 * so collision is unchanged in tile terms — player_blocked divides by TILE, so
 * doubling both the player box and the tile cancels exactly. Only absolute
 * pixel numbers move (speed_selftest's 110.00 becomes 220.00). */
#define TILE     32
#define WORLD_W  80
#define WORLD_H  45

#define PLAYER_SIZE  24
#define PLAYER_SPEED 220.0f /* world px/sec; 6.9 tiles/sec, as before */

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
#define SIGHT_MAX    0.50f
#define RESTORE_RATE 0.9f  /* region restoration units/sec once triggered */

#define INTERACT_RADIUS 44.0f /* world px; same fraction of a tile as before */

/* Where unrevealed land resolves to. This used to be (44,52,68) applied on top
 * of a luminance already scaled to 0.55 — a DARK blue-grey, and since walking
 * only ever reveals to SIGHT_MAX, roughly 95% of any given screen was that one
 * dead colour. The world read as a cave, or as night, and that is what made
 * traversal feel closed-in.
 *
 * Distance in the real world goes lighter, bluer and lower-contrast, never
 * darker — see design/Art Bible.md §2 and §4. So the tint is now a LIGHT cool
 * haze and the blend keeps a fixed fraction of the source's luminance contrast
 * rather than crushing it, which is also what keeps a four-shade canopy from
 * collapsing into one blob at low reveal. */
#define FOG_TINT_R 60.0f
#define FOG_TINT_G 70.0f
#define FOG_TINT_B 86.0f
#define FOG_KEEP   0.50f /* fraction of luminance contrast surviving at reveal 0 */

/* The F3 tuning overlay adjusts these live, so fog_lerp reads FOG_*_V rather
 * than the literals directly. In the shipping build FOG_*_V expands straight
 * back to the constants above and the generated code is byte-identical to
 * having no overlay at all — the same structural guarantee WAYFARER_PERF
 * gets, and the reason the +0-byte claim holds without re-deriving it. */
#if WAYFARER_SELFTEST
typedef struct { float tint_r, tint_g, tint_b, keep; } FogTune;
static FogTune fog_tune = { FOG_TINT_R, FOG_TINT_G, FOG_TINT_B, FOG_KEEP };
#define FOG_R_V fog_tune.tint_r
#define FOG_G_V fog_tune.tint_g
#define FOG_B_V fog_tune.tint_b
#define FOG_K_V fog_tune.keep
#else
#define FOG_R_V FOG_TINT_R
#define FOG_G_V FOG_TINT_G
#define FOG_B_V FOG_TINT_B
#define FOG_K_V FOG_KEEP
#endif

/* --- Isometric projection ------------------------------------------------
 *
 * A 2:1 diamond. Picking ISO_HW == TILE and ISO_HH == TILE/2 is the whole
 * trick: the world-to-screen transform then collapses to
 *
 *     sx = wx - wy + ISO_OX
 *     sy = (wx + wy) / 2 + ISO_OY
 *
 * — one subtract and one halve, no multiplies and no matrix, exact for integer
 * inputs. It also holds at any tile size, which is why the 16 -> 32 change cost
 * nothing here.
 *
 * sx = wx - wy is negative over half the world, so ISO_OX pushes the map right
 * by the world's height in pixels; ISO_OY leaves headroom above the north rim
 * for tiles raised by up to ELEV_MAX. See design/systems/Isometric Rendering.md. */
#define ISO_HW    TILE                                  /* 32, diamond half-width  */
#define ISO_HH    (TILE / 2)                            /* 16, diamond half-height */
#define DIA_W     (2 * ISO_HW)                          /* 64 */
#define DIA_H     (2 * ISO_HH)                          /* 32 */
#define ELEV_MAX  48                                    /* tallest raised tile, px */
#define ELEV_STEP 12    /* one terrace per ring of distance into a rock mass */
#define ELEV_WATER (-6) /* water sits below the ground plane */
#define ELEV_LEDGE 16   /* Climb terrain reads as a shelf before you can climb */

/* Face shading — the whole lighting model. One notional light from the upper
 * left, no normals and no dot products: the top face keeps its true colour and
 * each side face is scaled by a fixed percentage. Two constants do the entire
 * job of making flat colour read as volume. */
#define FACE_L 58   /* down-left face, per cent of true colour */
#define FACE_R 76   /* down-right face */
/* The same idea for roof slopes. Both visible roof faces are slopes rather than
 * a flat top, so neither takes the full 100% a tile's top face does — but the
 * split has to be wider than the terrain one to read across a surface this
 * large, and the lit side stays near full so roofs do not go muddy. */
#define ROOF_L 64   /* down-left roof slope, per cent */
#define ISO_OX    (WORLD_H * TILE)                      /* 1440 */
#define ISO_OY    ELEV_MAX
#define ISO_MAP_W ((WORLD_W + WORLD_H) * TILE)          /* 4000 */
#define ISO_MAP_H (((WORLD_W + WORLD_H) * TILE) / 2)    /* 2000 */
#define BAND_MAX  (WORLD_W + WORLD_H - 2)               /* largest tx+ty */

/* The void outside the landmass. The flat renderer never needed a screen clear
 * because its tile loop covered every pixel; diamonds only tile the plane where
 * the world exists, so beyond the rim there is nothing to draw and the clear
 * stops being optional. */
#define VOID_R 0x0a
#define VOID_G 0x0c
#define VOID_B 0x14

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

/* A building footprint. Walls are not drawn by anything here: the footprint
 * tiles are solid, world_heights gives them a wall height instead of a rock
 * height, and the existing tile rasteriser then draws their front faces — which
 * IS the wall, for free. Only the roof and the surface details need new code.
 *
 * `variant` packs nine independent part choices at 3 bits each. There are no
 * building "types": every house is a fresh combination, which is why 40 of them
 * on screen do not read as 40 copies of five prefabs. */
typedef struct {
    Uint8  x, y, w, h;  /* footprint, in tiles */
    Uint8  levels;      /* 1..3, wall height in storeys */
    Uint8  region;
    Uint16 pad;
    Uint32 variant;
} Building;

#define BUILDING_MAX 40
/* Houses cluster into villages rather than covering the island. BUILDING_TARGET
 * is what placement actually aims for; BUILDING_MAX stays the array bound. */
#define VILLAGE_SITES   3
#define VILLAGE_RADIUS  9   /* tiles from a site centre to its outermost plot */
#define VILLAGE_SPACING 22  /* minimum tiles between two site centres */
#define BUILDING_TARGET 15
#define STOREY_H     14   /* wall px per storey */
#define WALL_BASE    10   /* plinth under the first storey */

/* What a tile is made of. `solid` stays the single collision truth — both ROCK
 * and OCEAN are solid and neither is walkable — and this only records WHICH kind
 * of solid, so water can be drawn as water instead of as brown rock. Read by
 * rendering and by world_heights; never by collision. */
enum {
    SURF_LAND = 0, /* open ground */
    SURF_ROCK,     /* cliff or outcrop: solid, raised */
    SURF_OCEAN     /* sea: solid, sunken */
};

typedef struct {
    Uint8  solid[WORLD_H][WORLD_W];
    /* Written by world_gen alongside `solid`, which is derived from it. Read
     * only by rendering and by world_heights — see the SURF_* note above. */
    Uint8  surf[WORLD_H][WORLD_W];
    float  reveal[WORLD_H][WORLD_W]; /* 0 = fogged and colourless, 1 = restored */
    Uint8  region[WORLD_H][WORLD_W]; /* REGION_NONE where solid or unreachable */
    /* DERIVED, RENDER-ONLY. Draw height in screen px, computed once at the end
     * of game_init and read only by render. Collision still consults `solid`
     * and regions[].terrain and nothing else, so no movement, reachability,
     * gating or playthrough result can observe this. Keep it that way: the
     * moment collision reads it, the 50-seed completability proof has to be
     * re-argued rather than merely re-run. */
    Sint8  height[WORLD_H][WORLD_W];
    /* Building index per tile, +1 so 0 means "no building". Render-only, like
     * height: collision sees only that these tiles are solid. */
    Uint8  bld_at[WORLD_H][WORLD_W];
    Building bld[BUILDING_MAX];
    int    bld_count;
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
    Uint64 seed;            /* world seed, kept for the decoration hash */
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

/* --- island generation ----------------------------------------------------
 *
 * This was a cellular-automaton cave, and because world_heights derives terrace
 * height from distance into a rock mass, the LANDFORM was cave noise. On screen
 * that read as random brown lumps rather than as a place, and there was no
 * coastline anywhere — design/Art Bible.md §6 is explicit that water has to be a
 * body with a shore, not a scattering of blue tiles.
 *
 * An island instead. A radial term makes the middle high and the rim low; two
 * octaves of value noise push the coastline in and out so it is ragged rather
 * than elliptical; a third, higher-frequency octave raises rock outcrops inland
 * so the interior still has structure to walk around. The radial term uses
 * NORMALISED coordinates, so the island is stretched to the world's 80x45
 * proportion — which lands as a diamond in the isometric projection, the shape
 * an isometric island wants to be.
 *
 * Crucially this writes `solid` and nothing else that collision can observe.
 * Ocean and rock are both solid; which one a tile is lives in `surf` and is
 * render-only. So the region partition, building placement and the 50-seed
 * completability proof all still see exactly what they saw before, and could be
 * re-RUN rather than re-argued. */
#define LAND_LAT_W    13   /* coarse height lattice; ~6.7 tiles per cell */
#define LAND_LAT_H     9
#define LAND_LAT2_W   25   /* fine height octave */
#define LAND_LAT2_H   17
/* Outcrop lattice. Deliberately LOW frequency: at 33x19 the threshold produced
 * dozens of small blobs, and because world_heights raises a one-ring outcrop to
 * exactly one terrace, every one of them came out as an identical flat-topped
 * 12 px platform — on screen, a lawn scattered with concrete slabs. Fewer and
 * larger outcrops give the chamfer room to produce several terraces within one
 * mass, which is what makes rock read as rock. */
#define LAND_ROCK_W   17
#define LAND_ROCK_H   11
#define LAND_SEA      0.24f /* height below this is ocean; lower = bigger island */
#define LAND_ROUGH    0.55f /* how far noise moves the coastline in and out */
/* Raised from 0.68: at that threshold a single outcrop could cover 40% of the
 * screen, and because rock is the darkest large surface in the palette the
 * result read as a quarry with a lawn around it rather than as a hillside. */
#define LAND_ROCK_T   0.74f /* outcrop threshold */

static void land_lattice(Rng *rng, float *lat, int n)
{
    int i;

    for (i = 0; i < n; i++)
        lat[i] = rng_float(rng);
}

/* Bilinear value noise over a random lattice, smoothstepped so cell boundaries
 * do not crease into visible straight lines across the coast. */
static float land_noise(const float *lat, int lw, int lh, float fx, float fy)
{
    int x0, y0, x1, y1;
    float tx, ty, a, b;

    fx *= (float)(lw - 1);
    fy *= (float)(lh - 1);
    x0 = (int)fx;
    y0 = (int)fy;
    if (x0 > lw - 2) x0 = lw - 2;
    if (y0 > lh - 2) y0 = lh - 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    x1 = x0 + 1;
    y1 = y0 + 1;

    tx = fx - (float)x0;
    ty = fy - (float)y0;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);

    a = lat[y0 * lw + x0] + (lat[y0 * lw + x1] - lat[y0 * lw + x0]) * tx;
    b = lat[y1 * lw + x0] + (lat[y1 * lw + x1] - lat[y1 * lw + x0]) * tx;
    return a + (b - a) * ty;
}

static void world_gen(World *w, Rng *rng)
{
    /* Deliberately locals, not statics. On PE/COFF, -fdata-sections emits
     * zero-initialised statics as .data$name COMDATs, which are stored in the
     * file — 40 KB of literal zeros measured in the executable before this was
     * moved to the stack. Keep generation scratch off the static path. */
    float lat0[LAND_LAT_W * LAND_LAT_H];
    float lat1[LAND_LAT2_W * LAND_LAT2_H];
    float rock[LAND_ROCK_W * LAND_ROCK_H];
    int x, y;

    land_lattice(rng, lat0, LAND_LAT_W * LAND_LAT_H);
    land_lattice(rng, lat1, LAND_LAT2_W * LAND_LAT2_H);
    land_lattice(rng, rock, LAND_ROCK_W * LAND_ROCK_H);

    for (y = 0; y < WORLD_H; y++) {
        for (x = 0; x < WORLD_W; x++) {
            float fx = (float)x / (float)(WORLD_W - 1);
            float fy = (float)y / (float)(WORLD_H - 1);
            float ex = (fx - 0.5f) * 2.0f;
            float ey = (fy - 0.5f) * 2.0f;
            float d  = SDL_sqrtf(ex * ex + ey * ey);
            /* Two octaves, recentred on zero so they push the coast both ways
             * rather than only outward. */
            float n  = (0.62f * land_noise(lat0, LAND_LAT_W, LAND_LAT_H, fx, fy)
                      + 0.38f * land_noise(lat1, LAND_LAT2_W, LAND_LAT2_H, fx, fy))
                     - 0.5f;
            float h  = (1.0f - d) + n * LAND_ROUGH;
            Uint8 s;

            /* The rim is always water, so nothing can walk off the world and
             * solid_at's out-of-bounds wall never has to be seen. */
            if (x == 0 || y == 0 || x == WORLD_W - 1 || y == WORLD_H - 1)
                s = SURF_OCEAN;
            else if (h < LAND_SEA)
                s = SURF_OCEAN;
            else if (land_noise(rock, LAND_ROCK_W, LAND_ROCK_H, fx, fy) > LAND_ROCK_T)
                s = SURF_ROCK;
            else
                s = SURF_LAND;

            w->surf[y][x] = s;
            w->solid[y][x] = (Uint8)(s != SURF_LAND);
            w->reveal[y][x] = 0.0f;
        }
    }
}

/* Stamp buildings into the open ground.
 *
 * Deliberately placed BEFORE the flood fill and the reachability verifier, not
 * after. Buildings are solid, so they genuinely change what is walkable — and
 * running them through the existing generate-then-verify loop means a layout
 * that walls off something the player needs is rejected and regenerated, using
 * the reachability guarantee as the safety net rather than working around it.
 *
 * A plot needs open ground plus a one-tile gap from anything else already
 * solid, which keeps a house from fusing into a cliff and guarantees it is
 * approachable from at least one side. */
static void place_buildings(World *w, Rng *rng)
{
    int sx[VILLAGE_SITES], sy[VILLAGE_SITES];
    int sites = 0, tries;

    w->bld_count = 0;
    SDL_memset(w->bld_at, 0, sizeof(w->bld_at));

    /* Pick village sites first. Scattering houses uniformly over the whole
     * island put a building on almost every open plot the moment the landmass
     * grew — on screen, a suburb of identical boxes with no ground between
     * them. Clustering is what makes the same houses read as a village with
     * countryside around it, and it costs one rejection test.
     *
     * Sites are kept VILLAGE_SPACING apart so two clusters never merge back
     * into the uniform scatter this replaced. */
    for (tries = 0; tries < 400 && sites < VILLAGE_SITES; tries++) {
        int cx = 6 + (int)rng_below(rng, WORLD_W - 12);
        int cy = 5 + (int)rng_below(rng, WORLD_H - 10);
        int i, ok = 1;

        if (solid_at(w, cx, cy))
            continue;
        for (i = 0; i < sites; i++) {
            int dx = cx - sx[i], dy = cy - sy[i];
            if (dx * dx + dy * dy < VILLAGE_SPACING * VILLAGE_SPACING)
                ok = 0;
        }
        if (!ok)
            continue;
        sx[sites] = cx;
        sy[sites] = cy;
        sites++;
    }
    if (sites == 0)
        return; /* no open ground at all; the verifier will reject this world */

    /* Footprints are 2..3 tiles, not 2..4. A 4-tile house is 256 px wide on
     * screen against a ~20 px tree, which broke the scale contract in
     * design/Art Bible.md §4 badly enough that the world read as warehouses
     * with shrubs. */
    for (tries = 0; tries < 5000 && w->bld_count < BUILDING_TARGET; tries++) {
        int s  = (int)rng_below(rng, (Uint32)sites);
        int bw = 2 + (int)rng_below(rng, 2);          /* 2..3 tiles */
        int bh = 2 + (int)rng_below(rng, 2);
        /* Two draws summed, so plots bunch toward the site centre and thin out
         * at the edge rather than filling a hard-edged disc. */
        int bx = sx[s] - VILLAGE_RADIUS
               + (int)rng_below(rng, VILLAGE_RADIUS + 1)
               + (int)rng_below(rng, VILLAGE_RADIUS + 1);
        int by = sy[s] - VILLAGE_RADIUS
               + (int)rng_below(rng, VILLAGE_RADIUS + 1)
               + (int)rng_below(rng, VILLAGE_RADIUS + 1);
        int x, y, ok = 1;

        if (bx < 2 || by < 2 || bx + bw > WORLD_W - 2 || by + bh > WORLD_H - 2)
            continue;

        /* The footprint and a TWO-tile skirt must all be open ground. At one
         * tile, neighbouring houses ended up with a single tile between them
         * and the cluster read as one continuous terrace of roofs; two tiles
         * leaves a lane wide enough to walk down and to see ground through.
         * It also still guarantees the plot is approachable from every side. */
        for (y = by - 2; y <= by + bh + 1 && ok; y++)
            for (x = bx - 2; x <= bx + bw + 1 && ok; x++)
                if (solid_at(w, x, y))
                    ok = 0;
        if (!ok)
            continue;

        for (y = by; y < by + bh; y++)
            for (x = bx; x < bx + bw; x++) {
                w->solid[y][x] = 1;
                w->bld_at[y][x] = (Uint8)(w->bld_count + 1);
            }
        {
            Building *b = &w->bld[w->bld_count];
            b->x = (Uint8)bx; b->y = (Uint8)by;
            b->w = (Uint8)bw; b->h = (Uint8)bh;
            /* Bigger footprints carry more storeys, so a village silhouette has
             * a few halls standing over the cottages instead of being uniform. */
            b->levels = (Uint8)(1 + (int)rng_below(rng, (bw * bh >= 9) ? 3 : 2));
            b->region = REGION_NONE;
            b->pad = 0;
            b->variant = rng_next(rng);
            w->bld_count++;
        }
    }
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

/* Draw height per tile, derived from `solid` plus the region's terrain. Rock
 * rises in terraces toward the interior of a mass, via a two-pass chamfer
 * distance transform, so a cliff edge gets a rounded rim rather than a slab
 * wall and an isolated pillar stays short. Water sinks; Climb terrain is
 * raised, which makes that ability gate legible before you have the ability.
 *
 * Render-only, and deliberately the last thing game_init does: it needs
 * regions[].terrain, and nothing downstream may depend on it.
 *
 * `dist` is a function local, never a static — see the .data trap in
 * design/Toolchain Setup.md. */
static void world_heights(World *w)
{
    Uint8 dist[WORLD_H][WORLD_W];
    int x, y, d;

    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            dist[y][x] = (Uint8)(w->solid[y][x] ? 200 : 0);

    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            if (!dist[y][x]) continue;
            d = dist[y][x];
            if (y > 0 && dist[y - 1][x] + 1 < d) d = dist[y - 1][x] + 1;
            if (x > 0 && dist[y][x - 1] + 1 < d) d = dist[y][x - 1] + 1;
            dist[y][x] = (Uint8)d;
        }
    for (y = WORLD_H - 1; y >= 0; y--)
        for (x = WORLD_W - 1; x >= 0; x--) {
            if (!dist[y][x]) continue;
            d = dist[y][x];
            if (y < WORLD_H - 1 && dist[y + 1][x] + 1 < d) d = dist[y + 1][x] + 1;
            if (x < WORLD_W - 1 && dist[y][x + 1] + 1 < d) d = dist[y][x + 1] + 1;
            dist[y][x] = (Uint8)d;
        }

    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++) {
            int h;
            if (w->bld_at[y][x]) {
                /* Walls, not rock. The tile rasteriser then draws this tile's
                 * front faces at wall height, which IS the wall — no separate
                 * wall-drawing code exists anywhere. */
                h = WALL_BASE + w->bld[w->bld_at[y][x] - 1].levels * STOREY_H;
            } else if (w->surf[y][x] == SURF_OCEAN) {
                /* Sea floor, stepping down away from the shore. Without this the
                 * chamfer below would read open water as the deep interior of a
                 * rock mass and raise it to ELEV_MAX — an ocean drawn as a
                 * 48 px plateau. It steps rather than sitting flat so the shelf
                 * reads as depth. */
                int step = dist[y][x] - 1;
                if (step < 0) step = 0;
                if (step > 3) step = 3;
                h = ELEV_WATER - step * 4;
            } else if (w->solid[y][x]) {
                h = dist[y][x] * ELEV_STEP;
                if (h > ELEV_MAX) h = ELEV_MAX;
                /* Break the terrace top. Without this every tile of a one-ring
                 * outcrop sits at exactly ELEV_STEP and the mass reads as a
                 * poured slab; the jitter turns the top into small facets and
                 * the rasteriser draws the resulting 1-8 px steps for free.
                 *
                 * A positional hash, NOT an RNG stream — decoration must never
                 * perturb terrain or entity placement (see the note on the
                 * decoration hash). It is also render-only: `height` is not a
                 * collision input, so no seed's solvability can change. */
                {
                    /* Hashed on the 2x2 block, not the tile, and by +-2 rather
                     * than +-4. Per-tile jitter at full amplitude turned every
                     * outcrop top into a visible checkerboard — adjacent tiles
                     * disagreed every time, so the rasteriser drew a step
                     * between all of them. Sharing a value across a block gives
                     * facets a few tiles wide, which is what rock looks like. */
                    Uint32 j = (Uint32)(x >> 1) * 73856093u
                             ^ (Uint32)(y >> 1) * 19349663u;
                    j ^= j >> 13;
                    h += (int)(j % 5u) - 2;
                    if (h < ELEV_STEP / 2) h = ELEV_STEP / 2;
                }
            } else {
                Uint8 rg = w->region[y][x];
                int t = (rg == REGION_NONE) ? TERRAIN_NORMAL
                                            : w->regions[rg].terrain;
                h = (t == TERRAIN_WATER) ? ELEV_WATER
                  : (t == TERRAIN_LEDGE) ? ELEV_LEDGE : 0;
            }
            w->height[y][x] = (Sint8)h;
        }
}

/* Out-of-world reads as ground level, so border tiles draw their full front
 * face and the void clear covers everything past the rim. */
static int height_at(const World *w, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H)
        return 0;
    return w->height[ty][tx];
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
    /* Before the flood fill, so buildings are part of what "walkable" means and
     * the reachability verifier gets to reject a layout they wall off. */
    place_buildings(&g->w, &rngs->terrain);
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

    /* Last, and after terrain assignment: purely derived, purely for drawing.
     * The pathological-seed early return above leaves height all zeros courtesy
     * of the SDL_zero at the top, which draws flat and is correct. */
    world_heights(&g->w);

    g->seed = rngs->seed;
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

static void perf_report(const Perf *p, int w, int h, int scale)
{
    double n     = (double)(p->frames > 0 ? p->frames : 1);
    double frame = p->frame_sum / n;
    double px    = p->px_sum / n;

    printf("=== render perf: %d frames at %dx%d logical, x%d -> %dx%d ===\n",
           p->frames, w, h, scale, w * scale, h * scale);
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

#if WAYFARER_SELFTEST
/* ---- Bitmap font ---------------------------------------------------------
 *
 * 5x7, hand-rolled and bit-packed rather than a sprite (decision 3, Handover
 * §6: no SDL_ttf, ever). Covers uppercase, digits and the punctuation a
 * restoration line or a tuning HUD is likely to need — see design/phases/
 * Phase 03 - Legibility Tools.md, task 1.
 *
 * Gated behind WAYFARER_SELFTEST for now, the same way WAYFARER_PERF is
 * (Handover §6 decision 8): nothing in the shipping build calls draw_text
 * yet — wiring it into a real restoration line or HUD is Phase 09/11's job,
 * not this phase's. This phase only has to prove the tool itself works, via
 * --font-test and the tuning overlay built on top of it next. Un-gating is a
 * one-line change once a real caller exists; leaving it gated until then
 * keeps "the shipping build carries none of this" true by construction
 * rather than by remembering not to call it.
 *
 * FONT_5X7 is FLAT and indexed with an explicit stride rather than declared
 * as [glyph][row], on purpose: font_selftest's negative control corrupts
 * that stride to prove its pixel-count checker actually rejects a misread
 * glyph, not just one nobody looked at closely. */
#define FONT_W      5
#define FONT_H      7
#define FONT_SCALE  2      /* logical px per font px; legible at --scale 1 */
#define FONT_FIRST  0x20   /* space */
#define FONT_LAST   0x5F   /* underscore; covers digits, A-Z, punctuation */
#define FONT_GLYPHS (FONT_LAST - FONT_FIRST + 1)
#define FONT_STRIDE FONT_H /* rows per glyph in FONT_5X7 — see note above */

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
    /* 0x2F unused */ 0,0,0,0,0,0,0,
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
    /* 0x5B-0x5F unused */ 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0,
};

#undef GR

/* idx*stride+row is the deliberately-exposed seam: every caller outside
 * font_selftest passes FONT_STRIDE; the test corrupts it to prove the
 * pixel-count checker actually notices. */
static void draw_glyph(SDL_Surface *fb, int x, int y, int ch, Uint32 colour,
                       int stride)
{
    int idx = ch - FONT_FIRST;
    int row, col;

    if (idx < 0 || idx >= FONT_GLYPHS)
        return;
    for (row = 0; row < FONT_H; row++) {
        Uint8 bits = FONT_5X7[idx * stride + row];
        for (col = 0; col < FONT_W; col++)
            if (bits & (1u << (FONT_W - 1 - col)))
                fill_rect(fb, x + col * FONT_SCALE, y + row * FONT_SCALE,
                          FONT_SCALE, FONT_SCALE, colour);
    }
}

static void draw_text(SDL_Surface *fb, int x, int y, const char *str, Uint32 colour)
{
    int cx = x;
    for (; *str; str++) {
        if (*str == '\n') { cx = x; y += (FONT_H + 1) * FONT_SCALE; continue; }
        draw_glyph(fb, cx, y, (unsigned char)*str, colour, FONT_STRIDE);
        cx += (FONT_W + 1) * FONT_SCALE;
    }
}

/* One font px down-right in solid black, then the real colour on top —
 * legible over arbitrary terrain without needing a backing panel. */
static void draw_text_shadow(SDL_Surface *fb, int x, int y, const char *str, Uint32 colour)
{
    draw_text(fb, x + FONT_SCALE, y + FONT_SCALE, str, 0);
    draw_text(fb, x, y, str, colour);
}
#endif /* WAYFARER_SELFTEST */

/* Integer nearest-neighbour upscale, logical -> window, centred with the
 * leftover margin cleared. Nearest-neighbour and integer-only on purpose:
 * anything smoother would undo the hard pixel edges this exists to produce.
 *
 * Each source row is expanded once and the result memcpy'd down to the other
 * s-1 rows, so scaling vertically costs a linear copy rather than another pass
 * of per-pixel work. Centring matters because a fullscreen window is rarely an
 * exact multiple of the logical size — 960x540 at x2 leaves a 64x36 border on
 * a 2048x1152 desktop, and that border holds whatever was in the surface
 * before unless it is cleared. */
static void blit_scale(const SDL_Surface *src, SDL_Surface *dst, int s)
{
    int y, x, k, ox, oy, dw, dh;

    if (s < 1)
        s = 1;
    dw = src->w * s;
    dh = src->h * s;
    if (dst->w < dw || dst->h < dh)
        return;
    ox = (dst->w - dw) / 2;
    oy = (dst->h - dh) / 2;

    /* Margins only, not the whole surface: at x2 on a 2048x1152 desktop this is
     * ~285k px rather than 2.36M. */
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

/* Per-tile decoration hash: a SplitMix64 finaliser over (seed, tx, ty).
 *
 * Deliberately stateless and deliberately NOT drawn from the terrain or entity
 * RNG streams. Decoration therefore cannot perturb a single fragment placement,
 * and every existing seed-based test result stays valid by construction rather
 * than by tracing the call graph to prove it. It is also stable regardless of
 * draw order or culling, so nothing crawls or flickers as the camera moves.
 *
 * One call per tile per frame; each decoration parameter is a different bit
 * field of the single result. The budget, fixed once so it is never re-derived:
 *
 *   0-11  detail mark positions      18-21  trunk/canopy size jitter
 *  12-14  canopy palette             22-26  lean and shape jitter
 *  16-17  trunk palette              27-31  sway phase
 *   8-12  prop presence roll (reused; presence is decided before shape)
 */
static Uint32 tile_hash(Uint64 seed, int tx, int ty)
{
    Uint64 h = seed * 0x9E3779B97F4A7C15ULL
             + (Uint64)(Uint32)tx * 0xBF58476D1CE4E5B9ULL
             + (Uint64)(Uint32)ty * 0x94D049BB133111EBULL;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27; h *= 0x94D049BB133111EBULL;
    return (Uint32)(h ^ (h >> 31));
}

/* World pixels -> isometric map pixels. The camera is NOT applied here; callers
 * subtract cam_x/cam_y, exactly as the flat renderer did. */
static void world_to_iso(float wx, float wy, int *sx, int *sy)
{
    *sx = (int)(wx - wy) + ISO_OX;
    *sy = (int)((wx + wy) * 0.5f) + ISO_OY;
}

/* Vertical run. x is clipped by the caller (once per tile, not once per column),
 * so only y is tested here. Striding by pitch sounds cache-hostile, but a whole
 * diamond is 64 adjacent columns of at most ~80 px — a working set small enough
 * to stay resident while consecutive columns re-touch the same lines. */
static void vspan(SDL_Surface *s, int x, int y, int n, Uint32 c)
{
    Uint8 *p;

    if (n <= 0)
        return;
    if (y < 0) { n += y; y = 0; }
    if (y + n > s->h) n = s->h - y;
    if (n <= 0)
        return;

    PERF_COUNT(n);
    p = (Uint8 *)s->pixels + y * s->pitch + x * 4;
    while (n--) {
        *(Uint32 *)p = c;
        p += s->pitch;
    }
}

/* One ground tile: the raised diamond top face plus its two visible side faces,
 * drawn as a single contiguous vertical run per screen column, split into
 * (top colour, side colour).
 *
 * For column i of DIA_W, with a = |i - ISO_HW|, the top face starts at a>>1 and
 * runs DIA_H - a pixels. That is the exact preimage of the tile under the
 * inverse projection rather than a slope walk, which is what makes the tiling
 * provably gap-free: for the east neighbour a_A + a_B = DIA_W/2 exactly, and
 * requiring A's bottom to equal B's top reduces to an identity that holds for
 * both parities of a. Same for the south neighbour. Total per diamond works out
 * at DIA_W * DIA_H / 2 px, which is what a diamond must be.
 *
 * Elevation then costs nothing extra: raising the tile by h leaves a hole
 * exactly h tall starting exactly where the top face ended, so the side face is
 * the same column immediately below. There is no second edge computation that
 * could disagree with the first, which is the usual source of iso seams.
 *
 * hl/hr are the drops to the front-left and front-right neighbours, already
 * clamped at 0 by the caller so hidden faces are never drawn. */
static void iso_tile(SDL_Surface *fb, int ax, int ay, int h, int hl, int hr,
                     Uint32 top, Uint32 top2, Uint32 grain, Uint32 cl, Uint32 cr)
{
    int i, i0 = 0, i1 = DIA_W;
    int x0 = ax - ISO_HW;

    if (x0 < 0)
        i0 = -x0;
    if (x0 + i1 > fb->w)
        i1 = fb->w - x0;
    if (i0 >= i1)
        return;

    for (i = i0; i < i1; i++) {
        int a = i - ISO_HW;
        int y, n, left;
        if (a < 0)
            a = -a;
        y = ay - h + (a >> 1);
        n = DIA_H - a;
        left = (i < ISO_HW);
        /* Grain, as a two-tone dither over the top face. Indexed by (i ^ a>>1)
         * rather than by i so the pattern follows the tile's own diagonal axes
         * and reads as ground in perspective rather than screen-aligned noise.
         *
         * Split into two runs because one bit per column produced visible
         * vertical stripes: the whole 32 px column took a single shade. Two
         * independently-indexed halves break the streak for one extra vspan.
         * Callers wanting a flat tile pass top2 == top and grain == 0. */
        {
            int half = n >> 1;
            vspan(fb, x0 + i, y, half,
                  ((grain >> ((i ^ (a >> 1)) & 31)) & 1) ? top2 : top);
            vspan(fb, x0 + i, y + half, n - half,
                  ((grain >> ((i * 5 + a + 7) & 31)) & 1) ? top2 : top);
        }
        vspan(fb, x0 + i, y + n, left ? hl : hr, left ? cl : cr);
    }
}

/* The core visual hook, and the one piece of this slice that is not a
 * placeholder: colour = lerp(drained grey, true colour, restoration%).
 * See design/systems/Fog and Reveal.md. */
static Uint32 fog_lerp(SDL_Surface *s, int r, int gr, int b, float reveal)
{
    /* Luminance, then pulled toward the haze while keeping FOG_KEEP of its own
     * contrast: unrestored land keeps its shape and its relative light-to-dark
     * ordering, but loses its colour. That ordering is the load-bearing part —
     * a canopy is four shades and a cliff has two faces, so a blend that
     * flattened luminance would turn every prop into a silhouette. */
    float lum = 0.299f * r + 0.587f * gr + 0.114f * b;
    float fr = FOG_R_V + (lum - FOG_R_V) * FOG_K_V;
    float fg = FOG_G_V + (lum - FOG_G_V) * FOG_K_V;
    float fb = FOG_B_V + (lum - FOG_B_V) * FOG_K_V;

    if (reveal < 0.0f) reveal = 0.0f;
    if (reveal > 1.0f) reveal = 1.0f;

    return SDL_MapRGB(s->format,
                      (Uint8)(fr + ((float)r - fr) * reveal),
                      (Uint8)(fg + ((float)gr - fg) * reveal),
                      (Uint8)(fb + ((float)b - fb) * reveal));
}

/* A diamond outline lying flat on the ground plane, in the same 2:1 ratio as
 * the tiles. An axis-aligned box read as a rectangle floating in the air; a
 * diamond reads as painted on the floor, which is what a reach indicator is. */
static void iso_ring(SDL_Surface *fb, int cx, int cy, int rx, Uint32 c)
{
    int i;

    for (i = 0; i <= rx; i++) {
        int dy = i / 2; /* the 2:1 slope, matching ISO_HH/ISO_HW exactly */
        fill_rect(fb, cx - rx + i, cy - dy, 1, 2, c);
        fill_rect(fb, cx + rx - i, cy - dy, 1, 2, c);
        fill_rect(fb, cx - rx + i, cy + dy, 1, 2, c);
        fill_rect(fb, cx + rx - i, cy + dy, 1, 2, c);
    }
}

/* Scatter a few marks across a tile's top face — grass tufts, pebbles, cracks,
 * strata. This is what stops a field of one terrain reading as a single flat
 * colour, and it is the cheapest detail in the renderer by a wide margin.
 *
 * The trick that removes all clipping: a mark is positioned by its offset
 * WITHIN the tile in world pixels and then projected, so it lands inside the
 * diamond by construction and needs no shape test. Positions come from the tile
 * hash, so a given tile's marks are identical every frame — deterministic, not
 * per-frame noise. */
static void tile_detail(SDL_Surface *fb, int ax, int ay, int h, Uint32 hash,
                        Uint32 c, int nmark, int mw)
{
    int k;

    for (k = 0; k < nmark; k++) {
        /* Inset to 4..27 of the tile's 32 px so a mark never straddles the
         * diamond's very edge, where it would read as a notch in the outline. */
        int ox = 4 + (int)((hash >> (k * 4)) & 15) + (int)((hash >> (k * 4 + 2)) & 7);
        int oy = 4 + (int)((hash >> (k * 4 + 6)) & 15) + (int)((hash >> (k * 4 + 1)) & 7);
        fill_rect(fb, ax + ox - oy, ay + ((ox + oy) >> 1) - h, mw, 1, c);
    }
}

/* --- house parts ---------------------------------------------------------
 *
 * There are no building "types". Nine independent parts, each with 4 or 5
 * variations, are picked from separate bit fields of one `variant` word:
 *
 *   wall material 5 x wall trim 4 x roof shape 5 x roof material 4 x door 4
 *     x windows 5 x chimney 4 x attachment 5 x sign 4  = 640,000 combinations
 *
 * before footprint size (2..4 x 2..4) and storey count multiply it further.
 * That is why forty houses on screen do not read as forty copies of five
 * prefabs, and it costs nine small draw routines rather than nine drawings. */
#define BV_WALL(v)   (((v) >> 0)  % 5)
#define BV_TRIM(v)   (((v) >> 3)  & 3)
#define BV_RSHAPE(v) (((v) >> 5)  % 5)
#define BV_RMAT(v)   (((v) >> 8)  & 3)
#define BV_DOOR(v)   (((v) >> 10) & 3)
#define BV_WIN(v)    (((v) >> 12) % 5)
#define BV_CHIM(v)   (((v) >> 15) & 3)
#define BV_ATT(v)    (((v) >> 17) % 5)
#define BV_SIGN(v)   (((v) >> 20) & 3)

/* Wall materials: plaster, timber-frame, stone, brick, log. */
static const Uint8 wall_pal[5][3] = {
    { 0xd8, 0xc8, 0xa8 }, { 0xc4, 0xb0, 0x90 }, { 0x9a, 0x96, 0x8c },
    { 0xa8, 0x70, 0x5c }, { 0x9c, 0x7c, 0x54 }
};
/* Roof materials: slate, red tile, thatch, wood shingle. */
static const Uint8 roof_pal[4][3][3] = {
    { {0x30,0x36,0x48},{0x44,0x4e,0x66},{0x5e,0x6a,0x86} },
    { {0x74,0x32,0x28},{0x9c,0x48,0x36},{0xc0,0x64,0x48} },
    { {0x6c,0x56,0x2c},{0x92,0x76,0x40},{0xb8,0x9c,0x5e} },
    { {0x4a,0x38,0x28},{0x66,0x4e,0x38},{0x86,0x6a,0x4c} }
};

/* A filled 2:1 diamond centred on (cx, cy). Same column geometry as iso_tile,
 * so a roof ring lines up exactly with the tile grid it sits over. */
static void iso_diamond(SDL_Surface *fb, int cx, int cy, int rw, Uint32 c)
{
    int i, i0 = -rw, i1 = rw;

    if (cx + i0 < 0)      i0 = -cx;
    if (cx + i1 > fb->w)  i1 = fb->w - cx;
    for (i = i0; i <= i1; i++) {
        int a = i < 0 ? -i : i;
        int half = (rw - a) / 2;
        vspan(fb, cx + i, cy - half, half * 2 + 1, c);
    }
}

/* The same diamond split down its vertical centre line into a left and a right
 * colour.
 *
 * This exists for roofs. A hip roof drawn as a stack of concentric diamonds has
 * no volume: every slice is one flat colour, so however the steps are shaded up
 * the slope the result reads as a plate, which is exactly how every house in the
 * build looked. Terrain does not have this problem because it gets its volume
 * from FACE_L/FACE_R — one notional light, two constants, no normals. Roofs now
 * use the identical trick: the left half of every slice is the down-left slope,
 * the right half is the down-right slope, and they take different shades.
 *
 * Same column geometry as iso_diamond, so the two still line up exactly. */
static void iso_diamond_lr(SDL_Surface *fb, int cx, int cy, int rw,
                           Uint32 cl, Uint32 cr)
{
    int i, i0 = -rw, i1 = rw;

    if (cx + i0 < 0)      i0 = -cx;
    if (cx + i1 > fb->w)  i1 = fb->w - cx;
    for (i = i0; i <= i1; i++) {
        int a = i < 0 ? -i : i;
        int half = (rw - a) / 2;
        vspan(fb, cx + i, cy - half, half * 2 + 1, i < 0 ? cl : cr);
    }
}

/* True colour per terrain, before the fog blend. Flat-shaded and readable —
 * design/Overview.md is explicit that the painted mockup is pitch art and the
 * in-engine target is simple procedural geometry. */
static void terrain_colour(int terrain, int *r, int *g, int *b)
{
    switch (terrain) {
    case TERRAIN_WATER: *r = 0x2f; *g = 0x6d; *b = 0x7d; break; /* Wade */
    case TERRAIN_LEDGE: *r = 0x8c; *g = 0x70; *b = 0x48; break; /* Climb */
    case TERRAIN_DARK:  *r = 0x3b; *g = 0x33; *b = 0x50; break; /* Kindle */
    /* Sage, not the primary green this was. design/Art Bible.md §4: the old
     * 0x4e9e54 was the most saturated thing on screen and flattened everything
     * next to it — trees included, which is what made them merge into the lawn. */
    default:            *r = 0x3e; *g = 0x5c; *b = 0x35; break;
    }
}

/* A filled ellipse, one horizontal run per row.
 *
 * Canopy lobes used to be axis-aligned fill_rects, and that is the single
 * reason trees read as broccoli: six stacked rectangles of decreasing width are
 * a stepped pyramid however carefully their widths are chosen, and no amount of
 * palette work fixes a silhouette. design/Art Bible.md §3 asks for foliage to be
 * ROUND against angular architecture and faceted rock, and this is the primitive
 * that buys it. Costs one span per row against one per lobe — nothing, against
 * the ~150x render headroom measured in devlog/2026-08-04-session-01. */
static void fill_ellipse(SDL_Surface *fb, int cx, int cy, int rx, int ry, Uint32 c)
{
    int dy;

    if (rx <= 0 || ry <= 0)
        return;
    for (dy = -ry; dy <= ry; dy++) {
        float t = (float)dy / (float)ry;
        int w = (int)((float)rx * SDL_sqrtf(1.0f - t * t) + 0.5f);
        if (w > 0)
            fill_rect(fb, cx - w, cy + dy, w * 2, 1, c);
    }
}

/* ---------------------------------------------------------------- props --
 *
 * Layered procedural scenery. Every prop is built from flat rectangles in a
 * handful of shades, stacked back to front, with each dimension jittered from a
 * different bit field of the tile hash. Nothing here is a sprite and nothing is
 * stored per tile: a prop is entirely a function of (seed, tx, ty).
 *
 * The point of the layering is combinatorial. A tree draws its trunk in 3
 * shades under a canopy of 5 lobes in 4 shades, and the free parameters are
 *
 *   8 canopy palettes x 4 trunk palettes x 4 trunk heights
 *     x 4 canopy widths x 4 canopy heights x 4 leans   = 8,192 distinct trees
 *
 * from ~130 bytes of palette data and about forty lines. Eight palettes is
 * enough because the eye reads silhouette before colour — the shape jitter does
 * the work, and the palettes only have to stop the wood being a monoculture. */

/* Canopy palettes, four shades each, dark to light, all inside the foliage value
 * band of design/Art Bible.md §4.
 *
 * The two autumn palettes that used to sit at indices 4 and 5 are gone. At full
 * reveal they read as autumn; at the 0.42 that walking alone ever reaches they
 * read as DEAD, and with a quarter of the wood drawn in them every screenshot
 * looked like a blighted forest. Variety now comes from hue within green —
 * blue-green, olive, teal, yellow-green — which survives the fog blend because
 * it is carried by value, not by hue. */
static const Uint8 canopy_pal[8][4][3] = {
    { {0x1e,0x33,0x24},{0x2b,0x4a,0x2e},{0x3d,0x66,0x3a},{0x54,0x80,0x49} },
    { {0x1a,0x30,0x28},{0x26,0x46,0x38},{0x36,0x60,0x4c},{0x4a,0x7a,0x62} },
    { {0x20,0x31,0x1c},{0x2f,0x48,0x27},{0x42,0x63,0x35},{0x5b,0x7e,0x47} },
    { {0x16,0x28,0x24},{0x21,0x3b,0x35},{0x2f,0x53,0x49},{0x40,0x6e,0x60} },
    { {0x24,0x36,0x1e},{0x35,0x4e,0x2b},{0x49,0x6b,0x3b},{0x63,0x88,0x4d} },
    { {0x1c,0x2c,0x20},{0x28,0x41,0x2e},{0x38,0x5a,0x3f},{0x4c,0x74,0x53} },
    { {0x12,0x22,0x1a},{0x1b,0x33,0x27},{0x27,0x48,0x36},{0x36,0x60,0x49} },
    { {0x26,0x38,0x22},{0x37,0x52,0x30},{0x4c,0x70,0x41},{0x68,0x90,0x55} }
};

/* Trunk palettes: shadow side, body, lit side. Three slices is what makes a
 * trunk read as round rather than as a stick. */
static const Uint8 trunk_pal[4][3][3] = {
    { {0x2a,0x1e,0x18},{0x42,0x30,0x22},{0x5c,0x44,0x30} },
    { {0x24,0x1c,0x1a},{0x38,0x2c,0x28},{0x50,0x40,0x38} },
    { {0x32,0x26,0x18},{0x4c,0x3a,0x24},{0x68,0x52,0x34} },
    { {0x1e,0x1a,0x16},{0x30,0x2a,0x24},{0x46,0x3e,0x34} }
};

/* Lobe widths as a percentage of the canopy width, and which shade each takes.
 * Widest just below the middle and lighter going up, so the stack reads as a
 * rounded mass lit from above rather than as a pile of boxes. Six lobes rather
 * than four, and each drawn taller than its step, so they overlap into one
 * silhouette — at five barely-touching lobes the canopy read as a stack of
 * discs on a stick. */
#define LOBES 6
static const Uint8 lobe_w[LOBES] = { 56, 80, 95, 100, 86, 60 };
static const Uint8 lobe_s[LOBES] = { 3, 3, 2, 2, 1, 0 };

/* Rock, crystal and reed palettes. Three shades each is the minimum that reads
 * as a lit form rather than a silhouette, and the maximum worth spending on
 * something a few pixels across. */
static const Uint8 rock_pal[3][3][3] = {
    { {0x3e,0x38,0x32},{0x5c,0x54,0x4a},{0x7a,0x72,0x66} },
    { {0x44,0x3c,0x36},{0x64,0x5a,0x50},{0x86,0x7c,0x70} },
    { {0x38,0x36,0x36},{0x54,0x52,0x52},{0x72,0x70,0x70} }
};
static const Uint8 crystal_pal[3][3][3] = {
    { {0x4a,0x2e,0x82},{0x7a,0x50,0xc0},{0xb4,0x8e,0xf0} },
    { {0x2e,0x4a,0x82},{0x50,0x7a,0xc0},{0x8e,0xb4,0xf0} },
    { {0x62,0x2e,0x70},{0x96,0x50,0xa8},{0xc8,0x8e,0xd8} }
};

enum { PROP_NONE = 0, PROP_TREE, PROP_BUSH, PROP_ROCK, PROP_REED,
       PROP_FLOWER, PROP_CRYSTAL, PROP_STUMP };

/* cx is the tile centre in screen x; by is the ground under it, already lifted
 * by the tile's height, so the tree stands ON the tile rather than through it. */
static void draw_tree(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    const Uint8 (*cp)[3] = canopy_pal[(h >> 13) & 7];
    const Uint8 (*tp)[3] = trunk_pal[(h >> 16) & 3];
    /* Trunk kept short relative to the crown: at 11..17 px under a 20 px canopy
     * every tree read as a lollipop. The crown now starts low, the way a
     * deciduous tree actually does. */
    int th   = 7 + (int)((h >> 18) & 3) * 2;   /* trunk height  7..13 */
    int cw   = 22 + (int)((h >> 20) & 3) * 3;  /* canopy width  22..31 */
    int ch   = 22 + (int)((h >> 22) & 3) * 3;  /* canopy height 22..31 */
    int lean = (int)((h >> 24) & 3) - 1;
    int step = ch / LOBES;
    int top  = by - th - ch;
    int i;

    /* Contact shadow first, flat on the ground plane. Without one a prop floats:
     * in an isometric projection there is no other cue for where its base
     * actually meets the tile, and every prop in the world was floating.
     * design/Art Bible.md §3 — "everything touches the ground". */
    iso_diamond(fb, cx, by - 1, cw / 3, fog_lerp(fb, 0x24, 0x33, 0x22, rev));

    fill_rect(fb, cx - 3, by - th, 3, th, fog_lerp(fb, tp[0][0], tp[0][1], tp[0][2], rev));
    fill_rect(fb, cx,     by - th, 2, th, fog_lerp(fb, tp[1][0], tp[1][1], tp[1][2], rev));
    fill_rect(fb, cx + 2, by - th, 1, th, fog_lerp(fb, tp[2][0], tp[2][1], tp[2][2], rev));

    cx += lean;
    for (i = 0; i < LOBES; i++) {
        int lw = cw * lobe_w[i] / 100;
        int s  = lobe_s[i];
        int ly = top + i * step + (step + 4) / 2;
        fill_ellipse(fb, cx, ly, lw / 2, (step + 5) / 2,
                     fog_lerp(fb, cp[s][0], cp[s][1], cp[s][2], rev));
    }
    /* The few pixels that sell the volume: a highlight on the up-left shoulder,
     * where the notional light already lands on the tile faces. */
    fill_ellipse(fb, cx - (cw * 95 / 100) / 4, top + step + 2, 4, 2,
                 fog_lerp(fb, cp[3][0], cp[3][1], cp[3][2], rev));
}

static void draw_bush(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    const Uint8 (*cp)[3] = canopy_pal[(h >> 13) & 7];
    int bw = 13 + (int)((h >> 20) & 3) * 2;
    int bh = 8 + (int)((h >> 22) & 3) * 2;

    iso_diamond(fb, cx, by - 1, bw / 3, fog_lerp(fb, 0x24, 0x33, 0x22, rev));
    fill_ellipse(fb, cx, by - bh / 2, bw / 2, (bh + 1) / 2,
                 fog_lerp(fb, cp[1][0], cp[1][1], cp[1][2], rev));
    fill_ellipse(fb, cx, by - bh - 1, bw / 3, 3,
                 fog_lerp(fb, cp[2][0], cp[2][1], cp[2][2], rev));
    fill_rect(fb, cx - bw / 3 + 1, by - bh - 2, 4, 2,
              fog_lerp(fb, cp[3][0], cp[3][1], cp[3][2], rev));
}

/* A boulder: three stacked slabs, widest at the base, lit from the same
 * up-left direction as everything else. */
static void draw_rock(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    const Uint8 (*p)[3] = rock_pal[(h >> 13) % 3];
    int rw = 12 + (int)((h >> 20) & 3) * 3;
    int rh = 7 + (int)((h >> 22) & 3) * 2;

    fill_rect(fb, cx - rw / 2, by - rh, rw, rh,
              fog_lerp(fb, p[1][0], p[1][1], p[1][2], rev));
    fill_rect(fb, cx - rw / 2, by - rh, rw / 2, rh,
              fog_lerp(fb, p[0][0], p[0][1], p[0][2], rev));
    fill_rect(fb, cx - rw / 4, by - rh - 3, rw * 2 / 3, 4,
              fog_lerp(fb, p[2][0], p[2][1], p[2][2], rev));
}

/* Reeds: a few thin blades of differing height. Deliberately spindly — this is
 * what tells you the blue tile is shallow water rather than a hole. */
static void draw_reed(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    Uint32 dark = fog_lerp(fb, 0x2c, 0x54, 0x38, rev);
    Uint32 lit  = fog_lerp(fb, 0x54, 0x84, 0x50, rev);
    int i;

    for (i = 0; i < 5; i++) {
        int ox = -8 + i * 4 + (int)((h >> (i * 3)) & 3);
        int bh = 9 + (int)((h >> (i * 3 + 2)) & 7);
        fill_rect(fb, cx + ox, by - bh, 1, bh, (i & 1) ? lit : dark);
    }
}

/* A clump of blooms on stems. Four hue choices, so a meadow has colour without
 * a fourth palette table. */
static void draw_flower(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    static const Uint8 bloom[4][3] = {
        { 0xe8, 0xd8, 0x60 }, { 0xe0, 0x78, 0x90 },
        { 0xd8, 0xe8, 0xf0 }, { 0xc8, 0x90, 0xe8 }
    };
    const Uint8 *b = bloom[(h >> 13) & 3];
    Uint32 stem = fog_lerp(fb, 0x38, 0x70, 0x3c, rev);
    Uint32 head = fog_lerp(fb, b[0], b[1], b[2], rev);
    int i;

    for (i = 0; i < 3; i++) {
        int ox = -5 + i * 5 + (int)((h >> (i * 4)) & 3);
        int bh = 5 + (int)((h >> (i * 4 + 2)) & 3);
        fill_rect(fb, cx + ox, by - bh, 1, bh, stem);
        fill_rect(fb, cx + ox - 1, by - bh - 2, 3, 2, head);
    }
}

/* A memory crystal: a tapering shard, brightest at the tip. The one prop in the
 * set that is meant to look like it is emitting rather than reflecting. */
static void draw_crystal(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    const Uint8 (*p)[3] = crystal_pal[(h >> 13) % 3];
    int ch = 14 + (int)((h >> 20) & 3) * 3;
    int i, bands = 4;

    for (i = 0; i < bands; i++) {
        int w = 8 - i * 2;
        int s = (i * 3) / bands;
        if (w < 2) w = 2;
        fill_rect(fb, cx - w / 2, by - (ch * (i + 1)) / bands,
                  w, ch / bands + 1,
                  fog_lerp(fb, p[s][0], p[s][1], p[s][2], rev));
    }
    fill_rect(fb, cx - 1, by - ch - 2, 2, 3,
              fog_lerp(fb, 0xf0, 0xe8, 0xff, rev));
}

/* A cut stump with a pale ring, and the sawn face catching the light. */
static void draw_stump(SDL_Surface *fb, int cx, int by, Uint32 h, float rev)
{
    const Uint8 (*tp)[3] = trunk_pal[(h >> 16) & 3];
    int sw = 8 + (int)((h >> 20) & 3);
    int sh = 5 + (int)((h >> 22) & 1) * 2;

    fill_rect(fb, cx - sw / 2, by - sh, sw, sh,
              fog_lerp(fb, tp[0][0], tp[0][1], tp[0][2], rev));
    fill_rect(fb, cx - sw / 2, by - sh, sw / 2, sh,
              fog_lerp(fb, tp[1][0], tp[1][1], tp[1][2], rev));
    fill_rect(fb, cx - sw / 2, by - sh - 2, sw, 3,
              fog_lerp(fb, tp[2][0], tp[2][1], tp[2][2], rev));
}

/* Which prop, if any, stands on this tile. Presence is decided from its own bit
 * field, before shape, so retuning a tree's jitter never moves a tree. */
static int prop_at(const World *w, Uint64 seed, int tx, int ty, Uint32 *hout)
{
    Uint32 h = tile_hash(seed, tx, ty);
    Uint8  rg;
    int    roll = (int)((h >> 8) & 31);

    *hout = h;

    /* Never stand a tall prop where the tile in front is far higher: the band
     * sweep draws that neighbour afterwards, but the prop is tall enough to
     * poke out above it, which is the one depth artefact the sweep cannot fix.
     * Three lines here beat a z-buffer. */
    if (height_at(w, tx + 1, ty) - height_at(w, tx, ty) >= 24 ||
        height_at(w, tx, ty + 1) - height_at(w, tx, ty) >= 24)
        return PROP_NONE;

    /* Boulders on the tops of cliffs, sparsely — enough to break the bare
     * plateau silhouette without turning it into scree. */
    if (w->solid[ty][tx])
        return (roll < 3) ? PROP_ROCK : PROP_NONE;

    rg = w->region[ty][tx];
    switch (rg == REGION_NONE ? TERRAIN_NORMAL : w->regions[rg].terrain) {
    case TERRAIN_WATER:  return (roll < 6) ? PROP_REED    : PROP_NONE;
    case TERRAIN_DARK:   return (roll < 7) ? PROP_CRYSTAL : PROP_NONE;
    case TERRAIN_LEDGE:  return (roll < 4) ? PROP_ROCK    : PROP_NONE;
    default:
        return (roll <  7) ? PROP_TREE
             : (roll < 12) ? PROP_BUSH
             : (roll < 14) ? PROP_STUMP
             : (roll < 19) ? PROP_FLOWER : PROP_NONE;
    }
}

/* A whole building's roof and fittings, drawn once from its record at the
 * footprint's front corner — never per tile, or a roof would tear along every
 * internal tile edge.
 *
 * The roof is a stack of shrinking diamonds rather than a pitched-plane
 * rasteriser. That reuses iso_diamond, is trivially correct, and in pixel art
 * reads convincingly as a hip roof; a real gable would need a second
 * rasteriser and an edge that has to agree with the first to the pixel, which
 * is the seam bug class this project already decided to avoid once. Roof shape
 * varies by how many steps it takes and how fast it narrows. */
static void draw_building(SDL_Surface *fb, const World *w, const Building *b,
                          int cam_x, int cam_y, float rev)
{
    Uint32 v = b->variant;
    const Uint8 (*rp)[3] = roof_pal[BV_RMAT(v)];
    const Uint8 *wp = wall_pal[BV_WALL(v)];
    int cx, cy, wall = WALL_BASE + b->levels * STOREY_H;
    int rw, steps, k, pitch;

    /* Centre of the footprint in world px, projected. */
    world_to_iso((float)(b->x * TILE + b->w * TILE / 2),
                 (float)(b->y * TILE + b->h * TILE / 2), &cx, &cy);
    cx -= cam_x;
    cy -= cam_y - ISO_HH;
    if (cx < -400 || cx > fb->w + 400)
        return;

    /* Half-width of the footprint's diamond, plus a small eave overhang.
     *
     * The rise has to be rw/2, not a fixed number of pixels: in a 2:1
     * projection a 45-degree roof over a footprint of half-width rw rises
     * exactly rw/2 on screen. The first attempt used a flat 5-7 px per step and
     * every house read as an open-topped box with a plate balanced on it.
     *
     * Seven steps rather than three, for the same reason: over a 130 px wide
     * diamond, three steps is a ziggurat and seven reads as a slope. */
    rw    = (b->w + b->h) * ISO_HW / 2 + 5;
    steps = 8;
    {
        int rise = (BV_RSHAPE(v) >= 3) ? (rw * 3) / 4   /* steep */
                 : (BV_RSHAPE(v) == 0) ? (rw * 2) / 5   /* shallow */
                                       : rw / 2;        /* 45 degrees */
        pitch = rise / steps;
        if (pitch < 3)
            pitch = 3;
    }

    /* Each slice split left/right, so the roof has two lit faces the way every
     * other solid in the world does. Before this the whole stack was concentric
     * rings of one colour per step and every house read as a plate; an
     * eave-shadow diamond was tried first and made it worse, because a ring
     * under a ring is still rings.
     *
     * Slight lightening up the slope is kept on top of the split — a roof does
     * catch more light near the ridge — but it is no longer doing the work. */
    for (k = 0; k < steps; k++) {
        int krw = rw - (rw * k) / (steps + 1);
        int s   = (k * 3) / steps;
        iso_diamond_lr(fb, cx, cy - wall - k * pitch, krw,
                       fog_lerp(fb, rp[s][0] * ROOF_L / 100,
                                    rp[s][1] * ROOF_L / 100,
                                    rp[s][2] * ROOF_L / 100, rev),
                       fog_lerp(fb, rp[s][0], rp[s][1], rp[s][2], rev));
    }
    /* Ridge cap. Small on purpose: at rw/(steps+1)+3 the top diamond was wide
     * enough to read as a flat plateau, which is what made the roof look like a
     * tarp stretched over a box instead of coming to a peak. */
    iso_diamond_lr(fb, cx, cy - wall - steps * pitch, rw / (steps + 2),
                   fog_lerp(fb, rp[2][0] * ROOF_L / 100,
                                rp[2][1] * ROOF_L / 100,
                                rp[2][2] * ROOF_L / 100, rev),
                   fog_lerp(fb, rp[2][0], rp[2][1], rp[2][2], rev));

    /* Chimney, on the roof rather than beside it. */
    if (BV_CHIM(v)) {
        int ox = (BV_CHIM(v) == 1) ? -rw / 3 : rw / 3;
        int ch = 8 + (int)BV_CHIM(v) * 3;
        fill_rect(fb, cx + ox - 3, cy - wall - steps * pitch - ch, 6, ch + 6,
                  fog_lerp(fb, 0x6e, 0x5a, 0x4e, rev));
        fill_rect(fb, cx + ox - 4, cy - wall - steps * pitch - ch - 2, 8, 3,
                  fog_lerp(fb, 0x86, 0x72, 0x64, rev));
    }

    /* Facade: door and windows on the two faces the camera can see. The wall
     * itself was drawn by the tile pass; these sit on top of it. Positions are
     * along the footprint's front edges, in the same 2:1 slope. */
    {
        int i, fy = cy - wall + ISO_HH;
        Uint32 dark  = fog_lerp(fb, wp[0] * 35 / 100, wp[1] * 35 / 100,
                                wp[2] * 35 / 100, rev);
        Uint32 glass = fog_lerp(fb, 0x3c, 0x4e, 0x62, rev);
        Uint32 trim  = fog_lerp(fb, wp[0] * 72 / 100, wp[1] * 72 / 100,
                                wp[2] * 72 / 100, rev);
        int nwin = 1 + (int)BV_WIN(v) % 3;

        /* Door on the down-right face, one storey tall. */
        {
            int dx = cx + rw / 3, dy = fy + (rw / 3) / 2;
            fill_rect(fb, dx - 4, dy - 16, 8, 16, dark);
            if (BV_DOOR(v) >= 2) /* arched or double: a lintel */
                fill_rect(fb, dx - 5, dy - 18, 10, 2, trim);
        }
        /* Windows on the down-left face, spread along it. */
        for (i = 0; i < nwin; i++) {
            int ox = -rw + (rw * 2 * (i + 1)) / (nwin + 2);
            int wx = cx + ox / 2 - rw / 4, wy = fy + (ox / 2 + rw / 4) / 2;
            int wh = 7;
            fill_rect(fb, wx - 3, wy - 14 - wh, 7, wh, glass);
            if (BV_TRIM(v) & 1)
                fill_rect(fb, wx - 4, wy - 15 - wh, 9, 2, trim);
        }
        /* A hanging sign or banner: the one asymmetric detail, so a row of
         * houses does not read as a repeated stamp. */
        if (BV_SIGN(v) == 1)
            fill_rect(fb, cx + rw / 2, fy + rw / 4 - 24, 7, 10,
                      fog_lerp(fb, 0x8c, 0x5a, 0x36, rev));
        else if (BV_SIGN(v) == 2)
            fill_rect(fb, cx - rw / 2 - 3, fy - rw / 4 - 26, 5, 14,
                      fog_lerp(fb, 0x50, 0x64, 0xa8, rev));
    }
    (void)w;
}

/* Single dispatch, so the render loop never grows a switch of its own. */
static void draw_prop(SDL_Surface *fb, int kind, int cx, int by, Uint32 h, float rev)
{
    switch (kind) {
    case PROP_TREE:    draw_tree(fb, cx, by, h, rev);    break;
    case PROP_BUSH:    draw_bush(fb, cx, by, h, rev);    break;
    case PROP_ROCK:    draw_rock(fb, cx, by, h, rev);    break;
    case PROP_REED:    draw_reed(fb, cx, by, h, rev);    break;
    case PROP_FLOWER:  draw_flower(fb, cx, by, h, rev);  break;
    case PROP_CRYSTAL: draw_crystal(fb, cx, by, h, rev); break;
    case PROP_STUMP:   draw_stump(fb, cx, by, h, rev);   break;
    default: break;
    }
}

/* How much of a tile's colour reaches the screen: sight shows shape,
 * restoration brings colour, and whichever is stronger wins — so a restored
 * region stays lit after you leave it, because restoration is permanent and
 * sight is not a memory of colour. */
static float tile_reveal(const Game *g, int tx, int ty, int overlay)
{
    Uint8 rg;
    float rev, restored;

    if (overlay)
        return 1.0f;
    rg = g->w.region[ty][tx];
    restored = (rg == REGION_NONE) ? 0.0f : g->w.regions[rg].restoration;
    rev = g->w.reveal[ty][tx];
    return restored > rev ? restored : rev;
}

/* Ground colour before fog, as flat r/g/b. */
static void tile_colour(const Game *g, int tx, int ty, int overlay,
                        int *cr, int *cg, int *cb)
{
    Uint8 bi = g->w.bld_at[ty][tx];

    if (bi) {
        const Uint8 *p = wall_pal[BV_WALL(g->w.bld[bi - 1].variant)];
        *cr = p[0]; *cg = p[1]; *cb = p[2];
    } else if (g->w.surf[ty][tx] == SURF_OCEAN) {
        /* Water ramp, design/Art Bible.md §4, picked by depth. A single flat
         * blue reads as painted paper; stepping the ramp with the sea floor
         * makes the shelf near the shore read as shallows. */
        static const Uint8 water_ramp[4][3] = {
            {0x2f,0x6d,0x7d}, {0x25,0x5b,0x6c}, {0x1e,0x4a,0x5c}, {0x18,0x3d,0x4d}
        };
        int d = (ELEV_WATER - g->w.height[ty][tx]) / 4;
        if (d < 0) d = 0;
        if (d > 3) d = 3;
        *cr = water_ramp[d][0]; *cg = water_ramp[d][1]; *cb = water_ramp[d][2];
    } else if (g->w.solid[ty][tx]) {
        /* Stone ramp, varied per tile. One flat grey over a whole outcrop was
         * the other half of the "concrete slab" read — real rock has tonal
         * variation across its face, and three shades is enough to get it. */
        /* Just above grass in value, and no further. Stone should catch more
         * light than vegetation, but at the previous 0x5c5a68 it became the
         * BRIGHTEST large surface in the world — under fog the outcrops read as
         * white shapes floating in a dark field and pulled the eye away from
         * the player and the lit ground. Walls are meant to be the lightest
         * mass on screen; see the value hierarchy in design/Art Bible.md §4. */
        static const Uint8 stone_ramp[3][3] = {
            {0x44,0x42,0x4e}, {0x4e,0x4c,0x59}, {0x59,0x57,0x64}
        };
        const Uint8 *p = stone_ramp[tile_hash(g->seed, tx, ty) % 3u];
        *cr = p[0]; *cg = p[1]; *cb = p[2];
    } else {
        Uint8 reg = g->w.region[ty][tx];
        terrain_colour(reg == REGION_NONE ? TERRAIN_NORMAL
                                          : g->w.regions[reg].terrain, cr, cg, cb);
        /* Alternate brightness by region id so boundaries are visible without
         * needing a font or an outline pass. */
        if (overlay && reg != REGION_NONE && (reg & 1)) {
            *cr = *cr * 3 / 4; *cg = *cg * 3 / 4; *cb = *cb * 3 / 4;
        }
    }
}

static void render(SDL_Surface *fb, Game *g, int overlay)
{
    int band, b0, b1, i, y;
    int ptx = (int)(g->p.x / TILE), pty = (int)(g->p.y / TILE);
    int pband = ptx + pty;
    Uint32 voidc = SDL_MapRGB(fb->format, VOID_R, VOID_G, VOID_B);

    /* Mandatory now, unlike in the flat renderer: outside the landmass and in
     * the ELEV_MAX strip above the north rim there is simply nothing to draw.
     * SDL_memset4 is SDL_FORCE_INLINE, so this costs no link bytes. */
    for (y = 0; y < fb->h; y++)
        SDL_memset4((Uint8 *)fb->pixels + y * fb->pitch, voidc, (size_t)fb->w);
    PERF_COUNT(fb->w * fb->h);

    /* Back to front by diagonal band. ay is constant across a band, so the
     * vertical cull rejects whole bands with one test and the inner loop only
     * ever runs over rows that can be on screen. */
    b0 = (g->cam_y - ISO_OY - DIA_H) / ISO_HH;
    b1 = (g->cam_y - ISO_OY + fb->h + ELEV_MAX) / ISO_HH;
    if (b0 < 0) b0 = 0;
    if (b1 > BAND_MAX) b1 = BAND_MAX;
    /* Clamp the player into the drawn range: the camera keeps it on screen, but
     * a clamped camera at a world corner could leave its band just outside, and
     * a player that vanishes is worse than one drawn a band early. */
    if (pband < b0) pband = b0;
    if (pband > b1) pband = b1;

    for (band = b0; band <= b1; band++) {
        int lo = band - (WORLD_H - 1);
        int hi = band;
        int ay = band * ISO_HH + ISO_OY - g->cam_y;
        int tx;
        if (lo < 0) lo = 0;
        if (hi > WORLD_W - 1) hi = WORLD_W - 1;

        for (tx = lo; tx <= hi; tx++) {
            int ty = band - tx;
            int ax = (tx - ty) * ISO_HW + ISO_OX - g->cam_x;
            int cr, cg, cb, h, hl, hr, terr, nmark, mw;
            float rev;
            Uint32 top, top2, c_l = 0, c_r = 0, hash, mark;
            if (ax + ISO_HW <= 0 || ax - ISO_HW >= fb->w)
                continue;

            /* (tx, ty+1) is down-left on screen and (tx+1, ty) is down-right,
             * which is exactly the pair of faces this tile can show. A drop of
             * zero or less means the neighbour hides that face entirely. */
            h  = g->w.height[ty][tx];
            hl = h - height_at(&g->w, tx, ty + 1);
            hr = h - height_at(&g->w, tx + 1, ty);
            if (hl < 0) hl = 0;
            if (hr < 0) hr = 0;

            tile_colour(g, tx, ty, overlay, &cr, &cg, &cb);
            rev = tile_reveal(g, tx, ty, overlay);
            hash = tile_hash(g->seed, tx, ty);
            top = fog_lerp(fb, cr, cg, cb, rev);
            /* A second shade a few per cent lighter, dithered across the face.
             * Small on purpose: enough to break the flat fill, not enough to
             * read as a pattern. */
            top2 = fog_lerp(fb, cr + (cr >> 4), cg + (cg >> 4), cb + (cb >> 4), rev);
            /* Most tiles are flat, so the side colours are only computed when
             * there is actually a face to paint with them. */
            if (hl)
                c_l = fog_lerp(fb, cr * FACE_L / 100, cg * FACE_L / 100,
                               cb * FACE_L / 100, rev);
            if (hr)
                c_r = fog_lerp(fb, cr * FACE_R / 100, cg * FACE_R / 100,
                               cb * FACE_R / 100, rev);
            iso_tile(fb, ax, ay, h, hl, hr, top, top2, hash, c_l, c_r);

            /* Per-terrain marks on top of the face. Rock gets a few pale
             * pebbles and a crack; grass gets tufts, both lighter and darker so
             * it reads as texture rather than as sprinkles; ledges get long
             * horizontal strata that emphasise the shelf. */
            terr = g->w.solid[ty][tx] ? -1 : (g->w.region[ty][tx] == REGION_NONE
                       ? TERRAIN_NORMAL : g->w.regions[g->w.region[ty][tx]].terrain);
            nmark = 5; mw = 2;
            if (terr < 0) {
                /* Rock. Long marks, because stone reads through aligned
                 * repetition — strata, not speckle (design/Art Bible.md §6). */
                mark = fog_lerp(fb, 0x6e, 0x6c, 0x7a, rev); nmark = 4; mw = 6;
            } else if (terr == TERRAIN_LEDGE) {
                mark = fog_lerp(fb, 0xa8, 0x90, 0x60, rev); nmark = 3; mw = 7;
            } else if (terr == TERRAIN_WATER) {
                mark = fog_lerp(fb, 0x5a, 0xa0, 0xa8, rev); nmark = 3; mw = 5;
            } else if (terr == TERRAIN_DARK) {
                mark = fog_lerp(fb, 0x58, 0x4c, 0x74, rev); nmark = 2; mw = 2;
            } else {
                mark = fog_lerp(fb, 0x55, 0x7a, 0x45, rev); nmark = 6; mw = 2;
            }
            tile_detail(fb, ax, ay, h, hash, mark, nmark, mw);
            /* Grass gets a second, darker scatter. One shade of speckle reads
             * as dirt on a flat field; two read as depth in the grass. Both
             * shades now come from the grass ramp rather than being invented,
             * so the tufts sit in the same family as the ground. */
            if (terr == TERRAIN_NORMAL)
                tile_detail(fb, ax, ay, h, hash * 2654435761u,
                            fog_lerp(fb, 0x2c, 0x44, 0x29, rev), 5, 2);
        }

        /* Second sub-pass over the SAME band: props. It has to be separate from
         * the ground pass because a prop is tall enough to spill onto the
         * diamond of the tile to its right, which is in the same band and whose
         * ground would otherwise repaint over it. */
        for (tx = lo; tx <= hi; tx++) {
            int ty = band - tx;
            int ax = (tx - ty) * ISO_HW + ISO_OX - g->cam_x;
            int kind, by;
            float rev;
            Uint32 hash;
            if (ax + ISO_HW - 20 <= 0 || ax - ISO_HW + 20 >= fb->w)
                continue;
            /* A building is drawn once, when the sweep reaches its front-most
             * corner tile — so it occludes everything behind it and is
             * occluded by everything in front, and its roof never tears along
             * an internal tile edge. */
            {
                Uint8 bi = g->w.bld_at[ty][tx];
                if (bi) {
                    const Building *b = &g->w.bld[bi - 1];
                    if (tx == b->x + b->w - 1 && ty == b->y + b->h - 1) {
                        float brev = tile_reveal(g, tx, ty, overlay);
                        if (overlay || brev >= 0.06f)
                            draw_building(fb, &g->w, b, g->cam_x, g->cam_y, brev);
                    }
                    continue;
                }
            }
            kind = prop_at(&g->w, g->seed, tx, ty, &hash);
            if (kind == PROP_NONE)
                continue;
            rev = tile_reveal(g, tx, ty, overlay);
            if (!overlay && rev < 0.06f)
                continue; /* nothing is legible this deep in the fog */
            /* The tile centre projects to (ax, ay + ISO_HH); lifting by the
             * tile's height puts the prop's feet on the surface. */
            by = ay + ISO_HH - g->w.height[ty][tx];
            draw_prop(fb, kind, ax, by, hash, rev);
        }

        /* Entities standing in this band. Scanned per band rather than per tile
         * — 19 compares times ~34 visible bands, not 19 times ~500 tiles.
         *
         * Found Souls follow Lost -> Found -> Remembered from their design note:
         * unseen, then a pale grey silhouette, then coloured once restored.
         * Fragments glow warm, ability-granting ones brighter, and fade to a dim
         * marker once restored so a cleared region does not still look full of
         * things to do. */
        for (i = 0; i < ENTITY_COUNT; i++) {
        int t = g->ents[i].tile, ex, ey, s, sx, sy;
        int cr, cg, cb;
        if (t < 0)
            continue;
        ex = t % WORLD_W;
        ey = t / WORLD_W;
        if (ex + ey != band)
            continue;
        if (!overlay && g->w.reveal[ey][ex] < 0.15f)
            continue; /* Lost: not yet discovered */
        if (g->ents[i].is_soul) {
            if (g->ents[i].restored) {
                cr = 0xf0; cg = 0xd0; cb = 0x90; s = 18; /* Remembered */
            } else {
                cr = 0x9a; cg = 0xa8; cb = 0xb8; s = 18; /* Found */
            }
        } else if (g->ents[i].restored) {
            cr = 0x6a; cg = 0x6a; cb = 0x62; s = 8;
        } else if (g->ents[i].grants) {
            cr = 0xff; cg = 0x9a; cb = 0x3c; s = 16;
        } else {
            cr = 0xff; cg = 0xd7; cb = 0x6a; s = 12;
        }
        /* Entities are tile-anchored, so project the tile centre, then lift by
         * the tile's height — without this a fragment on a ledge is drawn
         * buried in the cliff it sits on. */
        world_to_iso((float)(ex * TILE + TILE / 2), (float)(ey * TILE + TILE / 2),
                     &sx, &sy);
        sy -= height_at(&g->w, ex, ey);
        fill_rect(fb, sx - s / 2 - g->cam_x, sy - s / 2 - g->cam_y, s, s,
                  SDL_MapRGB(fb->format, (Uint8)cr, (Uint8)cg, (Uint8)cb));
        }

        /* The player, drawn in its own band: after everything one tile behind
         * it, before everything one tile in front. That is the entire depth
         * sort — a tree ahead of you occludes you, a tree behind you does not,
         * and there is no z-buffer anywhere. */
        if (band == pband) {
            int px, py;
            world_to_iso(g->p.x, g->p.y, &px, &py);
            py -= height_at(&g->w, ptx, pty);
            px -= g->cam_x;
            py -= g->cam_y;

            /* A ring on the ground under the player when something is close
             * enough to restore — the only affordance telling you the interact
             * key will do anything. Drawn at the feet, not the centre, so it
             * reads as lying on the tile. */
            if (entity_in_reach(g) >= 0)
                iso_ring(fb, px, py + PLAYER_SIZE / 2, 26,
                         SDL_MapRGB(fb->format, 0xff, 0xf0, 0xc0));
            /* The camera deliberately is NOT lifted by height: following the
             * visual height would jerk the whole view the instant you step onto
             * a ledge, where letting the player ride up within the frame reads
             * as climbing. */
            fill_rect(fb, px - PLAYER_SIZE / 2, py - PLAYER_SIZE / 2,
                      PLAYER_SIZE, PLAYER_SIZE,
                      SDL_MapRGB(fb->format, 0xe0, 0x64, 0x28));
        }
    }
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

/* Same shape as the flat version; only the coordinate space and the two limits
 * change. The world's screen footprint is a diamond inside a 4000x2000 box, so
 * at the east and west corners the visible content is a thin wedge and the rest
 * is void. That is not a bug to fix — a landmass floating in a dark abyss is the
 * expected isometric read, and world_gen's forced-solid border ring gives it a
 * rock rim for free. */
static void camera_follow(Game *g, int view_w, int view_h)
{
    int max_x = ISO_MAP_W - view_w;
    int max_y = ISO_MAP_H + 2 * ELEV_MAX - view_h;

    world_to_iso(g->p.x, g->p.y, &g->cam_x, &g->cam_y);
    g->cam_x -= view_w / 2;
    g->cam_y -= view_h / 2;
    if (g->cam_x < 0) g->cam_x = 0;
    if (g->cam_y < 0) g->cam_y = 0;
    if (max_x > 0 && g->cam_x > max_x) g->cam_x = max_x;
    if (max_y > 0 && g->cam_y > max_y) g->cam_y = max_y;
}

/* --- window and presentation ---------------------------------------------
 *
 * The game draws into a LOGICAL_W x LOGICAL_H heap backbuffer and that is
 * hard-doubled into the window. Everything here exists so main() and the
 * autoplay harness present through the same path rather than two copies that
 * can drift. */

/* Largest integer scale whose window still fits on screen. Usable bounds
 * already exclude the taskbar, so only the title bar has to be allowed for;
 * guessing at the taskbar too was costing a whole scale step on a 2048x1152
 * desktop, where x2 fits with 72 px to spare. Falls back to the full desktop
 * mode, then to 1, which always fits. */
#define WIN_TITLEBAR 40

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

/* Wrap an SDL_malloc'd buffer as a drawable surface in the window's own pixel
 * format, which fog_lerp needs for SDL_MapRGB. Heap, never static: 2 MB of
 * runtime memory and zero exe bytes.
 *
 * Returns NULL if it cannot be made, and the caller then draws straight into
 * the window surface at native resolution. That is a real degraded mode rather
 * than a crash — the renderer does not care what resolution it is handed. */
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

/* Push whatever was drawn to the screen. The scale is recomputed from the live
 * surface every frame rather than remembered, so toggling fullscreen — where
 * the window stops being an exact multiple of the logical size — needs no
 * special case. `back` may be NULL (degraded mode), in which case the drawing
 * already went straight to the window surface. */
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

#if WAYFARER_SELFTEST
/* ---- Live tuning overlay (F3) --------------------------------------------
 *
 * Exists because Phase 02's fog rewrite took three edit-rebuild-screenshot
 * passes to settle two constants, and pass 3 only found the real problem
 * (stone had drifted too light in an earlier commit) because two values were
 * finally seen side by side. See design/phases/Phase 03 - Legibility Tools.md.
 *
 * Scope is render-only by decision, not by omission: every value here feeds
 * fog_lerp and nothing else, so a keypress shows on the next frame with no
 * regeneration and no stale state. LAND_ROCK_T and VILLAGE_* were considered
 * and deliberately left out — they feed world_gen, so adjusting them live
 * would have to re-run game_init (and with it the reachability verifier) on
 * every keypress, which is a different tool from this one.
 *
 * TAB/-/= rather than arrows because arrows are movement: the whole point is
 * to tune while walking around, so the overlay must not steal the keys that
 * put you in front of the thing you are judging. */
#define TUNE_ROWS 4

static int tune_show;
static int tune_row;

static void tune_adjust(int dir)
{
    switch (tune_row) {
    case 0:  fog_tune.tint_r += 2.0f * dir; break;
    case 1:  fog_tune.tint_g += 2.0f * dir; break;
    case 2:  fog_tune.tint_b += 2.0f * dir; break;
    default: fog_tune.keep   += 0.02f * dir; break;
    }
    if (fog_tune.tint_r <   0.0f) fog_tune.tint_r =   0.0f;
    if (fog_tune.tint_r > 255.0f) fog_tune.tint_r = 255.0f;
    if (fog_tune.tint_g <   0.0f) fog_tune.tint_g =   0.0f;
    if (fog_tune.tint_g > 255.0f) fog_tune.tint_g = 255.0f;
    if (fog_tune.tint_b <   0.0f) fog_tune.tint_b =   0.0f;
    if (fog_tune.tint_b > 255.0f) fog_tune.tint_b = 255.0f;
    if (fog_tune.keep   <   0.0f) fog_tune.keep   =   0.0f;
    if (fog_tune.keep   >   1.0f) fog_tune.keep   =   1.0f;
}

static void tune_draw(SDL_Surface *fb)
{
    static const char *const label[TUNE_ROWS] = {
        "FOG TINT R", "FOG TINT G", "FOG TINT B", "FOG KEEP"
    };
    const int line_h = (FONT_H + 3) * FONT_SCALE;
    const int col_x  = 8 + 14 * (FONT_W + 1) * FONT_SCALE;
    float val[TUNE_ROWS];
    char buf[32];
    Uint32 white = SDL_MapRGB(fb->format, 0xff, 0xff, 0xff);
    Uint32 pick  = SDL_MapRGB(fb->format, 0xff, 0xd0, 0x60);
    int i, y = 8;

    val[0] = fog_tune.tint_r;
    val[1] = fog_tune.tint_g;
    val[2] = fog_tune.tint_b;
    val[3] = fog_tune.keep;

    draw_text_shadow(fb, 8, y, "TUNE  TAB ROW  - = ADJUST  F3 HIDE", white);
    y += line_h + FONT_SCALE * 2;

    for (i = 0; i < TUNE_ROWS; i++) {
        Uint32 c = (i == tune_row) ? pick : white;
        if (i == tune_row)
            draw_text_shadow(fb, 8, y, ">", c);
        draw_text_shadow(fb, 8 + 2 * (FONT_W + 1) * FONT_SCALE, y, label[i], c);
        SDL_snprintf(buf, sizeof(buf), "%.2f", val[i]);
        draw_text_shadow(fb, col_x, y, buf, c);
        y += line_h;
    }
}
#endif /* WAYFARER_SELFTEST */

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
    SDL_Surface *fb, *back = NULL, *draw;
    void *back_px = NULL;
    Uint32 end;
    int restores = 0, scale;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("FAIL  SDL_Init(VIDEO): %s\n", SDL_GetError());
        return 1;
    }
    scale = pick_scale();
    win = SDL_CreateWindow("Wayfarer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           LOGICAL_W * scale, LOGICAL_H * scale, SDL_WINDOW_SHOWN);
    if (!win) {
        SDL_Quit();
        return 1;
    }
    if (scale >= 2) {
        SDL_Surface *wfb = SDL_GetWindowSurface(win);
        if (wfb && wfb->format->BytesPerPixel == 4)
            back = backbuffer_new(wfb, &back_px);
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
        draw = back ? back : fb;
        camera_follow(&g, draw->w, draw->h);
        render(draw, &g, 0);
        present(win, fb, back);
        SDL_Delay(4); /* faster than real time; this is a capture aid */
    }

    printf("restored %d  fragments %d/%d  souls %d/%d  stage %d\n",
           restores, g.frags_restored, FRAGMENT_COUNT, g.souls_restored,
           SOUL_COUNT, world_stage(&g));
    if (back)
        SDL_FreeSurface(back);
    SDL_free(back_px);
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
    /* Scale 1: this harness measures the input path, and a small window is
     * both sufficient and less disruptive to whatever else is on screen. */
    win = SDL_CreateWindow("Wayfarer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           LOGICAL_W, LOGICAL_H, SDL_WINDOW_SHOWN);
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

/* --- isometric rasteriser ------------------------------------------------
 *
 * The single highest-risk claim in the isometric change is that diamonds tile
 * the plane exactly — no gaps from rounding, no overdraw — and that raising a
 * tile leaves a hole its side face fills precisely. Both are argued from an
 * identity in iso_tile's comment; an argument is not a check, so this measures
 * it instead. Headless, no window, milliseconds.
 *
 * Note what makes case 1 strong: it asserts written == covered == N*1024, so a
 * renderer that filled gaps by overdrawing would fail it just as loudly as one
 * that left them. */
#define ISO_T_N  8                       /* patch is N x N tiles */
#define ISO_T_W  768
#define ISO_T_H  640
#define ISO_T_OX 384
#define ISO_T_OY 160

/* Our drawing code reads only w/h/pitch/pixels, so a headless framebuffer needs
 * no SDL surface API and no pixel format — colours are passed in already
 * packed. This is also the fallback documented for the backbuffer if
 * SDL_CreateRGBSurfaceWithFormatFrom is ever culled from our SDL build. */
static void fake_surface(SDL_Surface *s, Uint32 *px, int w, int h)
{
    SDL_zerop(s);
    s->w = w;
    s->h = h;
    s->pitch = w * 4;
    s->pixels = px;
}

/* Draw one patch tile. Heights come from the caller's array, with out-of-patch
 * treated as ground level — exactly how the renderer treats out-of-world. */
static void iso_t_draw(SDL_Surface *s, const int *hgt, int tx, int ty, Uint32 c,
                       int dy)
{
    int h  = hgt[ty * ISO_T_N + tx];
    int hl = h - ((ty + 1 < ISO_T_N) ? hgt[(ty + 1) * ISO_T_N + tx] : 0);
    int hr = h - ((tx + 1 < ISO_T_N) ? hgt[ty * ISO_T_N + tx + 1] : 0);
    int ax = (tx - ty) * ISO_HW + ISO_T_OX;
    int ay = (tx + ty) * ISO_HH + ISO_T_OY + dy;

    if (hl < 0) hl = 0;
    if (hr < 0) hr = 0;
    iso_tile(s, ax, ay, h, hl, hr, c, c, 0, c, c);
}

static int iso_selftest(Uint64 seed)
{
    const int npx = ISO_T_W * ISO_T_H;
    Uint32 *comp = (Uint32 *)SDL_calloc((size_t)npx, sizeof(Uint32));
    Uint32 *solo = (Uint32 *)SDL_calloc((size_t)npx, sizeof(Uint32));
    Uint8  *band = (Uint8 *)SDL_calloc((size_t)npx, 1); /* deepest band covering px */
    int hgt[ISO_T_N * ISO_T_N];
    SDL_Surface sc, ss;
    Rng rng;
    int tx, ty, i, fails = 0;
    int solo_sum = 0, comp_nz = 0, edge = 0;

    if (!comp || !solo || !band) {
        printf("FAIL  out of memory\n");
        SDL_free(comp); SDL_free(solo); SDL_free(band);
        return 1;
    }
    fake_surface(&sc, comp, ISO_T_W, ISO_T_H);
    fake_surface(&ss, solo, ISO_T_W, ISO_T_H);

    printf("=== isometric rasteriser, %dx%d tile patch ===\n", ISO_T_N, ISO_T_N);

    /* ---- case 1: flat ground tiles the plane exactly ---------------------- */
    for (i = 0; i < ISO_T_N * ISO_T_N; i++)
        hgt[i] = 0;
    for (ty = 0; ty < ISO_T_N; ty++) {
        for (tx = 0; tx < ISO_T_N; tx++) {
            int n = 0;
            SDL_memset(solo, 0, (size_t)npx * sizeof(Uint32));
            iso_t_draw(&ss, hgt, tx, ty, 0xFFFFFFFFu, 0);
            for (i = 0; i < npx; i++)
                if (solo[i])
                    n++;
            if (n != DIA_W * DIA_H / 2) {
                printf("FAIL  tile (%d,%d) covers %d px, expected %d\n",
                       tx, ty, n, DIA_W * DIA_H / 2);
                fails++;
            }
            solo_sum += n;
            iso_t_draw(&sc, hgt, tx, ty, 0xFFFFFFFFu, 0);
        }
    }
    for (i = 0; i < npx; i++)
        if (comp[i])
            comp_nz++;
    printf("flat: covered %d px, composite %d px, expected %d\n",
           solo_sum, comp_nz, ISO_T_N * ISO_T_N * DIA_W * DIA_H / 2);
    if (solo_sum != ISO_T_N * ISO_T_N * DIA_W * DIA_H / 2) fails++;
    if (comp_nz != solo_sum) {
        printf("FAIL  composite != sum of tiles: %d px drawn twice or lost\n",
               solo_sum - comp_nz);
        fails++;
    }
    /* Anything touching the border means the patch was clipped, which would
     * have deflated every count above and hidden a real failure. */
    for (i = 0; i < ISO_T_W; i++)
        if (comp[i] || comp[(ISO_T_H - 1) * ISO_T_W + i]) edge++;
    for (i = 0; i < ISO_T_H; i++)
        if (comp[i * ISO_T_W] || comp[i * ISO_T_W + ISO_T_W - 1]) edge++;
    if (edge) {
        printf("FAIL  %d border px written - patch clipped, counts unreliable\n", edge);
        fails++;
    }

    /* ---- negative control -------------------------------------------------
     * Every check above passed on the first attempt, which is exactly when a
     * checker deserves suspicion — a function hardwired to print PASS would
     * have produced identical output. Re-rasterise the same flat patch with a
     * one-pixel vertical error injected into half the tiles. That is the
     * smallest error the exactness claim forbids, so the coverage check must
     * reject it. */
    {
        int nz = 0;
        SDL_memset(comp, 0, (size_t)npx * sizeof(Uint32));
        for (ty = 0; ty < ISO_T_N; ty++)
            for (tx = 0; tx < ISO_T_N; tx++)
                iso_t_draw(&sc, hgt, tx, ty, 0xFFFFFFFFu, (tx + ty) & 1);
        for (i = 0; i < npx; i++)
            if (comp[i])
                nz++;
        if (nz == solo_sum) {
            printf("FAIL  negative control: a 1px tile offset went undetected\n");
            fails++;
        } else {
            printf("negative control (1px tile offset is rejected): PASS  "
                   "[%d px vs %d]\n", nz, solo_sum);
        }
    }

    /* ---- cases 2 and 3: random elevation ---------------------------------- */
    rng_seed(&rng, seed, STREAM_TERRAIN);
    for (i = 0; i < ISO_T_N * ISO_T_N; i++)
        hgt[i] = (int)rng_below(&rng, ELEV_MAX + 1);
    SDL_memset(comp, 0, (size_t)npx * sizeof(Uint32));

    for (ty = 0; ty < ISO_T_N; ty++)
        for (tx = 0; tx < ISO_T_N; tx++) {
            SDL_memset(solo, 0, (size_t)npx * sizeof(Uint32));
            iso_t_draw(&ss, hgt, tx, ty, 0xFFFFFFFFu, 0);
            for (i = 0; i < npx; i++)
                if (solo[i] && (Uint8)(tx + ty + 1) > band[i])
                    band[i] = (Uint8)(tx + ty + 1); /* nearest tile covering px */
        }
    SDL_memset(comp, 0, (size_t)npx * sizeof(Uint32));
    /* Draw in the renderer's order: back to front by band. */
    for (i = 0; i <= 2 * (ISO_T_N - 1); i++)
        for (tx = 0; tx < ISO_T_N; tx++) {
            ty = i - tx;
            if (ty < 0 || ty >= ISO_T_N)
                continue;
            iso_t_draw(&sc, hgt, tx, ty, (Uint32)(tx + ty + 1), 0);
        }

    /* case 2: no seams. Along any interior column the covered pixels must form
     * one unbroken run — a gap between a raised tile's side face and the top
     * face of the tile in front of it would show up as a hole here. */
    {
        int holes = 0, x, y;
        for (x = ISO_T_OX - ISO_HW; x < ISO_T_OX + ISO_HW; x++) {
            int first = -1, last = -1;
            for (y = 0; y < ISO_T_H; y++)
                if (comp[y * ISO_T_W + x]) {
                    if (first < 0) first = y;
                    last = y;
                }
            for (y = first; y >= 0 && y <= last; y++)
                if (!comp[y * ISO_T_W + x])
                    holes++;
        }
        printf("elevated: %d interior-column gap px (expect 0)\n", holes);
        if (holes) fails++;
    }

    /* case 3: depth order. Every pixel must end up owned by the nearest tile
     * that covers it — a farther tile painting over a nearer one is the classic
     * painter's-algorithm bug and is invisible until something tall exists. */
    {
        int wrong = 0;
        for (i = 0; i < npx; i++)
            if (band[i] && comp[i] != (Uint32)band[i])
                wrong++;
        printf("elevated: %d px owned by the wrong tile (expect 0)\n", wrong);
        if (wrong) fails++;
    }

    /* ---- case 4: the upscale is exactly nearest-neighbour -----------------
     * The logical image must survive scaling untouched. Comparing framebuffer
     * checksums across the resolution change cannot show that — the images are
     * different sizes — so check the actual property: every source pixel
     * becomes exactly its own s-by-s block at the right place, and every pixel
     * outside the scaled image is cleared.
     *
     * The destination is deliberately NOT a multiple of the source, because
     * that is the fullscreen case where a centring or margin bug would live. */
    {
        const int sw = 64, sh = 48, dw = 200, dh = 160;
        Uint32 *src = (Uint32 *)SDL_calloc((size_t)sw * sh, sizeof(Uint32));
        Uint32 *dst = (Uint32 *)SDL_calloc((size_t)dw * dh, sizeof(Uint32));
        SDL_Surface ssrc, sdst;
        int s, x, y;

        if (src && dst) {
            fake_surface(&ssrc, src, sw, sh);
            fake_surface(&sdst, dst, dw, dh);
            for (i = 0; i < sw * sh; i++)
                src[i] = (Uint32)(i * 2654435761u) | 1u; /* distinct and non-zero */

            for (s = 1; s <= 3; s++) {
                int bad = 0, dirt = 0;
                int ox = (dw - sw * s) / 2, oy = (dh - sh * s) / 2;
                SDL_memset(dst, 0xAB, (size_t)dw * dh * sizeof(Uint32)); /* pre-dirty */
                blit_scale(&ssrc, &sdst, s);
                for (y = 0; y < dh; y++)
                    for (x = 0; x < dw; x++) {
                        Uint32 got = dst[y * dw + x];
                        if (x >= ox && x < ox + sw * s && y >= oy && y < oy + sh * s) {
                            if (got != src[((y - oy) / s) * sw + (x - ox) / s])
                                bad++;
                        } else if (got) {
                            dirt++; /* margin must be cleared, not left stale */
                        }
                    }
                printf("upscale x%d: %d wrong px, %d stale margin px (expect 0, 0)\n",
                       s, bad, dirt);
                if (bad || dirt) fails++;
            }
        } else {
            printf("FAIL  out of memory in upscale check\n");
            fails++;
        }
        SDL_free(src);
        SDL_free(dst);
    }

    SDL_free(comp);
    SDL_free(solo);
    SDL_free(band);
    printf("\n%s (%d checks failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

static int popcount5(Uint8 bits)
{
    int n = 0, i;
    for (i = 0; i < FONT_W; i++)
        if (bits & (1u << i))
            n++;
    return n;
}

/* Renders the whole supported charset with the real stride into one canvas
 * and with an off-by-one stride into a second, then compares each against a
 * pixel count taken straight from FONT_5X7 — not an eyeball, per Phase 03's
 * DoD. The negative control passes when the corrupted render's count does
 * NOT match: that's the proof the checker has teeth, not just a second look
 * at the path that was already going to pass. */
static int font_selftest(const char *shot_path)
{
    const int cols = 16;
    const int rows = (FONT_GLYPHS + cols - 1) / cols;
    const int glyph_w = (FONT_W + 1) * FONT_SCALE;
    const int glyph_h = (FONT_H + 1) * FONT_SCALE;
    const int cw = cols * glyph_w;
    const int ch = rows * glyph_h;
    char text[FONT_GLYPHS + FONT_GLYPHS / cols + 1];
    Uint32 *px  = (Uint32 *)SDL_calloc((size_t)cw * ch, sizeof(Uint32));
    Uint32 *bad = (Uint32 *)SDL_calloc((size_t)cw * ch, sizeof(Uint32));
    SDL_Surface *s, *sb;
    Uint32 white;
    int i, r, n = 0, expect = 0, actual = 0, actual_bad = 0, fails = 0;

    if (!px || !bad) {
        printf("FAIL  out of memory\n");
        SDL_free(px); SDL_free(bad);
        return 1;
    }
    s  = SDL_CreateRGBSurfaceWithFormatFrom(px,  cw, ch, 32, cw * 4, SDL_PIXELFORMAT_RGB888);
    sb = SDL_CreateRGBSurfaceWithFormatFrom(bad, cw, ch, 32, cw * 4, SDL_PIXELFORMAT_RGB888);
    if (!s || !sb) {
        printf("FAIL  SDL_CreateRGBSurfaceWithFormatFrom\n");
        if (s) SDL_FreeSurface(s);
        if (sb) SDL_FreeSurface(sb);
        SDL_free(px); SDL_free(bad);
        return 1;
    }
    white = SDL_MapRGB(s->format, 0xff, 0xff, 0xff);

    printf("=== bitmap font, %d glyphs, %dx%d canvas ===\n", FONT_GLYPHS, cw, ch);

    /* Good render goes through the real public API (draw_text_shadow), not
     * draw_glyph directly — this is the path a HUD would actually call.
     * The reference count comes straight from FONT_5X7, independent of any
     * drawing code. */
    for (i = 0; i < FONT_GLYPHS; i++) {
        if (i && i % cols == 0)
            text[n++] = '\n';
        text[n++] = (char)(FONT_FIRST + i);
        for (r = 0; r < FONT_H; r++)
            expect += popcount5(FONT_5X7[i * FONT_STRIDE + r]);
    }
    text[n] = '\0';
    draw_text_shadow(s, 0, 0, text, white);

    /* Same glyphs, same grid positions, but one row misaligned via a
     * corrupted stride. draw_text has no stride knob, so this goes through
     * draw_glyph directly, same as font_selftest's own comment above says. */
    for (i = 0; i < FONT_GLYPHS; i++) {
        int gx = (i % cols) * glyph_w;
        int gy = (i / cols) * glyph_h;
        draw_glyph(sb, gx, gy, FONT_FIRST + i, white, FONT_STRIDE - 1);
    }

    /* Each font px is a FONT_SCALE x FONT_SCALE block on the canvas. Counting
     * exactly `white` rather than non-zero also keeps draw_text_shadow's black
     * underlay out of the number — it is drawn first and only survives where
     * the glyph does not cover it. */
    expect *= FONT_SCALE * FONT_SCALE;
    for (i = 0; i < cw * ch; i++) {
        if (px[i]  == white) actual++;
        if (bad[i] == white) actual_bad++;
    }

    printf("expected %d lit px from the glyph table, rendered %d: %s\n",
           expect, actual, expect == actual ? "PASS" : "FAIL");
    if (expect != actual) fails++;

    printf("negative control (stride off by one): rendered %d instead of %d: %s\n",
           actual_bad, expect, actual_bad != expect ? "PASS (caught)" : "FAIL (missed)");
    if (actual_bad == expect) fails++;

    if (shot_path) {
        SDL_SaveBMP(s, shot_path);
        printf("wrote %s\n", shot_path);
    }

    SDL_FreeSurface(s);
    SDL_FreeSurface(sb);
    SDL_free(px);
    SDL_free(bad);
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails;
}

/* Structural invariants for building placement.
 *
 * Absolute, not relative — design/Toolchain Setup.md records that every
 * *relative* region test once passed on a partition covering 5 of 1585 tiles,
 * because they all checked counts against counts. So these check properties of
 * the world itself: footprints are really solid, really disjoint, really
 * indexed, and really approachable. */
static int village_selftest(Uint64 seed, int verbose, int *total)
{
    Game *g = (Game *)SDL_malloc(sizeof(Game));
    Rngs rngs;
    int i, x, y, bad = 0, unreachable = 0;

    if (!g)
        return 1;
    rngs_init(&rngs, seed);
    (void)game_init(g, &rngs);

    for (i = 0; i < g->w.bld_count; i++) {
        const Building *b = &g->w.bld[i];
        int open_neighbours = 0;

        for (y = b->y; y < b->y + b->h; y++)
            for (x = b->x; x < b->x + b->w; x++) {
                if (!g->w.solid[y][x])
                    bad++;                       /* footprint must be solid */
                if (g->w.bld_at[y][x] != i + 1)
                    bad++;                       /* and indexed to this building */
            }
        /* Approachable: at least one open tile touching the footprint. A house
         * entirely embedded in rock is not a house, it is a decoration. */
        for (y = b->y - 1; y <= b->y + b->h; y++)
            for (x = b->x - 1; x <= b->x + b->w; x++)
                if (!solid_at(&g->w, x, y))
                    open_neighbours++;
        if (open_neighbours == 0)
            unreachable++;
    }

    /* Disjointness, checked over the whole map rather than pairwise, so an
     * overlap cannot hide in an index we forgot to compare. */
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            if (g->w.bld_at[y][x] && !g->w.solid[y][x])
                bad++;

    if (verbose)
        printf("  seed %-6.0f buildings %2d  bad tiles %d  walled-in %d  %s\n",
               (double)seed, g->w.bld_count, bad, unreachable,
               (bad || unreachable || g->w.bld_count == 0) ? "FAIL" : "PASS");
    *total += g->w.bld_count;
    if (g->w.bld_count == 0)
        bad++;
    SDL_free(g);
    return (bad || unreachable) ? 1 : 0;
}

/* Negative control. Everything above passing first time is exactly when a
 * checker deserves suspicion, so hand-build the two failures it claims to
 * detect and confirm it detects them. */
static int village_negative_test(void)
{
    World *w = (World *)SDL_calloc(1, sizeof(World));
    int fails = 0, x, y, caught;

    if (!w)
        return 1;

    /* (a) a footprint that is indexed but not solid. */
    w->bld_count = 1;
    w->bld[0].x = 5; w->bld[0].y = 5; w->bld[0].w = 2; w->bld[0].h = 2;
    for (y = 5; y < 7; y++)
        for (x = 5; x < 7; x++) {
            w->solid[y][x] = 1;
            w->bld_at[y][x] = 1;
        }
    w->solid[5][5] = 0; /* the injected fault */
    caught = 0;
    for (y = 0; y < WORLD_H; y++)
        for (x = 0; x < WORLD_W; x++)
            if (w->bld_at[y][x] && !w->solid[y][x])
                caught++;
    printf("negative control (non-solid footprint tile detected): %s\n",
           caught ? "PASS" : "FAIL");
    if (!caught) fails++;

    /* (b) a building walled in on every side. */
    SDL_memset(w->solid, 1, sizeof(w->solid));
    caught = 0;
    for (y = 4; y <= 7; y++)
        for (x = 4; x <= 7; x++)
            if (!solid_at(w, x, y))
                caught++;
    printf("negative control (walled-in building detected): %s\n",
           caught == 0 ? "PASS" : "FAIL");
    if (caught != 0) fails++;

    SDL_free(w);
    return fails;
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
    SDL_Surface *back = NULL; /* logical-resolution backbuffer, or NULL */
    SDL_Surface *draw;        /* whichever of the two the renderer writes into */
    void *back_px = NULL;
    int scale;
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
    int overlay = 0, grid = 0, dirty = 1, title_dirty = 1, fullscreen = 0;
    int seed = arg_int(argc, argv, "--seed", 1);
#if WAYFARER_PERF
    Perf pf;
    int show_perf = arg_flag(argc, argv, "--perf");
#endif
#if WAYFARER_SELFTEST
    const char *shot = arg_val(argc, argv, "--shot");
    /* Start with F1 already held down, so a scripted screenshot can show
     * terrain and elevation without fog hiding most of it. */
    overlay = arg_flag(argc, argv, "--overlay");
    /* Same idea for F3, so the tuning HUD can be screenshotted over real
     * fogged terrain without a human at the keyboard. */
    tune_show = arg_flag(argc, argv, "--tune");
#endif

#if WAYFARER_SELFTEST
    {
        int ms = arg_int(argc, argv, "--audio-test", 0);
        if (ms > 0)
            return audio_selftest(argc, argv, ms);
        if (arg_flag(argc, argv, "--rng-test"))
            return rng_selftest((Uint64)arg_int(argc, argv, "--seed", 1));
        if (arg_flag(argc, argv, "--iso-test"))
            return iso_selftest((Uint64)arg_int(argc, argv, "--seed", 1));
        if (arg_flag(argc, argv, "--font-test"))
            return font_selftest(arg_val(argc, argv, "--shot"));
        if (arg_flag(argc, argv, "--village-test")) {
            int n = arg_int(argc, argv, "--seeds", 20);
            int base = arg_int(argc, argv, "--seed", 1);
            int s, bad = 0, total = 0;
            printf("=== building placement, %d seeds ===\n", n);
            for (s = 0; s < n; s++)
                bad += village_selftest((Uint64)(base + s), 1, &total);
            printf("\nmean %d buildings per world\n", total / (n > 0 ? n : 1));
            bad += village_negative_test();
            printf("%s (%d failures across %d seeds)\n", bad ? "FAIL" : "PASS", bad, n);
            return bad ? 1 : 0;
        }
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

    scale = arg_int(argc, argv, "--scale", 0);
    if (scale < 1)
        scale = pick_scale();
    win = SDL_CreateWindow("Wayfarer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           LOGICAL_W * scale, LOGICAL_H * scale, SDL_WINDOW_SHOWN);
    if (!win) {
        SDL_Quit();
        return 2;
    }

    /* Needs the window's pixel format, so it cannot be built before this point.
     * Allocated even at scale 1, where present() copies 1:1 — F11 can raise the
     * scale at any moment, and a rendering path that only exists above a
     * threshold is a path that only gets tested above one. */
    {
        SDL_Surface *wfb = SDL_GetWindowSurface(win);
        if (wfb && wfb->format->BytesPerPixel == 4)
            back = backbuffer_new(wfb, &back_px);
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
#if WAYFARER_SELFTEST
                case SDLK_F3: /* live tuning overlay */
                    tune_show = !tune_show;
                    break;
                case SDLK_TAB:
                    if (tune_show)
                        tune_row = (tune_row + 1) % TUNE_ROWS;
                    break;
                case SDLK_MINUS:
                    if (tune_show)
                        tune_adjust(-1);
                    break;
                case SDLK_EQUALS:
                    if (tune_show)
                        tune_adjust(1);
                    break;
#endif
                case SDLK_F11:
                    /* Borderless fullscreen. On a display that is tall enough
                     * for the doubled image but not for the window chrome as
                     * well, this is the only way to get the intended pixel
                     * scale — and it is where a pixel-art game wants to be. */
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen
                                            ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
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

        draw = back ? back : fb;
#if WAYFARER_PERF
        t_a = SDL_GetPerformanceCounter();
#endif
        if (grid) {
            /* Regenerate only when dirty — 12 worlds per frame would crawl —
             * but always re-present, so a repaint after the surface is
             * invalidated does not leave a blank window. */
            if (dirty) {
                render_grid(draw, seed);
                dirty = 0;
            }
        } else {
            camera_follow(&game, draw->w, draw->h);
            render(draw, &game, overlay);
        }
#if WAYFARER_SELFTEST
        /* After render, before present: the overlay reads as a HUD over the
         * world, which is also the only honest test of whether the shadowed
         * text stays legible over arbitrary terrain. */
        if (tune_show)
            tune_draw(draw);
#endif
#if WAYFARER_PERF
        t_b = SDL_GetPerformanceCounter();
#endif
        present(win, fb, back);
#if WAYFARER_PERF
        t_c = SDL_GetPerformanceCounter();
        ms_render  = (double)(t_b - t_a) / perf * 1000.0;
        ms_present = (double)(t_c - t_b) / perf * 1000.0;
#endif

#if WAYFARER_SELFTEST
        /* Scriptable screenshot on the final frame. Grabbing the window from
         * outside is unreliable here (see the SetForegroundWindow trap in
         * Handover.md); saving the surface we just drew is exact, and every
         * remaining slice of the isometric work needs to be looked at. */
        if (shot && limit && frame + 1 >= limit)
            SDL_SaveBMP(draw, shot); /* the logical image, not the upscale */
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
    /* Reported against the LOGICAL size — that is what render() actually filled,
     * and quoting the window size would make the pixel ratio meaningless. */
    if (show_perf)
        perf_report(&pf, back ? back->w : LOGICAL_W, back ? back->h : LOGICAL_H,
                    scale);
#endif

    if (dev)
        SDL_CloseAudioDevice(dev);
    if (back)
        SDL_FreeSurface(back);
    SDL_free(back_px);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
