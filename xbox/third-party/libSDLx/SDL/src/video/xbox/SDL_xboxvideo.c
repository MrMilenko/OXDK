/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002  Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Sam Lantinga
    slouken@libsdl.org
*/

#ifdef SAVE_RCSID
static char rcsid =
 "@(#) $Id: SDL_xboxvideo.c,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* XBOX SDL video driver implementation; this is just enough to make an
 *  SDL-based application THINK it's got a working video driver, for
 *  applications that call SDL_Init(SDL_INIT_VIDEO) when they don't need it,
 *  and also for use as a collection of stubs when porting SDL to a new
 *  platform for which you haven't yet written a valid video driver.
 *
 * This is also a great way to determine bottlenecks: if you think that SDL
 *  is a performance problem for a given platform, enable this driver, and
 *  then see if your application runs faster without video overhead.
 *
 * Initial work by Ryan C. Gordon (icculus@linuxgames.com). A good portion
 *  of this was cut-and-pasted from Stephane Peter's work in the AAlib
 *  SDL video driver.  Renamed to "XBOX" by Sam Lantinga.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_sysvideo.h"
#include "SDL_pixels_c.h"
#include "SDL_events_c.h"

#include "SDL_xboxvideo.h"
#include "SDL_xboxevents_c.h"
#include "SDL_xboxmouse_c.h"

#define XBOXVID_DRIVER_NAME "XBOX"

typedef struct VERTEX { D3DXVECTOR4 p; D3DCOLOR vbColor; FLOAT tu, tv; } SDL_Vertex;

/* Initialization/Query functions */
static int XBOX_VideoInit(_THIS, SDL_PixelFormat *vformat);
static int XBOX_BuildModesList();
static SDL_Rect **XBOX_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags);
static SDL_Surface *XBOX_SetVideoMode(_THIS, SDL_Surface *current, int width, int height, int bpp, Uint32 flags);
static int XBOX_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors);
static void XBOX_VideoQuit(_THIS);

/* Hardware surface functions */
static int XBOX_AllocHWSurface(_THIS, SDL_Surface *surface);
static int XBOX_LockHWSurface(_THIS, SDL_Surface *surface);
static void XBOX_UnlockHWSurface(_THIS, SDL_Surface *surface);
static void XBOX_FreeHWSurface(_THIS, SDL_Surface *surface);
static int XBOX_RenderSurface(_THIS, SDL_Surface *surface);
static int XBOX_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
static int XBOX_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst);
static int XBOX_HWAccelBlit(SDL_Surface *src, SDL_Rect *srcrect,SDL_Surface *dst, SDL_Rect *dstrect);
static int XBOX_SetHWAlpha(_THIS, SDL_Surface *surface, Uint8 alpha);
static int XBOX_SetHWColorKey(_THIS, SDL_Surface *surface, Uint32 key);
static int XBOX_SetFlickerFilter(_THIS, SDL_Surface *surface, int filter);
static int XBOX_SetSoftDisplayFilter(_THIS, SDL_Surface *surface, int enabled);

const static SDL_Rect
	RECT_1920x1080 = {0,0,1920,1080},
	RECT_1280x720 = {0,0,1280,720},
	RECT_800x600 = {0,0,800,600},
	RECT_720x576 = {0,0,720,576},
	RECT_720x480 = {0,0,720,480},
	RECT_640x480 = {0,0,640,480},
	RECT_512x384 = {0,0,512,384},
	RECT_400x300 = {0,0,400,300},
	RECT_320x240 = {0,0,320,240},
	RECT_320x200 = {0,0,320,200};
const static SDL_Rect **vid_modes;

/* XBOX screen calibration variables */
float cal_xoffset = 0;
float cal_yoffset = 0;
float cal_xstretch = 0;
float cal_ystretch = 0;

int have_vertexbuffer=0;
int have_direct3dtexture=0;

/* Set screen calibration functions */
void SDL_XBOX_SetScreenPosition(float x, float y)
{
	cal_xoffset = x;
	cal_yoffset = y;
	XBOX_RenderSurface(current_video,0);
}

void SDL_XBOX_SetScreenStretch(float xs, float ys)
{
	cal_xstretch = xs;
	cal_ystretch = ys;
	XBOX_RenderSurface(current_video,0);
}

