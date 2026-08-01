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

#define BOX 64
#define TRAVEL (WIN_W - BOX)

#define AUDIO_RATE     48000
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES  1024 /* frames per callback; ~21 ms at 48 kHz */
#define TONE_HZ        440.0
#define TONE_AMP       0.20f

#define TWO_PI 6.283185307179586

/* ------------------------------------------------------------------ RNG -- */
/* xorshift32. Every procedural generator in this project draws from a seeded
 * instance so that any bad output is reproducible from its seed alone
 * (--seed N). Built before the generators, deliberately: retrofitting
 * determinism onto a generator that already exists never works. */

typedef struct {
    Uint32 s;
} Rng;

static void rng_seed(Rng *r, Uint32 seed)
{
    r->s = seed ? seed : 1u; /* zero is a fixed point of xorshift; avoid it */
}

static Uint32 rng_next(Rng *r)
{
    Uint32 x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

/* Uniform in [-1, 1). Takes the top 24 bits: the low bits of xorshift are the
 * weakest, and 24 bits is already finer than float can represent here. */
static float rng_bipolar(Rng *r)
{
    return (float)(rng_next(r) >> 8) * (1.0f / 8388608.0f) - 1.0f;
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

/* ------------------------------------------------------------- graphics -- */

/* Deterministic triangle wave: frame number alone fixes the box position, so
 * a screenshot at a known frame is reproducible. */
static int box_x(int frame)
{
    int phase = frame % (TRAVEL * 2);
    return phase < TRAVEL ? phase : TRAVEL * 2 - phase;
}

/* Window surfaces are plain memory — never RLE-encoded — so SDL_MUSTLOCK is
 * false for them and no lock/unlock is needed. Caller guarantees 32bpp. */
static void fill_rect(SDL_Surface *s, int x, int y, int w, int h, Uint32 colour)
{
    int iy, ix;
    for (iy = 0; iy < h; iy++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (y + iy) * s->pitch);
        for (ix = 0; ix < w; ix++)
            row[x + ix] = colour;
    }
}

/* ------------------------------------------------------------- selftest -- */
#if WAYFARER_SELFTEST

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
    rng_seed(&a.rng, (Uint32)arg_int(argc, argv, "--seed", 1));

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
    SDL_AudioSpec have;
    SDL_AudioDeviceID dev;
    int limit = arg_int(argc, argv, "--frames", 0);
    int frame = 0;
    int running = 1;

#if WAYFARER_SELFTEST
    {
        int ms = arg_int(argc, argv, "--audio-test", 0);
        if (ms > 0)
            return audio_selftest(argc, argv, ms);
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
    rng_seed(&audio.rng, (Uint32)arg_int(argc, argv, "--seed", 1));

    /* Silence is an acceptable degraded mode; failing to launch is not. */
    dev = 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        dev = audio_open(&audio, &have);
        if (dev)
            SDL_PauseAudioDevice(dev, 0);
    }

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
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

        fill_rect(fb, 0, 0, fb->w, fb->h, SDL_MapRGB(fb->format, 0x10, 0x14, 0x1c));
        fill_rect(fb, box_x(frame), (WIN_H - BOX) / 2, BOX, BOX,
                  SDL_MapRGB(fb->format, 0xe0, 0x64, 0x28));

        SDL_UpdateWindowSurface(win);

        /* Crude cap; real frame pacing arrives with the game loop. */
        SDL_Delay(16);

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
