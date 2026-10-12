/*
 * edhar-clock: screensaver of the great clock at the Concent of Saunt Edhar, from "Anathem".
 *
 * The chancel of the Mynster, seen as if cut through. The meteorite that drives the
 * clock hangs on its chain in the middle and sinks through the day. Four weights of
 * black stone ride on rails up the four pillars and tell the date: a cube for the
 * year, an octahedron for the decade, a dodecahedron for the century and an
 * icosahedron for the millennium. Everything is set from the real time and date,
 * and at noon four fraas wind the clock, which is the aut of Provener.
 *
 *   edhar-clock            full screen on every display; any key or mouse movement quits
 *   edhar-clock --window   in a window; Esc or Q quits
 *   EdharClock.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define ART_SECTION ".section .rdata,\"dr\"\n"
#else
#define ART_SECTION ".section .rodata\n"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".global art_" #name "\nart_" #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".global art_" #name "_end\nart_" #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(chancel, "chancel.jpg") ART(chain, "chain.png") ART(meteorite, "meteorite.png") ART(cube, "cube.png")
ART(octa, "octahedron.png") ART(dodeca, "dodecahedron.png") ART(icosa, "icosahedron.png") ART(sphere, "sphere.png")
ART(dial, "dial.png") ART(hub, "hub.png") ART(banner, "banner.png")
ART(top1, "top1.png") ART(tophub, "tophub.png") ART(floor, "floor.jpg")

enum { SP_CHANCEL, SP_CHAIN, SP_METEORITE, SP_CUBE, SP_OCTA, SP_DODECA, SP_ICOSA, SP_SPHERE,
       SP_DIAL, SP_HUB, SP_BANNER, SP_TOP1, SP_TOPHUB, SP_FLOOR, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_chancel, art_chancel_end}, {art_chain, art_chain_end}, {art_meteorite, art_meteorite_end},
    {art_cube, art_cube_end}, {art_octa, art_octa_end}, {art_dodeca, art_dodeca_end}, {art_icosa, art_icosa_end},
    {art_sphere, art_sphere_end}, {art_dial, art_dial_end}, {art_hub, art_hub_end}, {art_banner, art_banner_end},
    {art_top1, art_top1_end}, {art_tophub, art_tophub_end}, {art_floor, art_floor_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0, opt_lapse = 1;
static double opt_shift = 0; /* seconds added to the real time, for --at */
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {20, 16, 14}, INK = {46, 32, 22}, RED = {186, 52, 34}, CHORD = {130, 44, 30};
static const Rgb IRON = {84, 76, 70}, WOOD = {190, 122, 62}, WOOD_END = {226, 170, 104}, GOLD = {222, 172, 70}, PAPER = {253, 246, 226};
static const Rgb NIGHT = {14, 22, 52};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    int n = r < 30 ? 32 : 64;
    vb_need(2 * n, 6 * n);
    int b = nv;
    for (int i = 0; i < n; i++) {
        double a = i * 2 * M_PI / n;
        vert(cx + (r + w / 2) * cos(a), cy + (r + w / 2) * sin(a), c);
        vert(cx + (r - w / 2) * cos(a), cy + (r - w / 2) * sin(a), c);
    }
    for (int i = 0; i < n; i++) {
        int a = b + 2 * i, d = b + 2 * ((i + 1) % n);
        ib[ni++] = a; ib[ni++] = a + 1; ib[ni++] = d; ib[ni++] = a + 1; ib[ni++] = d + 1; ib[ni++] = d;
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the time ----
 *
 * The clock is read off the real time and date. The meteorite is wound to the top at
 * Provener (noon, taking twenty minutes) and sinks through the next day, dropping
 * quickly at sunset and sunrise when it works the Day Gate. The four stone weights
 * creep up their rails and fall when their gate opens: the cube each year, the
 * octahedron each decade, the dodecahedron each century, the icosahedron each
 * millennium. The sphere takes over while the clock is being wound.
 *
 * Every so often a day is run through quickly so that all of this can be seen.
 */
#define WIND_SECS 1200.0  /* Provener: twenty minutes */
#define SUNSET (6 * 3600.0)   /* seconds after noon */
#define SUNRISE (18 * 3600.0)
static double anim_t, phase_t, lapse; /* lapse: seconds the show is running ahead of the real time */
static int lapsing;
static uint64_t seed_state;

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}