/* Get screen calibration functions */
void SDL_XBOX_GetScreenPosition(float *x, float *y)
{
	*x = cal_xoffset;
	*y = cal_yoffset;
}

void SDL_XBOX_GetScreenStretch(float *xs, float *ys)
{
	*xs = cal_xstretch;
	*ys = cal_ystretch;
}

/* etc. */
void SDL_XBOX_SetFlickerFilter(int filter)
{
XBOX_SetFlickerFilter(current_video,0,filter);
}

void SDL_XBOX_SetSoftDisplayFilter(int enabled)
{
XBOX_SetSoftDisplayFilter(current_video,0,enabled);
}


static void XBOX_UpdateRects(_THIS, int numrects, SDL_Rect *rects);

/* XBOX driver bootstrap functions */

static int XBOX_Available(void)
{
	return(1);
}

static void XBOX_DeleteDevice(SDL_VideoDevice *device)
{
	free(device->hidden);
	free(device);
}

static SDL_VideoDevice *XBOX_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
				malloc((sizeof *device->hidden));
	}
	if ( (device == NULL) || (device->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( device ) {
			free(device);
		}
		return(0);
	}
	memset(device->hidden, 0, (sizeof *device->hidden));

	/* Set the function pointers */
	device->VideoInit = XBOX_VideoInit;
	device->ListModes = XBOX_ListModes;
	device->SetVideoMode = XBOX_SetVideoMode;
	device->CreateYUVOverlay = NULL;
	device->SetColors = XBOX_SetColors;
	device->UpdateRects = XBOX_UpdateRects;
	device->VideoQuit = XBOX_VideoQuit;
	device->AllocHWSurface = XBOX_AllocHWSurface;
	device->CheckHWBlit = NULL;
	device->FillHWRect = NULL;
	device->SetHWColorKey = XBOX_SetHWColorKey;
	device->SetHWAlpha = NULL;
	device->LockHWSurface = XBOX_LockHWSurface;
	device->UnlockHWSurface = XBOX_UnlockHWSurface;
	device->FlipHWSurface = XBOX_RenderSurface;
	device->FreeHWSurface = XBOX_FreeHWSurface;
	device->SetCaption = NULL;
	device->SetIcon = NULL;
	device->IconifyWindow = NULL;
	device->GrabInput = NULL;
	device->GetWMInfo = NULL;
	device->InitOSKeymap = XBOX_InitOSKeymap;
	device->PumpEvents = XBOX_PumpEvents;
	device->free = XBOX_DeleteDevice;

	return device;
}

VideoBootStrap XBOX_bootstrap = {
	XBOXVID_DRIVER_NAME, "XBOX SDL video driver V0.09",
	XBOX_Available, XBOX_CreateDevice
};


int XBOX_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	DWORD vidflags;

	if(XBOX_BuildModesList() <= 0)
	{
		SDL_SetError("No modes available");
		return 0;
	}

	if (!D3D)
		D3D = Direct3DCreate8(D3D_SDK_VERSION);

	ZeroMemory(&D3D_PP,sizeof(D3D_PP));

	vidflags = XGetVideoFlags();

	if(vidflags & XC_VIDEO_FLAGS_HDTV_480p)
		D3D_PP.Flags = D3DPRESENTFLAG_PROGRESSIVE;
	else
		D3D_PP.Flags = D3DPRESENTFLAG_INTERLACED;

	if(XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I)	// PAL user
	{
		if(vidflags & XC_VIDEO_FLAGS_PAL_60Hz)		// PAL 60 user
			D3D_PP.FullScreen_RefreshRateInHz = 60;
		else
			D3D_PP.FullScreen_RefreshRateInHz = 50;
	}

	D3D_PP.BackBufferWidth = 640;
	D3D_PP.BackBufferHeight = 480;
	D3D_PP.BackBufferFormat = D3DFMT_LIN_X8R8G8B8;
	D3D_PP.BackBufferCount = 1;
	D3D_PP.EnableAutoDepthStencil = TRUE;
	D3D_PP.AutoDepthStencilFormat = D3DFMT_D24S8;
	D3D_PP.SwapEffect = D3DSWAPEFFECT_DISCARD;

	if (!D3D_Device)
		IDirect3D8_CreateDevice(D3D,0,D3DDEVTYPE_HAL,NULL,D3DCREATE_HARDWARE_VERTEXPROCESSING,&D3D_PP,&D3D_Device);

	vformat->BitsPerPixel = 32;
	vformat->BytesPerPixel = 4;

	vformat->Amask = 0xFF000000;
	vformat->Rmask = 0x00FF0000;
	vformat->Gmask = 0x0000FF00;
	vformat->Bmask = 0x000000FF;

	/* We're done! */

	if (D3D_Device)
		return(1);
	else
		return (0);
}

