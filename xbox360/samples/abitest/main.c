// Checks the Xenon ABI on the console and puts the results on screen.
//
// Every value below is formatted by Microsoft's own sprintf out of libcMT, so
// it crosses the boundary the ABI describes. A wrong calling convention shows
// up as a wrong number rather than a crash, which is why each line carries the
// value it should have.
//
// Press A, B or Start to return to the dashboard.
//
// Text is drawn with D3DDevice_Clear and a list of rectangles, one per lit
// pixel of the font. That needs no texture, no shader and no font file, which
// keeps this sample to two files.

#include <xdk.h>
#include <excpt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windef.h>
#include <winbase.h>
#include <xbox.h>
#include <d3d9.h>
#include <xinputdefs.h>

#include "font.h"

// Declared here rather than included: the XDK spreads these across headers
// that pull in more than this sample needs.
DWORD XInputGetState(DWORD dwUserIndex, XINPUT_STATE *pState);
void  DbgPrint(const char *fmt, ...);
void  HalReturnToFirmware(unsigned int routine);

#define REBOOT_TO_DASHBOARD 1

#define SCREEN_W 640
#define SCREEN_H 480

#define COLOR_BG    0xFF101418
#define COLOR_TEXT  0xFFC8CDD2
#define COLOR_TITLE 0xFFFFFFFF
#define COLOR_PASS  0xFF3DD68C
#define COLOR_FAIL  0xFFE8544A

static D3DDevice *g_device;

// Rectangles waiting to be drawn in one colour.
static D3DRECT g_rects[2048];
static int     g_rect_count;

static void flush_rects(D3DCOLOR color)
{
    if (g_rect_count == 0)
        return;
    D3DDevice_Clear(g_device, g_rect_count, g_rects, D3DCLEAR_TARGET,
                    color, 1.0f, 0, FALSE);
    g_rect_count = 0;
}

static void push_rect(D3DCOLOR color, int x, int y, int w, int h)
{
    if (g_rect_count == (int)(sizeof(g_rects) / sizeof(g_rects[0])))
        flush_rects(color);
    g_rects[g_rect_count].x1 = x;
    g_rects[g_rect_count].y1 = y;
    g_rects[g_rect_count].x2 = x + w;
    g_rects[g_rect_count].y2 = y + h;
    ++g_rect_count;
}

static int glyph_index(char c)
{
    const char *p;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (p = FONT_CHARS; *p; ++p)
        if (*p == c)
            return (int)(p - FONT_CHARS);
    return 0;
}

// Returns the x it ended at, so lines can be built from several colours.
static int draw_text(int x, int y, int scale, D3DCOLOR color, const char *s)
{
    int row, col;
    for (; *s; ++s) {
        const unsigned char *g = FONT[glyph_index(*s)];
        for (row = 0; row < 7; ++row) {
            for (col = 0; col < 5; ++col) {
                if (g[row] & (0x10 >> col))
                    push_rect(color, x + col * scale, y + row * scale,
                              scale, scale);
            }
        }
        x += 6 * scale;
    }
    flush_rects(color);
    return x;
}

struct check {
    const char *label;
    char        got[48];
    const char *want;
};

static struct check g_checks[5];
static int          g_count;
static int          g_failures;

static void add_check(const char *label, const char *got, const char *want)
{
    struct check *c = &g_checks[g_count++];
    c->label = label;
    strncpy(c->got, got, sizeof(c->got) - 1);
    c->got[sizeof(c->got) - 1] = 0;
    c->want = want;
    if (strcmp(got, want) != 0)
        ++g_failures;
}

