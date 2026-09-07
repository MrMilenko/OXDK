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
 "@(#) $Id: SDL_xboxevents.c,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* Being a null driver, there's no event stream. We just define stubs for
   most of the API. */

#ifndef _XBOX_DONT_USE_DEVICES
#define DEBUG_KEYBOARD
#define DEBUG_MOUSE
#endif

#include <xtl.h>
#include <xkbd.h>

#include "SDL.h"
#include "SDL_sysevents.h"
#include "SDL_events_c.h"
#include "SDL_xboxvideo.h"
#include "SDL_xboxevents_c.h"


#define VK_bracketleft	0xdb
#define VK_bracketright	0xdd
#define VK_comma	0xbc
#define VK_period	0xbe
#define VK_slash	0xbf
#define VK_semicolon	0xba
#define VK_grave	0xc0
#define VK_minus	0xbd
#define VK_equal	0xbb
#define VK_quote	0xde
#define VK_backslash	0xdc


static HANDLE g_hKeyboardDevice[4] = { 0 };
static HANDLE g_hMouseDevice[4] = { 0 };

BOOL                     g_bDevicesInitialized  = FALSE;

#ifndef _XBOX_DONT_USE_DEVICES
static XINPUT_DEBUG_KEYSTROKE   g_keyboardStroke;
static XINPUT_MOUSE				g_MouseInput;
XINPUT_STATE g_MouseStates[4];
#endif

/* The translation table from a Xbox KB scancode to an SDL keysym */

static SDLKey DIK_keymap[256];
static SDL_keysym *TranslateKey(XINPUT_DEBUG_KEYSTROKE scancode, SDL_keysym *keysym, int pressed);

static void keyboard_update(void);
static void mouse_update(void);

VOID Mouse_RefreshDeviceList(void);


void XBOX_PumpEvents(_THIS)
{
#ifndef _XBOX_DONT_USE_DEVICES
	mouse_update();
	keyboard_update();
#endif
}

