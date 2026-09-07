/*
  Simple WAV player with fullscreen visual (progress bar) for Xbox/SDL
*/
#include <xtl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "SDL.h"  // use one SDL header

static struct {
    SDL_AudioSpec spec;
    Uint8* sound;      // wave data
    Uint32  soundlen;   // total bytes
    Uint32  soundpos;   // current play position (bytes)
} wave;

static SDL_AudioDeviceID device = 0;

// ---- Video globals ----
static SDL_Window* gWin = NULL;
static SDL_Renderer* gRen = NULL;
static int done = 0;

// ---- Audio callback ----
static void SDLCALL fillerup(void* userdata, Uint8* stream, int len)
{
    (void)userdata;
    Uint8* waveptr = wave.sound + wave.soundpos;
    int waveleft = (int)(wave.soundlen - wave.soundpos);

    while (waveleft <= len) {
        SDL_memcpy(stream, waveptr, waveleft);
        stream += waveleft;
        len -= waveleft;
        waveptr = wave.sound;
        waveleft = (int)wave.soundlen;
        wave.soundpos = 0; // loop
    }
    SDL_memcpy(stream, waveptr, len);
    wave.soundpos += (Uint32)len;
}

// ---- Audio helpers ----
static void close_audio(void)
{
    if (device) { SDL_CloseAudioDevice(device); device = 0; }
}

static void open_audio(void)
{
    device = SDL_OpenAudioDevice(NULL, 0, &wave.spec, NULL, 0);
    if (!device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open audio: %s", SDL_GetError());
        SDL_FreeWAV(wave.sound);
        SDL_Quit();
        exit(2);
    }
    SDL_PauseAudioDevice(device, 0); // start playback
}

static void reopen_audio(void)
{
    close_audio();
    open_audio();
}

// ---- Video helpers ----
static void create_fullscreen_renderer(void)
{
    // Create a true fullscreen window; avoids desktop semantics
    if (SDL_CreateWindowAndRenderer(0, 0, SDL_WINDOW_FULLSCREEN, &gWin, &gRen) != 0) {
        // Fallback to fullscreen-desktop if needed
        gWin = SDL_CreateWindow("WAV Player",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            640, 480, SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (gWin) gRen = SDL_CreateRenderer(gWin, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    }
    if (!gWin || !gRen) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Video init failed: %s", SDL_GetError());
        // Not fatal for audio-only, but then you won't see visuals
    }
    else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    }
}

static void draw_progress_bar(void)
{
    if (!gRen) return;

    int w = 0, h = 0;
    SDL_GetRendererOutputSize(gRen, &w, &h);

    // background
    SDL_SetRenderDrawColor(gRen, 0, 0, 0, 255);
    SDL_RenderClear(gRen);

    // progress (bytes based; good enough for a visual)
    float pct = (wave.soundlen > 0) ? (wave.soundpos / (float)wave.soundlen) : 0.0f;
    if (pct < 0.0f) pct = 0.0f; if (pct > 1.0f) pct = 1.0f;

    const int barH = h / 24;     // ~4% of screen height
    const int barW = (int)(w * 0.8f);
    const int barX = (int)(w * 0.1f);
    const int barY = (int)(h * 0.85f);

    // bar outline (optional)
    SDL_Rect outline = { barX, barY, barW, barH };
    SDL_SetRenderDrawColor(gRen, 40, 40, 40, 255);
    SDL_RenderDrawRect(gRen, &outline);

    // filled part
    SDL_Rect fill = { barX, barY, (int)(barW * pct), barH };
    SDL_SetRenderDrawColor(gRen, 255, 255, 255, 255);
    SDL_RenderFillRect(gRen, &fill);

    SDL_RenderPresent(gRen);
}

// ---- Main loop ----
int main(int argc, char* argv[])
{
    char filename[4096];

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    // We want audio + events + video so we can draw something
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_VIDEO) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }

    if (argc > 1) SDL_strlcpy(filename, argv[1], sizeof(filename));
    else          SDL_strlcpy(filename, "D:\\sample.wav", sizeof(filename));

    if (SDL_LoadWAV(filename, &wave.spec, &wave.sound, &wave.soundlen) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't load %s: %s", filename, SDL_GetError());
        SDL_Quit();
        return 1;
    }

    wave.spec.callback = fillerup;
    wave.spec.userdata = NULL;
    wave.soundpos = 0;

    // Bring up fullscreen video so you don't see the dashboard
    create_fullscreen_renderer();

    open_audio();

    // Basic event/render loop
    while (!done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) done = 1;
            if ((ev.type == SDL_AUDIODEVICEADDED && !ev.adevice.iscapture) ||
                (ev.type == SDL_AUDIODEVICEREMOVED && !ev.adevice.iscapture && ev.adevice.which == device)) {
                reopen_audio();
            }
            // Escape/back: let user quit (optional; depends on your input setup)
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) done = 1;
        }

        // Draw a simple visual so the screen isn’t the dashboard
        draw_progress_bar();

        SDL_Delay(16); // ~60 fps visuals, audio runs independently
    }

    close_audio();
    if (gRen) SDL_DestroyRenderer(gRen);
    if (gWin) SDL_DestroyWindow(gWin);
    SDL_FreeWAV(wave.sound);
    SDL_Quit();
    return 0;
}
