// OXDK Summer 2026 Demo, a TeamUIX release. Code by Milenko.
//
// Dark, gritty, Xbox green. Video, input and the bitmap font come from
// RXDK-SDL2x; everything else is modern C++17 on libc++ (std::vector star
// layers, a std::string scroller, <random> seeding, <cmath> for motion).
// Linked against the genuine Microsoft XDK, built from macOS with clang + lld.
#include "SDL_test_common.h"
#include "SDL_test_font.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace {

int g_w = 640;
int g_h = 480;

struct Star {
    float x, y;
    float speed;
    int layer; // 0 far .. 2 near
};

// Xbox green; steel blue for the credit line.
constexpr Uint8 GR = 132, GG = 196, GB = 30;
constexpr Uint8 BR = 96, BG = 150, BB = 216;

Uint8 scale8(Uint8 v, float k) {
    float r = v * k;
    if (r < 0.0f) r = 0.0f;
    if (r > 255.0f) r = 255.0f;
    return static_cast<Uint8>(r);
}

void fill_rect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(r, &rect);
}

// Near black with a faint green vertical falloff.
void render_background(SDL_Renderer* r) {
    SDL_SetRenderDrawColor(r, 4, 8, 5, 255);
    SDL_RenderClear(r);
    const int band = 8;
    for (int y = 0; y < g_h; y += band) {
        float d = 1.0f - std::fabs(y - g_h * 0.5f) / (g_h * 0.5f);
        fill_rect(r, 0, y, g_w, band, scale8(GR, 0.09f * d), scale8(GG, 0.10f * d), scale8(GB, 0.06f * d));
    }
}

// A single soft green band crawling down the screen, like a scanner sweep.
void render_sweep(SDL_Renderer* r, float t) {
    // Fractional part without fmodf (the XDK CRT only ships the double fmod).
    float ph = t * 0.12f;
    ph -= static_cast<float>(static_cast<long>(ph));
    int sy = static_cast<int>(ph * g_h);
    const int reach = 36;
    for (int i = -reach; i <= reach; ++i) {
        float k = (1.0f - std::fabs(static_cast<float>(i)) / reach) * 0.42f;
        fill_rect(r, 0, sy + i, g_w, 1, scale8(GR, k), scale8(GG, k), scale8(GB, k));
    }
}

void render_stars(SDL_Renderer* r, std::vector<Star>& stars, std::mt19937& rng) {
    std::uniform_real_distribution<float> fy(0.0f, static_cast<float>(g_h));
    for (Star& s : stars) {
        s.x -= s.speed * 0.016f;
        if (s.x < 0.0f) {
            s.x += static_cast<float>(g_w);
            s.y = fy(rng);
        }
        float k = 0.40f + s.layer * 0.22f;
        int sz = s.layer + 1;
        fill_rect(r, static_cast<int>(s.x), static_cast<int>(s.y), sz, sz,
                  scale8(GR, k), scale8(GG, k), scale8(GB, k));
    }
}

void render_frame(SDL_Renderer* r) {
    Uint8 fr = scale8(GR, 0.65f), fg = scale8(GG, 0.65f), fb = scale8(GB, 0.65f);
    fill_rect(r, 0, 0, g_w, 2, fr, fg, fb);
    fill_rect(r, 0, g_h - 2, g_w, 2, fr, fg, fb);
    fill_rect(r, 0, 0, 2, g_h, fr, fg, fb);
    fill_rect(r, g_w - 2, 0, 2, g_h, fr, fg, fb);
}

// Green text on a sine wave, brightness cresting along it.
void draw_wavy(SDL_Renderer* r, const std::string& s, int x, int y, float t, float scale, float amp) {
    SDL_RenderSetScale(r, scale, scale);
    int cx = x;
    for (std::size_t i = 0; i < s.size(); ++i) {
        float wob = std::sin(t * 2.4f + static_cast<float>(i) * 0.4f) * amp;
        float k = 0.68f + 0.32f * (std::sin(t * 3.0f - static_cast<float>(i) * 0.35f) * 0.5f + 0.5f);
        SDL_SetRenderDrawColor(r, scale8(GR, k), scale8(GG, k), scale8(GB, k), 255);
        char buf[2] = { s[i], 0 };
        SDLTest_DrawString(r, static_cast<int>(cx / scale), static_cast<int>((y + wob) / scale), buf);
        cx += static_cast<int>(FONT_CHARACTER_SIZE * scale);
    }
    SDL_RenderSetScale(r, 1.0f, 1.0f);
}

