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
 "@(#) $Id: SDL_xboxvideo.h,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

#ifndef _SDL_nullvideo_h
#define _SDL_nullvideo_h

#include <xtl.h>

#include "SDL_mouse.h"
#include "SDL_sysvideo.h"
#include "SDL_mutex.h"

/* Hidden "this" pointer for the video functions */
#define _THIS	SDL_VideoDevice *this

/* Private display data */

struct SDL_PrivateVideoData {
	LPDIRECT3DTEXTURE8 SDL_primary;
};

LPDIRECT3D8 D3D;
LPDIRECT3DDEVICE8 D3D_Device;
D3DPRESENT_PARAMETERS D3D_PP;
LPDIRECT3DVERTEXBUFFER8 SDL_vertexbuffer;
PALETTEENTRY SDL_colors[256];
LPDIRECT3DPALETTE8 D3D_Palette;

#endif /* _SDL_nullvideo_h */

 