#include <xtl.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include "SDL_test_common.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---- Config ----
#define DESIGN_W 640              // target "look"
#define DESIGN_H 480
#define NUM_STARS 1000
#define MAX_DEPTH 2000.0f         // world units
#define BASE_SPEED 900.0f         // units/sec at 60 Hz (tweak to taste)

// ---- Globals ----
static SDLTest_CommonState* state;
static int done = 0;
static int refresh_hz = 60;

typedef struct {
    float x, y, z;   // 3D pos
    Uint8 r, g, b;   // color
} Star;

static Star stars[NUM_STARS];

static Uint32 lastTicks = 0;

// ---- Renderer / logical size ----
static void ConfigureRenderer(SDL_Renderer* r) {
    int outW = 0, outH = 0;
    SDL_GetRendererOutputSize(r, &outW, &outH);
    if (outW <= 0 || outH <= 0) { outW = DESIGN_W; outH = DESIGN_H; }

    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.refresh_rate > 0)
        refresh_hz = dm.refresh_rate;

    float sx = (float)outW / (float)DESIGN_W;
    float sy = (float)outH / (float)DESIGN_H;
    float s = (sx < sy) ? sx : sy;

    int logicalW = DESIGN_W, logicalH = DESIGN_H;
    if (s < 1.0f) {                        // downscale design to fit
        logicalW = (int)(DESIGN_W * s);
        logicalH = (int)(DESIGN_H * s);
        if (logicalW < 320) logicalW = 320;
        if (logicalH < 200) logicalH = 200;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize(r, logicalW, logicalH);
    SDL_RenderSetIntegerScale(r, (s >= 1.0f) ? SDL_TRUE : SDL_FALSE); // only upscale
}

// ---- Stars ----
static void InitStar(SDL_Renderer* r, Star* s) {
    int W, H; SDL_RenderGetLogicalSize(r, &W, &H);
    if (W == 0 || H == 0) { W = DESIGN_W; H = DESIGN_H; }

    s->x = (float)(rand() % (W * 2) - W);
    s->y = (float)(rand() % (H * 2) - H);
    s->z = (float)(rand() % (int)MAX_DEPTH + 1);
    s->r = (Uint8)(rand() % 256);
    s->g = (Uint8)(rand() % 256);
    s->b = (Uint8)(rand() % 256);
}

static void InitStars(SDL_Renderer* r) {
    for (int i = 0; i < NUM_STARS; ++i) InitStar(r, &stars[i]);
}

static void UpdateStars(float dt, SDL_Renderer* r) {
    float rateScale = (refresh_hz > 0 ? (refresh_hz / 60.0f) : 1.0f);
    float dz = BASE_SPEED * dt * rateScale;     // units to move this frame

    for (int i = 0; i < NUM_STARS; ++i) {
        stars[i].z -= dz;
        if (stars[i].z <= 0.0f) {
            InitStar(r, &stars[i]);
            stars[i].z = MAX_DEPTH;
        }
    }
}

// ---- Drawing ----
static void DrawStars(SDL_Renderer* r) {
    int W, H; SDL_RenderGetLogicalSize(r, &W, &H);
    // projection scales with logical size so it looks right at any res
    const float projX = 0.35f * (float)W;
    const float projY = 0.35f * (float)H;

    for (int i = 0; i < NUM_STARS; ++i) {
        // 3D -> 2D perspective
        float invz = 1.0f / (stars[i].z + 1.0f);
        int sx = (int)(stars[i].x * projX * invz + W * 0.5f);
        int sy = (int)(stars[i].y * projY * invz + H * 0.5f);

        // size from depth (clamped)
        int size = (int)((1.0f - stars[i].z / MAX_DEPTH) * 5.0f);
        if (size < 1) size = 1; if (size > 6) size = 6;

        // quick clip to avoid backend asserts if coords go wild
        if ((unsigned)sx >= (unsigned)W || (unsigned)sy >= (unsigned)H) continue;

        SDL_SetRenderDrawColor(r, stars[i].r, stars[i].g, stars[i].b, 255);
        SDL_Rect rc = { sx - size / 2, sy - size / 2, size, size };
        SDL_RenderFillRect(r, &rc);
    }
}

// ---- Frame loop ----
static void loop(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) SDLTest_CommonEvent(state, &e, &done);

    Uint32 now = SDL_GetTicks();
    float dt = (lastTicks ? (now - lastTicks) : 16) / 1000.0f; // seconds
    lastTicks = now;

    for (int i = 0; i < state->num_windows; ++i) {
        SDL_Renderer* r = state->renderers[i];
        if (!r || state->windows[i] == NULL) continue;

        UpdateStars(dt, r);

        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);

        DrawStars(r);

        SDL_RenderPresent(r);
    }
}

// ---- Entry ----
int main(int argc, char* argv[]) {
    srand((unsigned int)time(NULL));

    state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
    if (!state) return 1;
    if (!SDLTest_CommonInit(state)) { SDLTest_CommonQuit(state); return 2; }

    // Configure each renderer for the actual backbuffer and init content
    for (int i = 0; i < state->num_windows; ++i) {
        SDL_Renderer* r = state->renderers[i];
        if (!r) continue;
        ConfigureRenderer(r);
    }
    // Use renderer 0 for initial seeding (all use the same logical size)
    if (state->num_windows > 0 && state->renderers[0])
        InitStars(state->renderers[0]);
    else
        InitStars(NULL);

    done = 0;
    lastTicks = 0;
    while (!done) loop();

    SDLTest_CommonQuit(state);
    return 0;
}