int XBOX_BuildModesList()
{
	DWORD vidflags;
	int   i = 0;

	if(vid_modes)
		return -1;

	vid_modes = (SDL_Rect **)malloc(sizeof(SDL_Rect *));

	if(!vid_modes)
	{
		SDL_SetError("Couldn't allocate modes list");
		return -1;
	}

	vidflags = XGetVideoFlags();

	if(vidflags & XC_VIDEO_FLAGS_HDTV_1080i)
	{
		vid_modes[i++] = &RECT_1920x1080;
		vid_modes = (SDL_Rect **)realloc((void *)vid_modes, sizeof(SDL_Rect *) * (i + 1));
	}

	if(vidflags & XC_VIDEO_FLAGS_HDTV_720p)
	{
		vid_modes[i++] = &RECT_1280x720;
		vid_modes = (SDL_Rect **)realloc((void *)vid_modes, sizeof(SDL_Rect *) * (i + 1));
	}

	if(vidflags & XC_VIDEO_FLAGS_WIDESCREEN)
	{
		if(XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I)
			vid_modes[i++] = &RECT_720x576;
		else
			vid_modes[i++] = &RECT_720x480;
		vid_modes = (SDL_Rect **)realloc((void *)vid_modes, sizeof(SDL_Rect *) * (i + 1));
	}

	vid_modes = (SDL_Rect **)realloc((void *)vid_modes, sizeof(SDL_Rect *) * (i + 7));

	vid_modes[i++] = &RECT_800x600;
	vid_modes[i++] = &RECT_640x480;
	vid_modes[i++] = &RECT_512x384;
	vid_modes[i++] = &RECT_400x300;
	vid_modes[i++] = &RECT_320x240;
	vid_modes[i++] = &RECT_320x200;

	vid_modes[i] = NULL;

	return i;
}

SDL_Rect **XBOX_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	return (SDL_Rect **)vid_modes;
}