void XBOX_InitOSKeymap(_THIS)
{
#ifndef _XBOX_DONT_USE_DEVICES
	DWORD i;
	DWORD dwDeviceMask;

    XINPUT_DEBUG_KEYQUEUE_PARAMETERS keyboardSettings;

	/* Map the DIK scancodes to SDL keysyms */
	for ( i=0; i<SDL_TABLESIZE(DIK_keymap); ++i )
		DIK_keymap[i] = 0;

	/* Defined DIK_* constants */
	DIK_keymap[VK_ESCAPE] = SDLK_ESCAPE;
	DIK_keymap['1'] = SDLK_1;
	DIK_keymap['2'] = SDLK_2;
	DIK_keymap['3'] = SDLK_3;
	DIK_keymap['4'] = SDLK_4;
	DIK_keymap['5'] = SDLK_5;
	DIK_keymap['6'] = SDLK_6;
	DIK_keymap['7'] = SDLK_7;
	DIK_keymap['8'] = SDLK_8;
	DIK_keymap['9'] = SDLK_9;
	DIK_keymap['0'] = SDLK_0;
	DIK_keymap[VK_SUBTRACT] = SDLK_MINUS;
	DIK_keymap[VK_equal] = SDLK_EQUALS;
	DIK_keymap[VK_BACK] = SDLK_BACKSPACE;
	DIK_keymap[VK_TAB] = SDLK_TAB;
	DIK_keymap['Q'] = SDLK_q;
	DIK_keymap['W'] = SDLK_w;
	DIK_keymap['E'] = SDLK_e;
	DIK_keymap['R'] = SDLK_r;
	DIK_keymap['T'] = SDLK_t;
	DIK_keymap['Y'] = SDLK_y;
	DIK_keymap['U'] = SDLK_u;
	DIK_keymap['I'] = SDLK_i;
	DIK_keymap['O'] = SDLK_o;
	DIK_keymap['P'] = SDLK_p;
	DIK_keymap[VK_bracketleft] = SDLK_LEFTBRACKET;
	DIK_keymap[VK_bracketright] = SDLK_RIGHTBRACKET;
	DIK_keymap[VK_RETURN] = SDLK_RETURN;
	DIK_keymap[VK_LCONTROL] = SDLK_LCTRL;
	DIK_keymap['A'] = SDLK_a;
	DIK_keymap['S'] = SDLK_s;
	DIK_keymap['D'] = SDLK_d;
	DIK_keymap['F'] = SDLK_f;
	DIK_keymap['G'] = SDLK_g;
	DIK_keymap['H'] = SDLK_h;
	DIK_keymap['J'] = SDLK_j;
	DIK_keymap['K'] = SDLK_k;
	DIK_keymap['L'] = SDLK_l;
	DIK_keymap[VK_OEM_1] = SDLK_SEMICOLON;
	DIK_keymap[VK_OEM_7] = SDLK_QUOTE;
	DIK_keymap[VK_OEM_3] = SDLK_BACKQUOTE;
	DIK_keymap[VK_LSHIFT] = SDLK_LSHIFT;
	DIK_keymap[VK_backslash] = SDLK_BACKSLASH;
	DIK_keymap['Z'] = SDLK_z;
	DIK_keymap['X'] = SDLK_x;
	DIK_keymap['C'] = SDLK_c;
	DIK_keymap['V'] = SDLK_v;
	DIK_keymap['B'] = SDLK_b;
	DIK_keymap['N'] = SDLK_n;
	DIK_keymap['M'] = SDLK_m;
	DIK_keymap[VK_OEM_COMMA] = SDLK_COMMA;
	DIK_keymap[VK_OEM_PERIOD] = SDLK_PERIOD;
	DIK_keymap[VK_OEM_PLUS] = SDLK_PLUS;
	DIK_keymap[VK_OEM_MINUS] = SDLK_MINUS;
	DIK_keymap[VK_slash] = SDLK_SLASH;
	DIK_keymap[VK_RSHIFT] = SDLK_RSHIFT;
	DIK_keymap[VK_MULTIPLY] = SDLK_KP_MULTIPLY;
	DIK_keymap[VK_LMENU] = SDLK_LALT;
	DIK_keymap[VK_SPACE] = SDLK_SPACE;
	DIK_keymap[VK_CAPITAL] = SDLK_CAPSLOCK;
	DIK_keymap[VK_F1] = SDLK_F1;
	DIK_keymap[VK_F2] = SDLK_F2;
	DIK_keymap[VK_F3] = SDLK_F3;
	DIK_keymap[VK_F4] = SDLK_F4;
	DIK_keymap[VK_F5] = SDLK_F5;
	DIK_keymap[VK_F6] = SDLK_F6;
	DIK_keymap[VK_F7] = SDLK_F7;
	DIK_keymap[VK_F8] = SDLK_F8;
	DIK_keymap[VK_F9] = SDLK_F9;
	DIK_keymap[VK_F10] = SDLK_F10;
	DIK_keymap[VK_NUMLOCK] = SDLK_NUMLOCK;
	DIK_keymap[VK_SCROLL] = SDLK_SCROLLOCK;
	DIK_keymap[VK_NUMPAD7] = SDLK_KP7;
	DIK_keymap[VK_NUMPAD8] = SDLK_KP8;
	DIK_keymap[VK_NUMPAD9] = SDLK_KP9;
	DIK_keymap[VK_ADD]	   = SDLK_KP_PLUS;
	DIK_keymap[VK_SUBTRACT] = SDLK_KP_MINUS;
	DIK_keymap[VK_NUMPAD4] = SDLK_KP4;
	DIK_keymap[VK_NUMPAD5] = SDLK_KP5;
	DIK_keymap[VK_NUMPAD6] = SDLK_KP6;
	DIK_keymap[VK_NUMPAD1] = SDLK_KP1;
	DIK_keymap[VK_NUMPAD2] = SDLK_KP2;
	DIK_keymap[VK_NUMPAD3] = SDLK_KP3;
	DIK_keymap[VK_NUMPAD0] = SDLK_KP0;
	DIK_keymap[VK_DECIMAL] = SDLK_KP_PERIOD;
	DIK_keymap[VK_F11] = SDLK_F11;
	DIK_keymap[VK_F12] = SDLK_F12;

	DIK_keymap[VK_F13] = SDLK_F13;
	DIK_keymap[VK_F14] = SDLK_F14;
	DIK_keymap[VK_F15] = SDLK_F15;

	DIK_keymap[VK_equal] = SDLK_EQUALS;
	DIK_keymap[VK_RCONTROL] = SDLK_RCTRL;
	DIK_keymap[VK_DIVIDE] = SDLK_KP_DIVIDE;
	DIK_keymap[VK_RMENU] = SDLK_RALT;
	DIK_keymap[VK_PAUSE] = SDLK_PAUSE;
	DIK_keymap[VK_HOME] = SDLK_HOME;
	DIK_keymap[VK_UP] = SDLK_UP;
	DIK_keymap[VK_PRIOR] = SDLK_PAGEUP;
	DIK_keymap[VK_LEFT] = SDLK_LEFT;
	DIK_keymap[VK_RIGHT] = SDLK_RIGHT;
	DIK_keymap[VK_END] = SDLK_END;
	DIK_keymap[VK_DOWN] = SDLK_DOWN;
	DIK_keymap[VK_NEXT] = SDLK_PAGEDOWN;
	DIK_keymap[VK_INSERT] = SDLK_INSERT;
	DIK_keymap[VK_DELETE] = SDLK_DELETE;
	DIK_keymap[VK_LWIN] = SDLK_LMETA;
	DIK_keymap[VK_RWIN] = SDLK_RMETA;
	DIK_keymap[VK_APPS] = SDLK_MENU;

	if (!g_bDevicesInitialized)
		XInitDevices(0 ,NULL);


	Sleep( 1000 );
 
    keyboardSettings.dwFlags          = XINPUT_DEBUG_KEYQUEUE_FLAG_KEYDOWN|XINPUT_DEBUG_KEYQUEUE_FLAG_KEYREPEAT|XINPUT_DEBUG_KEYQUEUE_FLAG_KEYUP;
    keyboardSettings.dwQueueSize      = 25;
    keyboardSettings.dwRepeatDelay    = 500;
    keyboardSettings.dwRepeatInterval = 50;

    if( ERROR_SUCCESS != XInputDebugInitKeyboardQueue( &keyboardSettings ) )
        return;

    g_bDevicesInitialized = TRUE;

    // Now find the keyboard device, in this case we shall loop indefinitely, although
    // it would be better to monitor the time taken and to time out if necessary
    // in case the keyboard has been unplugged

    dwDeviceMask = XGetDevices( XDEVICE_TYPE_DEBUG_KEYBOARD );

    // Open the devices
    for( i=0; i < XGetPortCount(); i++ )
    {
        if( dwDeviceMask & (1<<i) ) 
        {
            // Now open the device
            XINPUT_POLLING_PARAMETERS pollValues;
            pollValues.fAutoPoll       = TRUE;
            pollValues.fInterruptOut   = TRUE;
            pollValues.bInputInterval  = 32;  
            pollValues.bOutputInterval = 32;
            pollValues.ReservedMBZ1    = 0;
            pollValues.ReservedMBZ2    = 0;

            g_hKeyboardDevice[i] = XInputOpen( XDEVICE_TYPE_DEBUG_KEYBOARD, i, 
                                               XDEVICE_NO_SLOT, &pollValues );
        }
    }

	// set up mouse

	dwDeviceMask = XGetDevices( XDEVICE_TYPE_DEBUG_MOUSE );

	// Open the devices
    for( i=0; i < XGetPortCount(); i++ )
    {
        if( dwDeviceMask & (1<<i) ) 
        {
            // Now open the device
            XINPUT_POLLING_PARAMETERS pollValues;
            pollValues.fAutoPoll       = TRUE;
            pollValues.fInterruptOut   = TRUE;
            pollValues.bInputInterval  = 32;  
            pollValues.bOutputInterval = 32;
            pollValues.ReservedMBZ1    = 0;
            pollValues.ReservedMBZ2    = 0;

            g_hMouseDevice[i] = XInputOpen( XDEVICE_TYPE_DEBUG_MOUSE, i, 
                                            XDEVICE_NO_SLOT, &pollValues );
        }
    }
#endif
}