typedef struct {
    int year, mon, mday, hour, min; double sec;
    double noon;      /* seconds since the last noon */
    double yearfrac;  /* how far through the year, 0..1 */
} Now;
static Now now_;

static void read_time(void)
{
    double t = (double)time(NULL) + lapse + opt_shift;
    time_t whole = (time_t)floor(t);
    struct tm *tm = localtime(&whole);
    if (!tm) return;
    now_.year = tm->tm_year + 1900; now_.mon = tm->tm_mon; now_.mday = tm->tm_mday;
    now_.hour = tm->tm_hour; now_.min = tm->tm_min; now_.sec = tm->tm_sec + (t - floor(t));
    double sod = tm->tm_hour * 3600.0 + tm->tm_min * 60.0 + now_.sec;
    now_.noon = fmod(sod - 12 * 3600.0 + 86400.0, 86400.0);
    now_.yearfrac = (tm->tm_yday + sod / 86400.0) / 365.25;
    if (now_.yearfrac > 1) now_.yearfrac = 1;
}
static int winding(void) { return now_.noon < WIND_SECS; }
static double ramp(double t, double at, double len) { return t <= at ? 0 : t >= at + len ? 1 : (t - at) / len; }
/* how far down its travel the meteorite hangs, 0 top .. 1 bottom */
static double meteorite_drop(void)
{
    double t = now_.noon;
    if (t < WIND_SECS) { double u = t / WIND_SECS; return 1 - u * u * (3 - 2 * u); }
    return 0.76 * (t - WIND_SECS) / (86400.0 - WIND_SECS) + 0.12 * ramp(t, SUNSET, 60) + 0.12 * ramp(t, SUNRISE, 60);
}
/* how far down its screw the sphere is: it sinks while the clock is wound and is
 * drawn back up over the following two hours */
static double sphere_drop(void)
{
    double t = now_.noon;
    return t < WIND_SECS ? t / WIND_SECS : 1 - ramp(t, WIND_SECS, 7200);
}
/* how far up its rail each stone weight is, 0 bottom .. 1 top */
static double weight_rise(int i)
{
    static const int SPAN[4] = {1, 10, 100, 1000};
    return (now_.year % SPAN[i] + now_.yearfrac) / SPAN[i];
}

static void update(double dt)
{
    anim_t += dt;
    phase_t += dt * opt_speed;
    if (!lapsing) {
        lapse = 0;
        if (opt_lapse && phase_t > 50) { lapsing = 1; phase_t = 0; }
    } else {
        /* a day in half a minute, slowing for the things worth watching */
        double t = now_.noon, rate = 5200;
        if (t < WIND_SECS) rate = 100;
        else if (fabs(t - SUNSET - 30) < 100 || fabs(t - SUNRISE - 30) < 100) rate = 22;
        lapse += rate * dt * opt_speed;
        if (lapse >= 86400) { lapse = 0; lapsing = 0; phase_t = 0; }
    }
    read_time();
}

