// Minimal Xbox XDK application built with OXDK
//
// If this boots to a black screen and doesn't crash, your toolchain works.

#include <xtl.h>

int main()
{
    // Init D3D
    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    if (pD3D == NULL)
        return 1;

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    LPDIRECT3DDEVICE8 pDev = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDev);

    if (FAILED(hr) || pDev == NULL)
        return 2;

    // Main loop -- clear to green so you know it worked
    for (;;)
    {
        pDev->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                    D3DCOLOR_XRGB(0, 80, 0), 1.0f, 0);
        pDev->BeginScene();
        pDev->EndScene();
        pDev->Present(NULL, NULL, NULL, NULL);
    }

    return 0;
}
