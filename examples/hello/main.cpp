// OXDK Hello World -- "Definitely Not A Dolphin"
//
// A sea creature of indeterminate species bounces around your screen.
// Any resemblance to a certain Xbox SDK sample is purely coincidental.

#include <xtl.h>


// A 16x16 sea creature, drawn by someone with no artistic talent.
// Each row is 16 pixels, ARGB format. 0 = transparent.
#define _ 0x00000000
#define B 0xFF003366
#define L 0xFF0055AA
#define W 0xFFFFFFFF
#define E 0xFF000000

static const DWORD creature_pixels[16 * 16] = {
//   0 1 2 3 4 5 6 7 8 9 A B C D E F
    _,_,_,_,_,_,_,_,_,B,B,_,_,_,_,_, // 0  tail tip
    _,_,_,_,_,_,_,_,B,L,L,B,_,_,_,_, // 1  tail
    _,_,_,_,_,_,_,B,L,L,B,_,_,_,_,_, // 2  tail base
    _,_,_,_,_,_,B,L,L,B,_,_,_,_,_,_, // 3  peduncle
    _,_,_,B,B,B,L,L,L,L,B,B,B,_,_,_, // 4  dorsal fin + body
    _,_,B,L,L,L,L,L,L,L,L,L,L,B,_,_, // 5  upper body
    _,B,L,L,L,L,L,L,L,L,L,L,L,L,B,_, // 6  body wide
    _,B,L,L,L,L,L,L,L,L,L,L,L,L,B,_, // 7  body
    B,L,L,L,L,L,L,L,L,L,W,E,L,L,L,B, // 8  head + eye
    B,L,L,L,L,L,L,L,L,L,L,L,L,L,L,B, // 9  body
    _,B,L,L,L,L,L,L,L,L,L,L,L,L,B,_, // A  lower body
    _,_,B,L,W,W,L,L,L,L,L,L,L,B,_,_, // B  belly + snout
    _,_,_,B,B,B,L,L,L,L,L,L,B,_,_,_, // C  flipper + tail start
    _,_,B,L,B,_,B,L,L,L,B,B,_,_,_,_, // D  flipper
    _,_,_,B,_,_,_,B,B,B,_,_,_,_,_,_, // E  flukes
    _,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_, // F
};

#undef _
#undef B
#undef L
#undef W
#undef E

struct QUADVERTEX
{
    float x, y, z, rhw;
    float u, v;
};

int main()
{
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

    // Create the world's worst sea creature texture
    LPDIRECT3DTEXTURE8 pTex = NULL;
    pDev->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pTex);
    if (pTex != NULL)
    {
        D3DLOCKED_RECT lr;
        pTex->LockRect(0, &lr, NULL, 0);
        for (int y = 0; y < 16; y++)
            memcpy((BYTE*)lr.pBits + y * lr.Pitch, &creature_pixels[y * 16], 16 * 4);
        pTex->UnlockRect(0);
    }

    // Sea creature state -- DVD screensaver style
    float dx = 2.2f, dy = 1.7f;
    float x = 200.0f, y = 150.0f;
    float size = 128.0f;
    DWORD tint = 0xFFFFFFFF;
    int bounceCount = 0;

    // Some colors to cycle through on bounce
    DWORD colors[] = {
        0xFFFFFFFF, 0xFF00FFFF, 0xFFFF88FF, 0xFF88FF88,
        0xFFFFFF00, 0xFFFF8844, 0xFF44CCFF, 0xFFFF4444,
    };

    pDev->SetRenderState(D3DRS_LIGHTING, FALSE);
    pDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    pDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    pDev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    pDev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);

    for (;;)
    {
        // Move the creature
        x += dx;
        y += dy;

        bool bounced = false;
        if (x <= 0.0f || x + size >= 640.0f) { dx = -dx; bounced = true; }
        if (y <= 0.0f || y + size >= 480.0f) { dy = -dy; bounced = true; }

        if (bounced)
            tint = colors[(++bounceCount) % 8];

        // Draw
        pDev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        pDev->BeginScene();

        pDev->SetTexture(0, pTex);
        pDev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_TEX1);

        QUADVERTEX quad[4] = {
            { x,        y,        0.0f, 1.0f, 0.0f, 0.0f },
            { x + size, y,        0.0f, 1.0f, 1.0f, 0.0f },
            { x,        y + size, 0.0f, 1.0f, 0.0f, 1.0f },
            { x + size, y + size, 0.0f, 1.0f, 1.0f, 1.0f },
        };

        pDev->SetRenderState(D3DRS_TEXTUREFACTOR, tint);
        pDev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        pDev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        pDev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_TFACTOR);

        pDev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(QUADVERTEX));

        pDev->EndScene();
        pDev->Present(NULL, NULL, NULL, NULL);
    }

    return 0;
}
