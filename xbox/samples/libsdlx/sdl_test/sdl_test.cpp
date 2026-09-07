// OXDK + libSDLx smoke test.
//
// Builds with OXDK's clang/lld-link toolchain against pristine HyperEye
// libSDLx vendored at ../../third-party/libSDLx/. The program shows 8
// vertical color bars on the Xbox screen for ~5 seconds and then
// returns to the dashboard.

#include <xtl.h>
#include <SDL.h>
#include <stdio.h>

static void dbg(const char *msg)
{
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

static void dbgf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    dbg("[sdl_test] entered");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        dbgf("[sdl_test] SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    dbg("[sdl_test] SDL_Init OK");

    SDL_Surface *screen = SDL_SetVideoMode(640, 480, 16, SDL_SWSURFACE);
    if (!screen) {
        dbgf("[sdl_test] SDL_SetVideoMode failed: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    dbgf("[sdl_test] screen %p (%dx%d %dbpp)", screen,
         screen->w, screen->h, screen->format->BitsPerPixel);

    // 24bpp BGR source. Mirrors what IMG_Load returns for a typical BMP.
    SDL_Surface *source = SDL_CreateRGBSurface(SDL_SWSURFACE, 640, 480, 24,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0);
    if (!source) {
        dbgf("[sdl_test] SDL_CreateRGBSurface failed: %s", SDL_GetError());
        SDL_Quit();
        return 3;
    }
    dbgf("[sdl_test] source %p (%dx%d %dbpp)", source,
         source->w, source->h, source->format->BitsPerPixel);

    // 8 vertical color bars.
    if (SDL_MUSTLOCK(source)) SDL_LockSurface(source);
    {
        unsigned char *pixels = (unsigned char *)source->pixels;
        for (int y = 0; y < source->h; y++) {
            unsigned char *row = pixels + y * source->pitch;
            for (int x = 0; x < source->w; x++) {
                int band = x / 80;
                row[x*3 + 0] = (band & 1) ? 0xFF : 0x00;
                row[x*3 + 1] = (band & 2) ? 0xFF : 0x00;
                row[x*3 + 2] = (band & 4) ? 0xFF : 0x00;
            }
        }
    }
    if (SDL_MUSTLOCK(source)) SDL_UnlockSurface(source);

    int rc = SDL_BlitSurface(source, NULL, screen, NULL);
    dbgf("[sdl_test] SDL_BlitSurface rc=%d", rc);
    SDL_UpdateRect(screen, 0, 0, 0, 0);

    dbg("[sdl_test] showing for 5 seconds");
    SDL_Delay(5000);

    SDL_FreeSurface(source);
    SDL_Quit();
    dbg("[sdl_test] clean exit");

    LD_LAUNCH_DASHBOARD LaunchData = { XLD_LAUNCH_DASHBOARD_MAIN_MENU };
    XLaunchNewImage(NULL, (LAUNCH_DATA*)&LaunchData);
    return 0;
}