SDL_Surface *XBOX_SetVideoMode(_THIS, SDL_Surface *current,
				int width, int height, int bpp, Uint32 flags)
{

	int pixel_mode,pitch;
	Uint32 Rmask, Gmask, Bmask;
	DWORD vidflags;

	HRESULT ret;

	switch(bpp)
	{
		case 8:
			bpp = 16;
			pitch = width*2;
			pixel_mode = D3DFMT_LIN_R5G6B5;
		case 16:
			pitch = width*2;
			Rmask = 0x0000f800;
			Gmask = 0x000007e0;
			Bmask = 0x0000001f;
			pixel_mode = D3DFMT_LIN_R5G6B5;
			break;
		case 24:
			pitch = width*4;
			bpp = 32;
			pixel_mode = D3DFMT_LIN_X8R8G8B8;
		case 32:
			pitch = width*4;
			pixel_mode = D3DFMT_LIN_X8R8G8B8;
			Rmask = 0x00ff0000;
			Gmask = 0x0000ff00;
			Bmask = 0x000000ff;
			break;
		default:
			SDL_SetError("Couldn't find requested mode in list");
			return(NULL);
	}

	vidflags = XGetVideoFlags();

	if((width != 1920 && height != 1080) && ((width == 1280 && height == 720) || vidflags & XC_VIDEO_FLAGS_HDTV_480p))
		D3D_PP.Flags = D3DPRESENTFLAG_PROGRESSIVE;
	else
		D3D_PP.Flags = D3DPRESENTFLAG_INTERLACED;

	if(width > 640 && ((float)height / (float)width != 0.75) ||
	                        ((width == 720) && (height == 576))) // widescreen
	{
		D3D_PP.BackBufferWidth = width;
		D3D_PP.BackBufferHeight = height;
		D3D_PP.Flags |= D3DPRESENTFLAG_WIDESCREEN;
	}
	else
	{
		D3D_PP.BackBufferWidth = 640;
		D3D_PP.BackBufferHeight = 480;
	}

	IDirect3DDevice8_Reset(D3D_Device, &D3D_PP);


	/* Allocate the new pixel format for the screen */
	if ( ! SDL_ReallocFormat(current, bpp, Rmask, Gmask, Bmask, 0) ) {
		SDL_SetError("Couldn't allocate new pixel format for requested mode");
		return(NULL);
	}

	if (have_direct3dtexture) {
		D3DResource_Release((D3DResource*)this->hidden->SDL_primary);
	}
	
	ret = IDirect3DDevice8_CreateTexture(D3D_Device,width,height,1, 0,pixel_mode, (D3DPOOL)NULL, (D3DTexture**)&this->hidden->SDL_primary);
	have_direct3dtexture=1;

	if (ret != D3D_OK)
	{
		SDL_SetError("Couldn't create Direct3D Texture!");
		return(NULL);
	}

	if (have_vertexbuffer) {
		D3DResource_Release((D3DResource*)SDL_vertexbuffer);
	}
	ret = IDirect3DDevice8_CreateVertexBuffer(D3D_Device,4*(6*sizeof(FLOAT) + sizeof(DWORD)), D3DUSAGE_WRITEONLY,0L, D3DPOOL_DEFAULT, &SDL_vertexbuffer );
	have_vertexbuffer=1;


	/* Set up the new mode framebuffer */
	current->flags = (SDL_FULLSCREEN|SDL_HWSURFACE);

	if (flags & SDL_DOUBLEBUF)
		current->flags |= SDL_DOUBLEBUF;
	if (flags & SDL_HWPALETTE)
		current->flags |= SDL_HWPALETTE;



	current->w = width;
	current->h = height;
	current->pitch = current->w * (bpp / 8);
	current->pixels = NULL;


	IDirect3DDevice8_Clear(D3D_Device, 0, NULL, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0L);
	IDirect3DDevice8_Present(D3D_Device,NULL,NULL,NULL,NULL);

	IDirect3DDevice8_SetFlickerFilter(D3D_Device, 5);

	/* We're done */
	return(current);
}

/* We don't actually allow hardware surfaces other than the main one */

static int XBOX_AllocHWSurface(_THIS, SDL_Surface *surface)
{

	return(-1);
}
static void XBOX_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	return;
}