CHAR XBInput_GetKeyboardInput()
{
#ifndef _XBOX_DONT_USE_DEVICES

    // Get status about gamepad insertions and removals. Note that, in order to
    // not miss devices, we will check for removed device BEFORE checking for
    // insertions
	DWORD i;
    DWORD dwInsertions, dwRemovals;
    XGetDeviceChanges( XDEVICE_TYPE_DEBUG_KEYBOARD, &dwInsertions, &dwRemovals );

    // Loop through all ports
    for( i=0; i < XGetPortCount(); i++ )
    {
        // Handle removed devices.
        if( dwRemovals & (1<<i) )
        {
            XInputClose( g_hKeyboardDevice[i] );
            g_hKeyboardDevice[i] = NULL;
        }

        // Handle inserted devices
        if( dwInsertions & (1<<i) )
        {
            // Now open the device
            XINPUT_POLLING_PARAMETERS pollValues;
            pollValues.fAutoPoll       = TRUE;
            pollValues.fInterruptOut   = TRUE;
            pollValues.bInputInterval  = 32;  
            pollValues.bOutputInterval = 32;
            pollValues.ReservedMBZ1    = 0;
            pollValues.ReservedMBZ2    = 0;

            g_hKeyboardDevice[i] = XInputOpen( XDEVICE_TYPE_DEBUG_KEYBOARD, i, 
                                               XDEVICE_NO_SLOT, &pollValues );
        }

        // If we have a valid device, poll it's state and track button changes
        if( g_hKeyboardDevice[i] )
        {
            if( ERROR_SUCCESS == XInputDebugGetKeystroke( &g_keyboardStroke ) )
                return g_keyboardStroke.Ascii;
        }
    }

#endif

    return '\0';
}