/* ---- drawing ---- */
static double unit;
static const double PILLAR[4] = {0.0763, 0.343, 0.6563, 0.9222}; /* where the pillars stand in the picture */
static const char *MONTH[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST",
                                "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

/* the chain: one picture of a few links, repeated from y0 down to y1 */
static void draw_chain(double x, double y0, double y1, double wide)
{
    flush(ren);
    if (!cur_tex || !cur_tex[SP_CHAIN]) return;
    double th = wide * sprite[SP_CHAIN].h / sprite[SP_CHAIN].w;
    /* hang the pattern from the bottom so the links move with the weight */
    for (double y = y1 - th; y > y0 - th; y -= th) {
        double top = y < y0 ? y0 : y, cut = (top - y) / th;
        SDL_Rect src = {0, (int)(cut * sprite[SP_CHAIN].h), sprite[SP_CHAIN].w, (int)((1 - cut) * sprite[SP_CHAIN].h)};
        SDL_FRect dst = {(float)(x - wide / 2), (float)top, (float)wide, (float)(y + th - top)};
        SDL_RenderCopyF(ren, cur_tex[SP_CHAIN], &src, &dst);
    }
}

static void plaque(double cx, double cy, const char *str, double s)
{
    double bw = text_width(str, s) + s * 9, bh = s * 11;
    rect(cx - bw / 2 - s * 0.7, cy - bh / 2 - s * 0.7, bw + s * 1.4, bh + s * 1.4, col(INK, 1));
    rect(cx - bw / 2, cy - bh / 2, bw, bh, col(PAPER, 1));
    text(str, cx, cy - s * 3, s, 1.0, col(INK, 1));
}

/* a hand of the dial, turned so many twelfths of a circle */
static void hand(double cx, double cy, double turn, double len, double wide, Rgb c)
{
    double a = turn * 2 * M_PI, sx = sin(a), cy2 = -cos(a);
    line(cx - sx * len * 0.16, cy - cy2 * len * 0.16, cx + sx * len, cy + cy2 * len, wide + unit * 0.3, col(INK, 1));
    line(cx - sx * len * 0.16, cy - cy2 * len * 0.16, cx + sx * len, cy + cy2 * len, wide, col(c, 1));
}

/* The capstan is seen from the side, so the circle the fraas walk is drawn as a flat
 * ellipse: (cx, cy) its middle on the floor, rx and ry its half-width and half-depth.
 * ang is where the end of a pole is on that circle; pole_y is the height of the poles
 * at the hub. Things on the far side are drawn before the hub (pass 0), things on the
 * near side after it (pass 1). */
static void draw_pole(double cx, double pole_y, double rx, double ry, double ang, double hub_half, int pass)
{
    if ((sin(ang) >= 0) != pass) return;
    double ex = cx + cos(ang) * rx, ey = pole_y + sin(ang) * ry, k = 1 + 0.12 * sin(ang);
    double bx = cx + cos(ang) * hub_half * 0.8, by = pole_y + sin(ang) * ry * hub_half / rx; /* where it leaves the hub */
    line(bx, by, ex, ey, unit * 1.15 * k, col(INK, 1));
    line(bx, by, ex, ey, unit * 0.7 * k, col(WOOD, 1));
    disc(ex, ey, unit * 0.35 * k, col(WOOD_END, 1), col(WOOD_END, 1));
}
/* a picture turned about its own middle; deg is clockwise, 0 leaves it upright */
static void pic_turned(int which, double x, double y, double tall, double deg)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    double w = tall * sprite[which].w / sprite[which].h;
    SDL_FRect dst = {(float)(x - w / 2), (float)(y - tall / 2), (float)w, (float)tall};
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, deg, NULL, SDL_FLIP_NONE);
}

/* Provener seen from straight above, in a round window: the hub, its four poles, and
 * the four fraas walking widdershins with their hands on them. A capstan only makes
 * sense from above, so this is where the people are shown. turn is how far round
 * they have gone; grow runs 0..1 as the window opens. */