void draw_text(SDL_Renderer* r, int x, int y, const std::string& s, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDLTest_DrawString(r, x, y, s.c_str());
}

// Block breaker. Move the paddle with the left stick or D-pad, clear the wall.
struct Brick {
    int x, y, w, h;
    Uint8 r, g, b;
    bool alive;
};

struct Breakout {
    float bx, by, bvx, bvy;
    float px;
    int score;
    bool init;
    std::vector<Brick> bricks;
};

// Playfield geometry, centered and filling the band below the info lines.
const int BO_W = 460, BO_H = 200;
const int BO_OY = 240;
const int BO_COLS = 10, BO_ROWS = 5;
const int BO_PW = 74, BO_PH = 8, BO_BALL = 6;
const float BO_MAX = 320.0f;

void breakout_reset_ball(Breakout& g, int ox) {
    g.px = ox + BO_W / 2.0f - BO_PW / 2.0f;
    g.bx = ox + BO_W / 2.0f;
    g.by = BO_OY + BO_H - 40.0f;
    g.bvx = 150.0f;
    g.bvy = -190.0f;
}

void breakout_build(Breakout& g, int ox) {
    const Uint8 row_rgb[BO_ROWS][3] = {
        { 232, 184, 48 },  // amber
        { 132, 196, 30 },  // xbox green
        { 70, 174, 208 },  // steel cyan
        { 216, 110, 44 },  // ember orange
        { 168, 212, 96 },  // pale green
    };
    const int margin = 12, gap = 3;
    const int bwid = (BO_W - 2 * margin - (BO_COLS - 1) * gap) / BO_COLS;
    const int bhei = 13;
    g.bricks.clear();
    for (int row = 0; row < BO_ROWS; ++row) {
        for (int col = 0; col < BO_COLS; ++col) {
            Brick br;
            br.x = ox + margin + col * (bwid + gap);
            br.y = BO_OY + 20 + row * (bhei + gap);
            br.w = bwid;
            br.h = bhei;
            br.r = row_rgb[row][0];
            br.g = row_rgb[row][1];
            br.b = row_rgb[row][2];
            br.alive = true;
            g.bricks.push_back(br);
        }
    }
}

