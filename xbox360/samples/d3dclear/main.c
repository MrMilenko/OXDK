// Clear the screen, using the real XDK headers and the real d3d9 library.
//
// This is ordinary Xbox 360 code. The structures, enums and function declarations all come
// from Microsoft's own d3d9.h; nothing here is reimplemented. The only thing that is not
// what you would write on Windows is that calls into the XDK go through the small assembly
// shims in shim.S, because clang's SysV frame does not give a Xenon callee the argument
// save area it expects. A compiler with the Xenon ABI would remove that detail.

#include <xdk.h>
#include <excpt.h>
#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <xbox.h>
#include <d3d9.h>

// Reached through shims too: everything crossing into XDK or kernel code does.
void oxdk_DbgPrint(const char *fmt, ...);
void oxdk_Reboot(unsigned int routine);
#define DbgPrint oxdk_DbgPrint
#define HalReturnToFirmware oxdk_Reboot

// ABI shims, see shim.S
HRESULT oxdk_CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, void *hFocusWindow,
                          DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pParams,
                          D3DDevice **ppDevice);
void oxdk_Clear(D3DDevice *pDevice, DWORD Count, CONST D3DRECT *pRects, DWORD Flags,
                D3DCOLOR Color, float Z, DWORD Stencil, BOOL EDRAMClear);
void oxdk_Present(D3DDevice *pDevice);

static D3DDevice *g_device;

int main(void)
{
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;
    int i;

    DbgPrint("OXDK360: creating a D3D device\n");

    memset(&pp, 0, sizeof(pp));
    // These are exactly the defaults in Microsoft's own ATG framework (AtgApp.cpp), which
    // every sample in the XDK starts from. Dictating 1280x720 instead put the display into
    // a mode it would not lock onto, and since the program then died before its own
    // reboot, the console stayed dark until a cold boot.
    pp.BackBufferWidth        = 640;
    pp.BackBufferHeight       = 480;
    pp.BackBufferFormat       = D3DFMT_A8R8G8B8;
    pp.BackBufferCount        = 1;
    pp.MultiSampleType        = D3DMULTISAMPLE_NONE;
    pp.MultiSampleQuality     = 0;
    pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow          = NULL;
    pp.Windowed               = FALSE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.Flags                  = 0;
    pp.FullScreen_RefreshRateInHz = 0;
    // INTERVAL_IMMEDIATE is the ATG default and presents as fast as the GPU will go,
    // which turns a 900 frame loop into a flicker. Lock to the refresh so a frame is
    // a frame: 900 of them is fifteen seconds.
    pp.PresentationInterval   = D3DPRESENT_INTERVAL_ONE;

    hr = oxdk_CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                           D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_device);
    if (hr < 0 || !g_device) {
        DbgPrint("OXDK360: Direct3D_CreateDevice failed\n");
        HalReturnToFirmware(1);
        return 1;
    }
    DbgPrint("OXDK360: device created\n");

    // One stage at a time, so a crash tells us which call is at fault. No formatted
    // output anywhere: DbgPrint is variadic and clang's varargs do not survive the shim.
    oxdk_Clear(g_device, 0, NULL, D3DCLEAR_TARGET, 0xFF107C10, 1.0f, 0, FALSE);
    DbgPrint("OXDK360: first clear returned\n");

    oxdk_Present(g_device);
    DbgPrint("OXDK360: first present returned\n");

    // Short and self-limiting: about two seconds, then reboot on our own terms rather
    // than leaving the display in whatever state a crash produces.
    for (i = 0; i < 900; i++) {
        oxdk_Clear(g_device, 0, NULL, D3DCLEAR_TARGET,
                   (i / 150) % 4 == 0 ? 0xFF107C10 : (i / 150) % 4 == 1 ? 0xFF0078D7 : (i / 150) % 4 == 2 ? 0xFFE81123 : 0xFFFFFFFF, 1.0f, 0, FALSE);
        oxdk_Present(g_device);
    }
    DbgPrint("OXDK360: present loop finished\n");

    DbgPrint("OXDK360: done, rebooting\n");
    HalReturnToFirmware(1);
    return 0;
}