static void keyboard_update(void)
{
#ifndef _XBOX_DONT_USE_DEVICES

	SDL_keysym keysym;
	
	XBInput_GetKeyboardInput();

	if (g_keyboardStroke.VirtualKey==0)
		return;

	if ( g_keyboardStroke.Flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP ) {
		SDL_PrivateKeyboard(SDL_RELEASED, TranslateKey(g_keyboardStroke,&keysym, 1));
	} else {
		SDL_PrivateKeyboard(SDL_PRESSED,  TranslateKey(g_keyboardStroke,&keysym, 0));
	}

#endif
}


static void mouse_update(void)
{
#ifndef _XBOX_DONT_USE_DEVICES

	int i, j;
	static int prev_buttons;
	static int lastmouseX, lastmouseY;
	static DWORD lastdwPacketNum;
	DWORD dwPacketNum;
	int mouseX, mouseY;
	int buttons,changed;
	int wheel;

	const	static char sdl_mousebtn[] = {
	XINPUT_DEBUG_MOUSE_LEFT_BUTTON,
	XINPUT_DEBUG_MOUSE_MIDDLE_BUTTON,
	XINPUT_DEBUG_MOUSE_RIGHT_BUTTON
	};

	Mouse_RefreshDeviceList();
   
	 // Loop through all gamepads
    for( i=0; i < XGetPortCount(); i++ )
    {
        // If we have a valid device, poll it's state and track button changes
        if( g_hMouseDevice[i] )
        {
            // Read the input state
            XInputGetState( g_hMouseDevice[i], &g_MouseStates[i] );

			mouseX = mouseY = 0;

			dwPacketNum = g_MouseStates[i].dwPacketNumber;

            // Copy gamepad to local structure
            memcpy( &g_MouseInput, &g_MouseStates[i].DebugMouse, sizeof(XINPUT_MOUSE) );

			if ((lastmouseX != g_MouseInput.cMickeysX) ||
				(lastmouseY != g_MouseInput.cMickeysY))
			{
				mouseX = g_MouseInput.cMickeysX;
				mouseY = g_MouseInput.cMickeysY;
			}
			
			if (mouseX||mouseY)
				SDL_PrivateMouseMotion(0,1, mouseX, mouseY);

			buttons = g_MouseInput.bButtons;
			
			changed = buttons^prev_buttons;
 
			for(i=0;i<sizeof(sdl_mousebtn);i++) {
				if (changed & sdl_mousebtn[i]) {
					SDL_PrivateMouseButton((buttons & sdl_mousebtn[i])?SDL_PRESSED:SDL_RELEASED,i+1,0,0);
				}
			}

			wheel = g_MouseInput.cWheel;

			if(wheel && dwPacketNum != lastdwPacketNum)
			{
				for(j = 0; j < ((wheel > 0)?wheel:-wheel); j++)
				{
					int button = (wheel > 0)?SDL_BUTTON_WHEELUP:SDL_BUTTON_WHEELDOWN;

					SDL_PrivateMouseButton(SDL_PRESSED, button, 0, 0);
					SDL_PrivateMouseButton(SDL_RELEASED, button, 0, 0);
				}
			}

			prev_buttons = buttons;
			lastmouseX = g_MouseInput.cMickeysX;
			lastmouseY = g_MouseInput.cMickeysY;
			lastdwPacketNum = dwPacketNum;	
        }
    }
#endif
}