static int XBOX_RenderSurface(_THIS, SDL_Surface *surface)
{
	static int    i = 0;
	D3DSURFACE_DESC desc;

	SDL_Vertex *v;

	D3DXVECTOR4 a;
	D3DXVECTOR4 b;
	D3DXVECTOR4 c;
	D3DXVECTOR4 d;

	if (!this->hidden->SDL_primary)
		return 0;

	IDirect3DTexture8_GetLevelDesc(this->hidden->SDL_primary, 0, &desc);
	IDirect3DVertexBuffer8_Lock(SDL_vertexbuffer, 0, 0, (BYTE**)&v, 0L );

	a.x = cal_xoffset;
	a.y = cal_yoffset;
	a.z = 0;
	a.w = 0;

	v[0].p = a;
	v[0].vbColor = 0xFFFFFFFF;
	v[0].tu = 0;
	v[0].tv = 0;

	b.x = cal_xoffset + D3D_PP.BackBufferWidth + cal_xstretch;
	b.y = cal_yoffset;
	b.z = 0;
	b.w = 0;

	v[1].p = b;
	v[1].vbColor = 0xFFFFFFFF;
	v[1].tu = (float)desc.Width-1;
	v[1].tv = 0;

	c.x = cal_xoffset + D3D_PP.BackBufferWidth + cal_xstretch;;
	c.y = cal_yoffset + D3D_PP.BackBufferHeight + cal_ystretch;;
	c.z = 0;
	c.w = 0;

    v[2].p = c;
	v[2].vbColor = 0xFFFFFFFF;
	v[2].tu = (float)desc.Width-1;
	v[2].tv = (float)desc.Height-1;

	d.x = cal_xoffset;
	d.y = cal_yoffset + D3D_PP.BackBufferHeight + cal_ystretch;
	d.z = 0;
	d.w = 0;

    v[3].p = d;
	v[3].vbColor = 0xFFFFFFFF;
	v[3].tu = 0;
	v[3].tv = (float)desc.Height-1;


	IDirect3DVertexBuffer8_Unlock(SDL_vertexbuffer);
	IDirect3DDevice8_Clear(D3D_Device, 0, NULL, D3DCLEAR_TARGET , 0x00000000, 1.0f, 0L);

	IDirect3DDevice8_SetTexture(D3D_Device, 0, (D3DBaseTexture *)this->hidden->SDL_primary);
	IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP );
	//IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_MAGFILTER,D3DTEXF_LINEAR);
	//IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_MINFILTER,D3DTEXF_LINEAR);
	//IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_MIPFILTER,D3DTEXF_NONE);
	IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_ZENABLE,      TRUE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_FOGENABLE,    FALSE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_FOGTABLEMODE, D3DFOG_NONE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_FILLMODE,     D3DFILL_SOLID );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_CULLMODE,     D3DCULL_CCW );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_ALPHABLENDENABLE, TRUE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
    IDirect3DDevice8_SetVertexShader( D3D_Device, D3DFVF_XYZRHW|D3DFVF_TEX1|D3DFVF_DIFFUSE );

    // Render the image
    IDirect3DDevice8_SetStreamSource(D3D_Device,  0, SDL_vertexbuffer, 6*sizeof(FLOAT)  + sizeof(DWORD));
    IDirect3DDevice8_DrawPrimitive(D3D_Device,  D3DPT_QUADLIST, 0, 1 );

	IDirect3DDevice8_Present(D3D_Device,NULL,NULL,NULL,NULL);

	return (1);
}

static int XBOX_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
	HRESULT ret;
	ret = IDirect3DDevice8_Clear(D3D_Device, 0, NULL, D3DCLEAR_TARGET, color, 1.0f, 0L);

	if (ret == D3D_OK)
		return (1);
	else
		return (0);
}


static int XBOX_HWAccelBlit(SDL_Surface *src, SDL_Rect *srcrect,
					SDL_Surface *dst, SDL_Rect *dstrect)
{
	return(1);
}

static int XBOX_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst)
{
	return(0);
}

/* We need to wait for vertical retrace on page flipped displays */
static int XBOX_LockHWSurface(_THIS, SDL_Surface *surface)
{
	HRESULT ret;
	D3DLOCKED_RECT d3dlr;

	if (!this->hidden->SDL_primary)
		return (-1);

	ret = IDirect3DTexture8_LockRect(this->hidden->SDL_primary, 0, &d3dlr, NULL, 0);

	surface->pitch = d3dlr.Pitch;
	surface->pixels = d3dlr.pBits;

	if (ret == D3D_OK)
		return(0);
	else
		return(-1);
}

static void XBOX_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	IDirect3DTexture8_UnlockRect(this->hidden->SDL_primary,0);

	return;
}