static void run_checks(void)
{
    unsigned long long u = 1234567890123ULL;
    long long          s = -98765432101234LL;
    char               b[64];

    // A 64 bit value takes one argument slot and one 64 bit register. Split
    // across two slots, this prints nonsense.
    sprintf(b, "%llu", u);
    add_check("64 bit unsigned", b, "1234567890123");

    sprintf(b, "%lld", s);
    add_check("64 bit signed", b, "-98765432101234");

    sprintf(b, "%llx", u);
    add_check("64 bit hex", b, "11f71fb04cb");

    // The int after the 64 bit value is the real test: it only lands right if
    // the value ahead of it consumed a single slot.
    sprintf(b, "%d %llu %d", 7, u, 9);
    add_check("int after 64 bit", b, "7 1234567890123 9");

    // A variadic double travels in the general register sharing its slot.
    sprintf(b, "%.2f", 2.5);
    add_check("double", b, "2.50");
}

static void draw_frame(void)
{
    int i, y, x;

    D3DDevice_Clear(g_device, 0, NULL, D3DCLEAR_TARGET, COLOR_BG, 1.0f, 0, FALSE);

    draw_text(40, 40, 3, COLOR_TITLE, "XENON ABI");
    draw_text(40, 76, 1, COLOR_TEXT,
              "FORMATTED BY MICROSOFTS SPRINTF IN LIBCMT");

    y = 120;
    for (i = 0; i < g_count; ++i) {
        int pass = strcmp(g_checks[i].got, g_checks[i].want) == 0;
        draw_text(40, y, 2, COLOR_TEXT, g_checks[i].label);
        x = draw_text(280, y, 2, pass ? COLOR_PASS : COLOR_FAIL,
                      g_checks[i].got);
        if (!pass) {
            draw_text(280, y + 18, 1, COLOR_FAIL, "WANTED");
            draw_text(340, y + 18, 1, COLOR_FAIL, g_checks[i].want);
        }
        (void)x;
        y += 46;
    }

    y += 20;
    if (g_failures == 0)
        draw_text(40, y, 2, COLOR_PASS, "ALL CHECKS PASSED");
    else
        draw_text(40, y, 2, COLOR_FAIL, "CHECKS FAILED");

    draw_text(40, SCREEN_H - 40, 1, COLOR_TEXT,
              "PRESS A B OR START TO RETURN TO THE DASHBOARD");

    D3DDevice_Present(g_device);
}

int main(void)
{
    D3DPRESENT_PARAMETERS pp;
    XINPUT_STATE          pad;
    HRESULT               hr;
    int                   held_at_start = 1;

    run_checks();

    // The defaults Microsoft's own ATG framework uses. Asking for a mode the
    // display will not lock onto leaves the console dark.
    memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth            = SCREEN_W;
    pp.BackBufferHeight           = SCREEN_H;
    pp.BackBufferFormat           = D3DFMT_A8R8G8B8;
    pp.BackBufferCount            = 1;
    pp.MultiSampleType            = D3DMULTISAMPLE_NONE;
    pp.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
    pp.Windowed                   = FALSE;
    pp.EnableAutoDepthStencil     = TRUE;
    pp.AutoDepthStencilFormat     = D3DFMT_D24S8;
    pp.FullScreen_RefreshRateInHz = 0;
    pp.PresentationInterval       = D3DPRESENT_INTERVAL_ONE;

    hr = Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                               D3DCREATE_HARDWARE_VERTEXPROCESSING,
                               &pp, &g_device);
    if (hr < 0 || !g_device) {
        DbgPrint("abitest: could not create a device\n");
        HalReturnToFirmware(REBOOT_TO_DASHBOARD);
        return 1;
    }

    for (;;) {
        draw_frame();

        memset(&pad, 0, sizeof(pad));
        if (XInputGetState(0, &pad) == ERROR_SUCCESS) {
            WORD b = pad.Gamepad.wButtons;
            int  pressed = (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B |
                                 XINPUT_GAMEPAD_START)) != 0;
            // Ignore a button already down when the title started, so the
            // press that launched it does not exit immediately.
            if (!pressed)
                held_at_start = 0;
            else if (!held_at_start)
                break;
        }
    }

    HalReturnToFirmware(REBOOT_TO_DASHBOARD);
    return 0;
}