void update_minigame(SDL_Renderer* r, SDL_GameController* gc, Breakout& g) {
    const int ox = g_w / 2 - BO_W / 2;
    const float paddle_y = static_cast<float>(BO_OY + BO_H - 18);

    if (!g.init) {
        breakout_build(g, ox);
        breakout_reset_ball(g, ox);
        g.score = 0;
        g.init = true;
    }

    float move = 0.0f;
    if (gc) {
        Sint16 ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        if (ax > 9000 || ax < -9000) {
            move = ax / 32767.0f;
        }
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) move = -1.0f;
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) move = 1.0f;
    }
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (ks) {
        if (ks[SDL_SCANCODE_LEFT]) move = -1.0f;
        if (ks[SDL_SCANCODE_RIGHT]) move = 1.0f;
    }
    g.px += move * 300.0f * 0.016f;
    if (g.px < ox + 2) g.px = ox + 2;
    if (g.px > ox + BO_W - BO_PW - 2) g.px = ox + BO_W - BO_PW - 2;

    g.bx += g.bvx * 0.016f;
    g.by += g.bvy * 0.016f;
    if (g.bx < ox + 2) { g.bx = ox + 2; g.bvx = -g.bvx; }
    if (g.bx > ox + BO_W - 2 - BO_BALL) { g.bx = ox + BO_W - 2 - BO_BALL; g.bvx = -g.bvx; }
    if (g.by < BO_OY + 2) { g.by = BO_OY + 2; g.bvy = -g.bvy; }

    // Brick hits: resolve along the axis of least penetration.
    float cx = g.bx + BO_BALL / 2.0f;
    float cy = g.by + BO_BALL / 2.0f;
    for (Brick& br : g.bricks) {
        if (!br.alive) {
            continue;
        }
        float bcx = br.x + br.w / 2.0f;
        float bcy = br.y + br.h / 2.0f;
        float ax = (BO_BALL / 2.0f + br.w / 2.0f) - (cx > bcx ? cx - bcx : bcx - cx);
        float ay = (BO_BALL / 2.0f + br.h / 2.0f) - (cy > bcy ? cy - bcy : bcy - cy);
        if (ax > 0.0f && ay > 0.0f) {
            if (ax < ay) {
                g.bvx = -g.bvx;
            } else {
                g.bvy = -g.bvy;
            }
            br.alive = false;
            ++g.score;
            break;
        }
    }

    // Paddle bounce, with a little english based on where it lands.
    if (g.bvy > 0.0f && g.by + BO_BALL >= paddle_y && g.by + BO_BALL <= paddle_y + BO_PH + 4 &&
        g.bx + BO_BALL >= g.px && g.bx <= g.px + BO_PW) {
        g.by = paddle_y - BO_BALL;
        g.bvy = -g.bvy;
        float off = (g.bx + BO_BALL / 2.0f - (g.px + BO_PW / 2.0f)) / (BO_PW / 2.0f);
        g.bvx += off * 60.0f;
    }
    if (g.bvx > BO_MAX) g.bvx = BO_MAX;
    if (g.bvx < -BO_MAX) g.bvx = -BO_MAX;

    // Lost ball: respawn it but keep the score and the wall.
    if (g.by > BO_OY + BO_H) {
        breakout_reset_ball(g, ox);
    }

    // Cleared the wall: rebuild and speed up.
    bool any = false;
    for (const Brick& br : g.bricks) {
        if (br.alive) { any = true; break; }
    }
    if (!any) {
        breakout_build(g, ox);
        breakout_reset_ball(g, ox);
        g.bvy *= 1.1f;
        if (g.bvy < -BO_MAX) g.bvy = -BO_MAX;
    }

    Uint8 fr = scale8(GR, 0.6f), fg = scale8(GG, 0.6f), fb = scale8(GB, 0.6f);
    fill_rect(r, ox, BO_OY, BO_W, 2, fr, fg, fb);
    fill_rect(r, ox, BO_OY + BO_H - 2, BO_W, 2, fr, fg, fb);
    fill_rect(r, ox, BO_OY, 2, BO_H, fr, fg, fb);
    fill_rect(r, ox + BO_W - 2, BO_OY, 2, BO_H, fr, fg, fb);

    for (const Brick& br : g.bricks) {
        if (br.alive) {
            fill_rect(r, br.x, br.y, br.w, br.h, br.r, br.g, br.b);
        }
    }

    fill_rect(r, static_cast<int>(g.px), static_cast<int>(paddle_y), BO_PW, BO_PH,
              scale8(GR, 0.9f), scale8(GG, 0.9f), scale8(GB, 0.9f));
    fill_rect(r, static_cast<int>(g.bx), static_cast<int>(g.by), BO_BALL, BO_BALL, 230, 240, 210);

    std::string score = "SCORE " + std::to_string(g.score);
    draw_text(r, ox + 8, BO_OY + 5, score, 230, 240, 210);
    draw_text(r, ox + BO_W - 152, BO_OY + 5, "dpad or stick", scale8(GR, 0.85f), scale8(GG, 0.85f), scale8(GB, 0.85f));
}

// CRT scanline grime over the whole frame. Semi-transparent so it dims rather
// than punches holes through the small text.
void render_scanlines(SDL_Renderer* r) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 70);
    for (int y = 0; y < g_h; y += 2) {
        SDL_Rect line = { 0, y, g_w, 1 };
        SDL_RenderFillRect(r, &line);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

const std::array<std::string, 4> kLines = {
    std::string("genuine Microsoft XDK libraries"),
    std::string("SDL2 through RXDK SDL2x"),
    std::string("a full C++17 runtime via libc++"),
    std::string("built on macOS with clang and lld"),
};

const std::string kScroller =
    "OXDK SUMMER 2026 DEMO ....... code by Milenko of TeamUIX ....... the original "
    "Xbox was mean, and it still bites ....... real Microsoft XDK, SDL2 through RXDK "
    "SDL2x, and a full C++17 standard library by way of libc++, all built on a Mac "
    "with clang and lld ....... std vector, std string, std unordered map and the "
    "random header running on a 733 MHz Pentium III ....... GREETZ to The Xbox-Scene, "
    "the modders, the developers, the tinkerers ....... special thanks to Team "
    "Resurgent for RXDK and the RXDK SDL2x port, CrunchBite, GoTeamScotch, "
    "ConsoleMods.org and Team Cerbios ....... if you can read this on real hardware "
    "then it just works ....... hit B to drop back to the dash ........        ";

} // namespace