static void XBOX_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
	static int    i = 0;
	D3DSURFACE_DESC desc;
	RECT src;
	SDL_Vertex *v;

	D3DXVECTOR4 a;
	D3DXVECTOR4 b;
	D3DXVECTOR4 c;
	D3DXVECTOR4 d;

	if (!this->hidden->SDL_primary)
		return;

	src.top    = (LONG)rects[0].y;
	src.bottom = (LONG)rects[0].y+rects[i].h;
	src.left   = (LONG)rects[0].x;
	src.right  = (LONG)rects[0].x+rects[i].w;

	IDirect3DTexture8_GetLevelDesc(this->hidden->SDL_primary, 0, &desc);
	IDirect3DVertexBuffer8_Lock(SDL_vertexbuffer, 0, 0, (BYTE**)&v, 0L );

	a.x = cal_xoffset;
	a.y = cal_yoffset;
	a.z = 0;
	a.w = 0;

	v[0].p = a;
	v[0].vbColor = 0xFFFFFFFF;
	v[0].tu = 0;
	v[0].tv = 0;

	b.x = cal_xoffset + D3D_PP.BackBufferWidth + cal_xstretch;
	b.y = cal_yoffset;
	b.z = 0;
	b.w = 0;

	v[1].p = b;
	v[1].vbColor = 0xFFFFFFFF;
	v[1].tu = (float)desc.Width-1;
	v[1].tv = 0;

	c.x = cal_xoffset + D3D_PP.BackBufferWidth + cal_xstretch;;
	c.y = cal_yoffset + D3D_PP.BackBufferHeight + cal_ystretch;;
	c.z = 0;
	c.w = 0;

    v[2].p = c;
	v[2].vbColor = 0xFFFFFFFF;
	v[2].tu = (float)desc.Width-1;
	v[2].tv = (float)desc.Height-1;

	d.x = cal_xoffset;
	d.y = cal_yoffset + D3D_PP.BackBufferHeight + cal_ystretch;
	d.z = 0;
	d.w = 0;

    v[3].p = d;
	v[3].vbColor = 0xFFFFFFFF;
	v[3].tu = 0;
	v[3].tv = (float)desc.Height-1;

	IDirect3DVertexBuffer8_Unlock(SDL_vertexbuffer);

	IDirect3DDevice8_Clear(D3D_Device, 0, NULL, D3DCLEAR_TARGET , 0x00000000, 1.0f, 0L);

	IDirect3DDevice8_SetTexture(D3D_Device, 0, (D3DBaseTexture *)this->hidden->SDL_primary);
	IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE );
	IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 1, D3DTSS_COLOROP,   D3DTOP_DISABLE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP );
    IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP );
	//IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_MAGFILTER,D3DTEXF_LINEAR);
	//IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_MINFILTER,D3DTEXF_LINEAR);
	//IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_MIPFILTER,D3DTEXF_NONE);
	IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_ZENABLE,      TRUE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_FOGENABLE,    FALSE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_FOGTABLEMODE, D3DFOG_NONE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_FILLMODE,     D3DFILL_SOLID );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_CULLMODE,     D3DCULL_CCW );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_ALPHABLENDENABLE, TRUE );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA );
    IDirect3DDevice8_SetRenderState( D3D_Device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
    IDirect3DDevice8_SetVertexShader( D3D_Device, D3DFVF_XYZRHW|D3DFVF_TEX1|D3DFVF_DIFFUSE );

    // Render the image
    IDirect3DDevice8_SetStreamSource(D3D_Device,  0, SDL_vertexbuffer, 6*sizeof(FLOAT)  + sizeof(DWORD));
    IDirect3DDevice8_DrawPrimitive(D3D_Device,  D3DPT_QUADLIST, 0, 1 );

	IDirect3DDevice8_Present(D3D_Device,NULL,NULL,NULL,NULL);


}

int XBOX_SetColors(_THIS, int firstcolor, int ncolors, SDL_Color *colors)
{
	return(1);
}

/* Note:  If we are terminated, this could be called in the middle of
   another SDL video routine -- notably UpdateRects.
*/
void XBOX_VideoQuit(_THIS)
{
	 if (this->hidden->SDL_primary)
		IDirect3DTexture8_Release(this->hidden->SDL_primary);

	 this->screen->pixels = NULL;

	 if(vid_modes)
		 free((void *)vid_modes);
}

static int XBOX_SetHWAlpha(_THIS, SDL_Surface *surface, Uint8 alpha)
{
	return(1);
}

static int XBOX_SetHWColorKey(_THIS, SDL_Surface *surface, Uint32 key)
{
	IDirect3DDevice8_SetTextureStageState(D3D_Device, 0, D3DTSS_COLORKEYCOLOR, key );
	return(0);
}

static int XBOX_SetFlickerFilter(_THIS, SDL_Surface *surface, int filter)
{
	IDirect3DDevice8_SetFlickerFilter(D3D_Device, filter);
	return(0);
}

static int XBOX_SetSoftDisplayFilter(_THIS, SDL_Surface *surface, int enabled)
{
	IDirect3DDevice8_SetSoftDisplayFilter(D3D_Device, enabled);
	return(0);
}