VOID Mouse_RefreshDeviceList()
{

#ifndef _XBOX_DONT_USE_DEVICES

        // Get status about gamepad insertions and removals. Note that, in order to
    // not miss devices, we will check for removed device BEFORE checking for
    // insertions

	DWORD i;
    DWORD dwInsertions, dwRemovals;
    XGetDeviceChanges( XDEVICE_TYPE_DEBUG_MOUSE, &dwInsertions, &dwRemovals );

    // Loop through all ports
    for( i=0; i < XGetPortCount(); i++ )
    {
        // Handle removed devices.
        if( dwRemovals & (1<<i) )
        {
            XInputClose( g_hMouseDevice[i] );
            g_hMouseDevice[i] = NULL;
        }

        // Handle inserted devices
        if( dwInsertions & (1<<i) )
        {
            // Now open the device
            XINPUT_POLLING_PARAMETERS pollValues;
            pollValues.fAutoPoll       = TRUE;
            pollValues.fInterruptOut   = TRUE;
            pollValues.bInputInterval  = 32;  
            pollValues.bOutputInterval = 32;
            pollValues.ReservedMBZ1    = 0;
            pollValues.ReservedMBZ2    = 0;

            g_hMouseDevice[i] = XInputOpen( XDEVICE_TYPE_DEBUG_MOUSE, i, 
                                            XDEVICE_NO_SLOT, &pollValues );
        }

         
    }

#endif 
}

 
static SDL_keysym *TranslateKey(XINPUT_DEBUG_KEYSTROKE scancode, SDL_keysym *keysym, int pressed)
{
	/* Set the keysym information */

	keysym->mod = KMOD_NONE;
	keysym->scancode = (unsigned char)scancode.VirtualKey;
	keysym->sym = DIK_keymap[scancode.VirtualKey];
	keysym->unicode = scancode.Ascii;
 
	return(keysym);
}
 