int main(int argc, char* argv[]) {
    SDLTest_CommonState* state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    if (!state) {
        return 1;
    }
    if (!SDLTest_CommonInit(state)) {
        SDLTest_CommonQuit(state);
        return 2;
    }

    SDL_Renderer* renderer = state->renderers[0];
    SDL_GetRendererOutputSize(renderer, &g_w, &g_h);

    // SDLTest_CommonInit only starts the video/audio subsystems, so the
    // GAMECONTROLLER flag we passed is ignored. Bring it up ourselves or no
    // controller ever enumerates.
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

    // Open whatever is already attached; CONTROLLERDEVICEADDED below catches
    // any that enumerate after init (which is how the OG Xbox pad arrives).
    SDL_GameController* gc = nullptr;
    for (int i = 0; i < SDL_NumJoysticks() && !gc; ++i) {
        if (SDL_IsGameController(i)) {
            gc = SDL_GameControllerOpen(i);
        }
    }

    std::mt19937 rng(0xF2CEu);
    std::uniform_real_distribution<float> fx(0.0f, static_cast<float>(g_w));
    std::uniform_real_distribution<float> fy(0.0f, static_cast<float>(g_h));
    std::vector<Star> stars;
    stars.reserve(200);
    for (int i = 0; i < 200; ++i) {
        Star s;
        s.x = fx(rng);
        s.y = fy(rng);
        s.layer = static_cast<int>(rng() % 3);
        s.speed = 24.0f + s.layer * 52.0f;
        stars.push_back(s);
    }

    Breakout game{};
    int done = 0;
    Uint32 start = SDL_GetTicks();
    float scroll_x = static_cast<float>(g_w);

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            SDLTest_CommonEvent(state, &event, &done);
            if (event.type == SDL_CONTROLLERDEVICEADDED && !gc) {
                gc = SDL_GameControllerOpen(event.cdevice.which);
            }
            if (event.type == SDL_CONTROLLERBUTTONDOWN &&
                event.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                done = 1;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                done = 1;
            }
        }
        if (gc && SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B)) {
            done = 1;
        }

        float t = (SDL_GetTicks() - start) / 1000.0f;

        render_background(renderer);
        render_sweep(renderer, t);
        render_stars(renderer, stars, rng);

        // Logo with a hard drop shadow and a breathing glow.
        float glow = 0.80f + 0.20f * (std::sin(t * 2.0f) * 0.5f + 0.5f);
        int title_x = g_w / 2 - 80; // "OXDK" is 4 glyphs * 8px * scale 5 = 160 wide
        int title_y = 56;
        SDL_RenderSetScale(renderer, 5.0f, 5.0f);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDLTest_DrawString(renderer, (title_x + 5) / 5, (title_y + 5) / 5, "OXDK");
        SDL_SetRenderDrawColor(renderer, scale8(GR, glow), scale8(GG, glow), scale8(GB, glow), 255);
        SDLTest_DrawString(renderer, title_x / 5, title_y / 5, "OXDK");
        SDL_RenderSetScale(renderer, 1.0f, 1.0f);

        draw_wavy(renderer, "SUMMER 2026 DEMO", g_w / 2 - 128, 110, t, 2.0f, 3.0f);

        {
            float p = std::sin(t * 2.5f) * 0.5f + 0.5f;
            float k = 0.8f + 0.2f * p;
            draw_text(renderer, g_w / 2 - 136, 140, "a TeamUIX release, code by Milenko",
                      scale8(BR, k), scale8(BG, k), scale8(BB, k));
        }

        for (std::size_t i = 0; i < kLines.size(); ++i) {
            float p = std::sin(t * 1.8f + static_cast<float>(i) * 0.6f) * 0.5f + 0.5f;
            float k = 0.78f + 0.22f * p;
            draw_text(renderer, g_w / 2 - static_cast<int>(kLines[i].size()) * 4,
                      162 + static_cast<int>(i) * (FONT_LINE_HEIGHT + 8), kLines[i],
                      scale8(GR, k), scale8(GG, k), scale8(GB, k));
        }

        update_minigame(renderer, gc, game);

        const float scroll_scale = 2.0f;
        float scroller_px = kScroller.size() * FONT_CHARACTER_SIZE * scroll_scale;
        scroll_x -= 110.0f * 0.016f;
        if (scroll_x < -scroller_px) {
            scroll_x = static_cast<float>(g_w);
        }
        draw_wavy(renderer, kScroller, static_cast<int>(scroll_x), g_h - 34, t, scroll_scale, 6.0f);

        render_scanlines(renderer);
        render_frame(renderer);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (gc) {
        SDL_GameControllerClose(gc);
    }
    SDLTest_CommonQuit(state);
    return 0;
}