static void draw_from_above(double cx, double cy, double R, double turn, double grow)
{
    if (grow <= 0) return;
    R *= grow < 1 ? 0.6 + 0.4 * grow : 1;
    SDL_Color white = {255, 255, 255, (Uint8)(255 * (grow < 1 ? grow : 1))};
    disc(cx + unit * 0.5, cy + unit * 0.8, R * 1.1, col(INK, 0.4), col(INK, 0.1));
    disc(cx, cy, R + unit * 1.3, col(INK, 1), col(INK, 1));
    disc(cx, cy, R + unit * 0.85, col(GOLD, 1), col(GOLD, 1));
    disc(cx, cy, R + unit * 0.3, col(INK, 1), col(INK, 1));
    /* the floor, cut round */
    flush(ren);
    int n = 64;
    vb_need(n + 1, 3 * n);
    vert(cx, cy, white);
    vb[0].tex_coord.x = vb[0].tex_coord.y = 0.5f;
    for (int i = 0; i < n; i++) {
        double a = i * 2 * M_PI / n;
        vert(cx + R * cos(a), cy + R * sin(a), white);
        vb[nv - 1].tex_coord.x = (float)(0.5 + 0.5 * cos(a)); vb[nv - 1].tex_coord.y = (float)(0.5 + 0.5 * sin(a));
    }
    for (int i = 0; i < n; i++) { ib[ni++] = 0; ib[ni++] = 1 + i; ib[ni++] = 1 + (i + 1) % n; }
    if (cur_tex && cur_tex[SP_FLOOR]) SDL_RenderGeometry(ren, cur_tex[SP_FLOOR], vb, nv, ib, ni);
    nv = ni = 0;
    if (grow < 1) return;
    double reach = R * 0.9, tall = R * 0.56;
    for (int i = 0; i < 4; i++) { /* the poles */
        double a = turn + i * M_PI / 2, ex = cx + cos(a) * reach, ey = cy - sin(a) * reach;
        line(cx + cos(a) * unit * 0.3 + unit * 0.3, cy - sin(a) * unit * 0.3 + unit * 0.4, ex + unit * 0.3, ey + unit * 0.4, R * 0.05, col(INK, 0.3));
        line(cx, cy, ex, ey, R * 0.062, col(INK, 1));
        line(cx, cy, ex, ey, R * 0.04, col(WOOD, 1));
    }
    pic_turned(SP_TOPHUB, cx, cy, R * 0.46, -turn * 180 / M_PI);
    for (int i = 0; i < 4; i++) { /* each fraa just behind his pole, near its end, facing the way he walks */
        double a = turn + i * M_PI / 2, px = cx + cos(a) * reach * 0.8, py = cy - sin(a) * reach * 0.8;
        double tx = -sin(a), ty = -cos(a); /* the way he is going, on screen */
        /* he sways a little from foot to foot as he pushes */
        double sway = sin(anim_t * 5.5 + i * 1.3) * 4, back = tall * (0.44 + 0.012 * sin(anim_t * 11 + i));
        disc(px - tx * back + unit * 0.3, py - ty * back + unit * 0.4, tall * 0.36, col(INK, 0.3), col(INK, 0));
        pic_turned(SP_TOP1, px - tx * back, py - ty * back, tall, -a * 180 / M_PI + sway);
    }
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_CHANCEL, 0, 0, w, h, 0, 1, 0, 0);

    /* the four stone weights on their rails */
    static const int WEIGHT[4] = {SP_CUBE, SP_OCTA, SP_DODECA, SP_ICOSA};
    static const char *GATE[4] = {"YEAR", "DECADE", "CENTURY", "MILLENNIUM"};
    double rail_top = h * 0.05, rail_bot = h * 0.775, wt = h * 0.125;
    for (int i = 0; i < 4; i++) {
        double x = w * PILLAR[i], rise = weight_rise(i);
        double y = rail_bot - wt * 0.5 - rise * (rail_bot - rail_top - wt * 1.2); /* middle of the weight */
        line(x - unit * 0.9, rail_top, x - unit * 0.9, rail_bot, unit * 0.34, col(IRON, 1));
        line(x + unit * 0.9, rail_top, x + unit * 0.9, rail_bot, unit * 0.34, col(IRON, 1));
        for (double q = rail_top + unit; q < rail_bot; q += unit * 5) line(x - unit * 1.3, q, x + unit * 1.3, q, unit * 0.3, col(IRON, 1));
        draw_chain(x, 0, y - wt * 0.42, unit * 1.5);
        disc(x + unit * 0.4, y + unit * 0.5, wt * 0.5, col(INK, 0.3), col(INK, 0));
        pic(WEIGHT[i], x, y, wt, 0.5, 0.5);
        plaque(x, h * 0.86, GATE[i], unit * 0.2);
    }

    /* the dial, in the bay on the left */
    double dx = w * (PILLAR[0] + PILLAR[1]) / 2, dy = h * 0.33, dr = h * 0.165;
    disc(dx + unit * 0.5, dy + unit * 0.8, dr * 1.04, col(INK, 0.35), col(INK, 0.1));
    pic(SP_DIAL, dx, dy, dr * 2, 0.5, 0.5);
    double hours = now_.hour % 12 + now_.min / 60.0 + now_.sec / 3600.0, mins = now_.min + now_.sec / 60.0;
    hand(dx, dy, hours / 12, dr * 0.42, unit * 0.9, INK);
    hand(dx, dy, mins / 60, dr * 0.64, unit * 0.6, INK);
    if (!lapsing) hand(dx, dy, floor(now_.sec) / 60, dr * 0.68, unit * 0.22, RED);
    disc(dx, dy, unit * 0.9, col(INK, 1), col(INK, 1));
    disc(dx, dy, unit * 0.45, col(GOLD, 1), col(GOLD, 1));
    ring(dx, dy, unit * 0.9, unit * 0.18, col(GOLD, 1));

    /* the sphere on its screw, in the bay on the right */
    double sx = w * (PILLAR[2] + PILLAR[3]) / 2, s_top = h * 0.06, s_tall = h * 0.11;
    double sy = s_top + h * 0.1 + sphere_drop() * h * 0.2;
    line(sx, 0, sx, h * 0.47, unit * 0.9, col(INK, 1));
    line(sx, 0, sx, h * 0.47, unit * 0.5, col(IRON, 1));
    for (double q = fmod(sy, unit * 1.4); q < h * 0.47; q += unit * 1.4) line(sx - unit * 0.45, q, sx + unit * 0.45, q + unit * 0.5, unit * 0.2, col(INK, 1));
    disc(sx + unit * 0.4, sy + unit * 0.5, s_tall * 0.5, col(INK, 0.3), col(INK, 0));
    pic(SP_SPHERE, sx, sy, s_tall, 0.5, 0.56);

    /* the meteorite on the main chain, down the middle */
    double cx = w * 0.5, floor_y = h * 0.93, hub_tall = h * 0.215;
    double m_top = h * 0.13, m_bot = floor_y - hub_tall - h * 0.15, mt = h * 0.15;
    double my = m_top + meteorite_drop() * (m_bot - m_top);
    draw_chain(cx, 0, my - mt * 0.4, unit * 2.0);
    pic(SP_METEORITE, cx, my + sin(anim_t * 0.7) * unit * 0.08, mt, 0.5, 0.5);

    /* the hub and its four poles, which go round while the clock is wound */
    int wind = winding();
    double rx = w * 0.158, ry = h * 0.052, ccy = floor_y - ry, pole_y = ccy - hub_tall * 0.5;
    double turn = wind ? anim_t * 0.6 : M_PI / 4;
    double hub_w = hub_tall * sprite[SP_HUB].w / sprite[SP_HUB].h * 0.8;
    for (int pass = 0; pass < 2; pass++) {
        if (pass) {
            disc(cx, ccy, hub_tall * 0.62, col(INK, 0.3), col(INK, 0));
            pic_part(SP_HUB, cx - hub_w / 2, ccy + hub_tall * 0.06 - hub_tall, hub_w, hub_tall, 0, 1, 0, 0);
        }
        for (int i = 0; i < 4; i++) draw_pole(cx, pole_y, rx, ry, -turn + i * M_PI / 2 + (pass ? 1e-6 : -1e-6), hub_w / 2, pass);
    }
    /* and the fraas who turn it, seen from above in a round window under the dial */
    double grow = wind ? fmin(now_.noon / 25.0, 1) * fmin((WIND_SECS - now_.noon) / 25.0, 1) : 0;
    if (grow > 0) {
        double ix = dx, iy = h * 0.735, iR = h * 0.175;
        for (double q = 0.08; q < 0.92; q += 0.07) { /* a dotted line from the hub to the window */
            double ax = ix + iR * 0.9 + (cx - hub_w * 0.6 - ix - iR * 0.9) * q, ay = iy + (pole_y - iy) * q;
            disc(ax, ay, unit * 0.28, col(INK, 0.8 * grow), col(INK, 0.8 * grow));
        }
        draw_from_above(ix, iy, iR, turn, grow < 1 ? grow : 1);
    }
    flush(r);

    /* night comes on: darkest at midnight, clear from sunrise to sunset */
    double t = now_.noon, dark = 0;
    if (t > SUNSET - 2400 && t < SUNRISE + 2400) {
        dark = fmin(ramp(t, SUNSET - 2400, 4800), 1 - ramp(t, SUNRISE - 2400, 4800));
        rect(0, 0, w, h, col(NIGHT, 0.58 * dark));
        flush(r);
    }

    /* what the time is, and what is going on */
    char l1[48], l2[64];
    snprintf(l1, sizeof l1, "%02d:%02d", now_.hour, now_.min);
    snprintf(l2, sizeof l2, "%d %s %d", now_.mday, MONTH[now_.mon], now_.year);
    if (wind) snprintf(l2, sizeof l2, "PROVENER: THE CLOCK IS WOUND");
    else if (fabs(t - SUNSET - 30) < 100) snprintf(l2, sizeof l2, "SUNSET: THE DAY GATE CLOSES");
    else if (fabs(t - SUNRISE - 30) < 100) snprintf(l2, sizeof l2, "SUNRISE: THE DAY GATE OPENS");
    else if (lapsing) snprintf(l2, sizeof l2, "A DAY GOES BY");
    double bw = w * 0.2, bh = bw * 0.5, bx = sx - bw / 2, by = h * 0.53, s1 = bw * 0.022, s2 = s1 * 0.44;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    double room = bw - cw * 2 - s1 * 3;
    if (text_width(l2, s2) > room) s2 *= room / text_width(l2, s2);
    text(l1, sx, by + bh * 0.25, s1, 1.0, col(INK, 1));
    text(l2, sx, by + bh * 0.25 + s1 * 7.4, s2, 1.0, col(CHORD, 1));
    flush(r);
    double fade = fmin(anim_t / 1.4, 1);
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("The Clock of Saunt Edhar", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0; phase_t = 0; lapse = 0; lapsing = 0;
    read_time();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "edhar-clock: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "edhar-clock: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Clock of Saunt Edhar",
        "The Clock of Saunt Edhar screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --no-lapse      only ever show the real time; never run a day through quickly\n"
        "  --at HH:MM      show the clock as it would be at this time of day\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "The Clock of Saunt Edhar", "The Clock of Saunt Edhar screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--no-lapse")) opt_lapse = 0;
        else if (!strcmp(a, "--at") && i + 1 < argc) {
            /* pretend it is this time of day (HH:MM) */
            const char *c = SDL_strchr(argv[++i], ':');
            time_t tt = time(NULL);
            struct tm *tm = localtime(&tt);
            if (tm) opt_shift = SDL_atoi(argv[i]) * 3600.0 + (c ? SDL_atoi(c + 1) : 0) * 60.0 - (tm->tm_hour * 3600.0 + tm->tm_min * 60.0 + tm->tm_sec);
        }
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "edhar-clock: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "edhar-clock: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
