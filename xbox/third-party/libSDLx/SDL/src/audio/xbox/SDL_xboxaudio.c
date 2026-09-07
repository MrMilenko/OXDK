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
 "@(#) $Id: SDL_xboxaudio.c,v 1.1 2003/07/18 15:19:33 lantus Exp $";
#endif

/* Allow access to a raw mixing buffer */

#include <stdio.h>

#include "SDL_types.h"
#include "SDL_error.h"
#include "SDL_timer.h"
#include "SDL_audio.h"
#include "SDL_audio_c.h"
#include "SDL_xboxaudio.h"

/* Define this if you want to use DirectX 6 DirectSoundNotify interface */
//#define USE_POSITION_NOTIFY

/* Audio driver functions */
static int XboxDX_OpenAudio(_THIS, SDL_AudioSpec *spec);
static void XboxDX_ThreadInit(_THIS);
static void XboxDX_WaitAudio_BusyWait(_THIS);
#ifdef USE_POSITION_NOTIFY
static void XboxDX_WaitAudio_EventWait(_THIS);
#endif
static void XboxDX_PlayAudio(_THIS);
static Uint8 *XboxDX_GetAudioBuf(_THIS);
static void XboxDX_WaitDone(_THIS);
static void XboxDX_CloseAudio(_THIS);

/* Audio driver bootstrap functions */

LPDIRECTSOUND g_sound;

static int Audio_Available(void)
{
	// Audio is always available on Xbox
	return(1);
}

/* Functions for loading the DirectX functions dynamically */
static HINSTANCE DSoundDLL = NULL;

static void XboxDX_Unload(void)
{
	// do nothing
}
static int XboxDX_Load(void)
{	
	return 1;
}

static void Audio_DeleteDevice(SDL_AudioDevice *device)
{
	 if (device->hidden)
	 {
		 free(device->hidden);
		 device->hidden = NULL;
	 }
}

static SDL_AudioDevice *Audio_CreateDevice(int devindex)
{
	HRESULT result;
	SDL_AudioDevice *this;

	/* Load DirectX */
	if ( XboxDX_Load() < 0 ) {
		return(NULL);
	}

	/* Initialize all variables that we clean on shutdown */
	this = (SDL_AudioDevice *)malloc(sizeof(SDL_AudioDevice));
	if ( this ) {
		memset(this, 0, (sizeof *this));
		this->hidden = (struct SDL_PrivateAudioData *)
				malloc((sizeof *this->hidden));
	}
	if ( (this == NULL) || (this->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( this ) {
			free(this);
		}
		return(0);
	}
	memset(this->hidden, 0, (sizeof *this->hidden));

	/* Set the function pointers */
	this->OpenAudio = XboxDX_OpenAudio;
	this->ThreadInit = XboxDX_ThreadInit;
	this->WaitAudio = XboxDX_WaitAudio_BusyWait;
	this->PlayAudio = XboxDX_PlayAudio;
	this->GetAudioBuf = XboxDX_GetAudioBuf;
	this->WaitDone = XboxDX_WaitDone;
	this->CloseAudio = XboxDX_CloseAudio;

	this->free = Audio_DeleteDevice;

	/* Open the audio device */
	result = DirectSoundCreate(NULL, &sound, NULL);
	if ( result != DS_OK ) {
		return(0);
	}

	g_sound = sound;

	return this;
}

AudioBootStrap DSOUND_bootstrap = {
	"dsound", "XBOX DirectSound",
	Audio_Available, Audio_CreateDevice
};

static void SetDSerror(const char *function, int code)
{
	static const char *error;
	static char  errbuf[1024];

	errbuf[0] = 0;
	switch (code) {
		case E_NOINTERFACE:
			error = 
		"Unsupported interface\n-- Is DirectX 5.0 or later installed?";
			break;
 		case DSERR_CONTROLUNAVAIL:
			error = "Control requested is not available";
			break;
		case DSERR_INVALIDCALL:
			error = "Invalid call for the current state";
			break;
 		case DSERR_NODRIVER:
			error = "No audio device found";
			break;
		case DSERR_OUTOFMEMORY:
			error = "Out of memory";
			break;
 		case DSERR_UNSUPPORTED:
			error = "Function not supported";
			break;
		default:
			sprintf(errbuf, "%s: Unknown DirectSound error: 0x%x",
								function, code);
			break;
	}
	if ( ! errbuf[0] ) {
		sprintf(errbuf, "%s: %s", function, error);
	}
	SDL_SetError("%s", errbuf);
	return;
}

/* DirectSound needs to be associated with a window */
static HWND mainwin = NULL;
/* */
void XboxDX_SoundFocus(int nHwnd)
{
	mainwin = (HWND)nHwnd;
}

static void XboxDX_ThreadInit(_THIS)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}

static void XboxDX_WaitAudio_BusyWait(_THIS)
{
	DWORD status;
	DWORD cursor, junk;
	HRESULT result;

	/* Semi-busy wait, since we have no way of getting play notification
	   on a primary mixing buffer located in hardware (DirectX 5.0)
	*/

	if (!mixbuf)
		return;

	result = IDirectSoundBuffer_GetCurrentPosition(mixbuf, &cursor, &junk);
	if ( result != DS_OK ) {

#ifdef DEBUG_SOUND
		SetDSerror("DirectSound GetCurrentPosition", result);
#endif
		return;
	}
	cursor /= mixlen;

	while ( cursor == playing ) {
		/* FIXME: find out how much time is left and sleep that long */
		SDL_Delay(10);

		/* Try to restore a lost sound buffer */
		IDirectSoundBuffer_GetStatus(mixbuf, &status);
 
		if ( ! (status&DSBSTATUS_PLAYING) ) {
			result = IDirectSoundBuffer_Play(mixbuf, 0, 0, DSBPLAY_LOOPING);
			if ( result == DS_OK ) {
				continue;
			}
#ifdef DEBUG_SOUND
			SetDSerror("DirectSound Play", result);
#endif
			return;
		}

		/* Find out where we are playing */
		result = IDirectSoundBuffer_GetCurrentPosition(mixbuf,
								&cursor, &junk);
		if ( result != DS_OK ) {
			SetDSerror("DirectSound GetCurrentPosition", result);
			return;
		}
		cursor /= mixlen;
	}
}

#ifdef USE_POSITION_NOTIFY
static void XboxDX_WaitAudio_EventWait(_THIS)
{
	DWORD status;
	HRESULT result;
	
	if (!mixbuf)
		return;

	/* Try to restore a lost sound buffer */
	IDirectSoundBuffer_GetStatus(mixbuf, &status);
	 
	if ( ! (status&DSBSTATUS_PLAYING) ) {
		result = IDirectSoundBuffer_Play(mixbuf, 0, 0, DSBPLAY_LOOPING);
		if ( result != DS_OK ) {
#ifdef DEBUG_SOUND
			SetDSerror("DirectSound Play", result);
#endif
			return;
		}
	}
	WaitForSingleObject(audio_event, INFINITE);
}
#endif /* USE_POSITION_NOTIFY */

static void XboxDX_PlayAudio(_THIS)
{
	/* Unlock the buffer, allowing it to play */
	if ( locked_buf ) {
		IDirectSoundBuffer_Unlock(mixbuf, locked_buf, mixlen, NULL, 0);
	}

}

static Uint8 *XboxDX_GetAudioBuf(_THIS)
{
	DWORD   cursor, junk;
	HRESULT result;
	DWORD   rawlen;

	/* Figure out which blocks to fill next */
	locked_buf = NULL;
	result = IDirectSoundBuffer_GetCurrentPosition(mixbuf, &cursor, &junk);
	if ( result != DS_OK ) {
		SetDSerror("DirectSound GetCurrentPosition", result);
		return(NULL);
	}
	cursor /= mixlen;
	playing = cursor;
	cursor = (cursor+1)%NUM_BUFFERS;
	cursor *= mixlen;

	/* Lock the audio buffer */
	result = IDirectSoundBuffer_Lock(mixbuf, cursor, mixlen,
				(LPVOID *)&locked_buf, &rawlen, NULL, NULL, 0);
 
	if ( result != DS_OK ) {
		SetDSerror("DirectSound Lock", result);
		return(NULL);
	}
	return(locked_buf);
}

static void XboxDX_WaitDone(_THIS)
{
	Uint8 *stream;
	DWORD BufferStatus;

	/* Wait for the playing chunk to finish */
	stream = this->GetAudioBuf(this);
	if ( stream != NULL ) {
		memset(stream, silence, mixlen);
		this->PlayAudio(this);
	}
	this->WaitAudio(this);

	if (!mixbuf)
		return;

	/* Stop the looping sound buffer */
	IDirectSoundBuffer_Stop(mixbuf);

	BufferStatus = DSBSTATUS_PLAYING;
    while (BufferStatus == DSBSTATUS_PLAYING)
		IDirectSoundBuffer_GetStatus(mixbuf,&BufferStatus);
}

static void XboxDX_CloseAudio(_THIS)
{
	if ( sound != NULL ) {
		if ( mixbuf != NULL ) {
			/* Clean up the audio buffer */
			IDirectSoundBuffer_Release(mixbuf);
			mixbuf = NULL;
		}
		if ( audio_event != NULL ) {
			CloseHandle(audio_event);
			audio_event = NULL;
		}
		IDirectSound_Release(sound);
		sound = NULL;
	}
}

#ifdef USE_PRIMARY_BUFFER
/* This function tries to create a primary audio buffer, and returns the
   number of audio chunks available in the created buffer.
*/
static int CreatePrimary(LPDIRECTSOUND sndObj, HWND focus, 
	LPDIRECTSOUNDBUFFER *sndbuf, WAVEFORMATEX *wavefmt, Uint32 chunksize)
{
	HRESULT result;
	DSBUFFERDESC format;
	DSBCAPS caps;
	int numchunks;

	/* Try to set primary mixing privileges */
	result = IDirectSound_SetCooperativeLevel(sndObj, focus,
							DSSCL_WRITEPRIMARY);
	if ( result != DS_OK ) {
#ifdef DEBUG_SOUND
		SetDSerror("DirectSound SetCooperativeLevel", result);
#endif
		return(-1);
	}

	/* Try to create the primary buffer */
	memset(&format, 0, sizeof(format));
	format.dwSize = sizeof(format);
	format.dwFlags=(DSBCAPS_PRIMARYBUFFER|DSBCAPS_GETCURRENTPOSITION2);
	format.dwFlags |= DSBCAPS_STICKYFOCUS;
#ifdef USE_POSITION_NOTIFY
	format.dwFlags |= DSBCAPS_CTRLPOSITIONNOTIFY;
#endif
	result = IDirectSound_CreateSoundBuffer(sndObj, &format, sndbuf, NULL);
	if ( result != DS_OK ) {
#ifdef DEBUG_SOUND
		SetDSerror("DirectSound CreateSoundBuffer", result);
#endif
		return(-1);
	}

	/* Check the size of the fragment buffer */
	memset(&caps, 0, sizeof(caps));
	caps.dwSize = sizeof(caps);
	result = IDirectSoundBuffer_GetCaps(*sndbuf, &caps);
	if ( result != DS_OK ) {
#ifdef DEBUG_SOUND
		SetDSerror("DirectSound GetCaps", result);
#endif
		IDirectSoundBuffer_Release(*sndbuf);
		return(-1);
	}
	if ( (chunksize > caps.dwBufferBytes) ||
				((caps.dwBufferBytes%chunksize) != 0) ) {
		/* The primary buffer size is not a multiple of 'chunksize'
		   -- this hopefully doesn't happen when 'chunksize' is a 
		      power of 2.
		*/
		IDirectSoundBuffer_Release(*sndbuf);
		SDL_SetError(
"Primary buffer size is: %d, cannot break it into chunks of %d bytes\n",
					caps.dwBufferBytes, chunksize);
		return(-1);
	}
	numchunks = (caps.dwBufferBytes/chunksize);

	/* Set the primary audio format */
	result = IDirectSoundBuffer_SetFormat(*sndbuf, wavefmt);
	if ( result != DS_OK ) {
#ifdef DEBUG_SOUND
		SetDSerror("DirectSound SetFormat", result);
#endif
		IDirectSoundBuffer_Release(*sndbuf);
		return(-1);
	}
	return(numchunks);
}
#endif /* USE_PRIMARY_BUFFER */

/* This function tries to create a secondary audio buffer, and returns the
   number of audio chunks available in the created buffer.
*/
static int CreateSecondary(LPDIRECTSOUND sndObj, int focus,
	LPDIRECTSOUNDBUFFER *sndbuf, WAVEFORMATEX *wavefmt, Uint32 chunksize)
{
	const int numchunks = 2;
	HRESULT result;
	DSBUFFERDESC format;
	LPVOID pvAudioPtr1, pvAudioPtr2;
	DWORD  dwAudioBytes1, dwAudioBytes2;
	DSMIXBINVOLUMEPAIR dsmbvp[8] = {
    {DSMIXBIN_FRONT_LEFT, DSBVOLUME_MAX},
    {DSMIXBIN_FRONT_RIGHT, DSBVOLUME_MAX},
    {DSMIXBIN_FRONT_CENTER, DSBVOLUME_MAX},
    {DSMIXBIN_FRONT_CENTER, DSBVOLUME_MAX},
    {DSMIXBIN_BACK_LEFT, DSBVOLUME_MAX},
    {DSMIXBIN_BACK_RIGHT, DSBVOLUME_MAX},
    {DSMIXBIN_LOW_FREQUENCY, DSBVOLUME_MAX},
    {DSMIXBIN_LOW_FREQUENCY, DSBVOLUME_MAX}};
    
	DSMIXBINS dsmb;

	/* Try to create the secondary buffer */
	memset(&format, 0, sizeof(format));
	format.dwSize = sizeof(format);
	format.dwFlags = 0;
#ifdef USE_POSITION_NOTIFY
	format.dwFlags |= DSBCAPS_CTRLPOSITIONNOTIFY;
#endif
	 
	format.dwBufferBytes = numchunks*chunksize;
	if ( (format.dwBufferBytes < DSBSIZE_MIN) ||
	     (format.dwBufferBytes > DSBSIZE_MAX) ) {
		SDL_SetError("Sound buffer size must be between %d and %d",
				DSBSIZE_MIN/numchunks, DSBSIZE_MAX/numchunks);
		return(-1);
	}

	format.lpwfxFormat = wavefmt;
	result = IDirectSound_CreateSoundBuffer(sndObj, &format, sndbuf, NULL);
	if ( result != DS_OK ) {
		SetDSerror("DirectSound CreateSoundBuffer", result);
		return(-1);
	}
	IDirectSoundBuffer_SetVolume(*sndbuf,DSBVOLUME_MAX);
	IDirectSoundBuffer_SetFormat(*sndbuf, wavefmt);

	
	dsmb.dwMixBinCount = 8;
	dsmb.lpMixBinVolumePairs = dsmbvp;

	IDirectSoundBuffer_SetHeadroom(*sndbuf, DSBHEADROOM_MIN);
	IDirectSoundBuffer_SetMixBins(*sndbuf, &dsmb);

	/* Silence the initial audio buffer */
	result = IDirectSoundBuffer_Lock(*sndbuf, 0, format.dwBufferBytes,
	                                 (LPVOID *)&pvAudioPtr1, &dwAudioBytes1,
	                                 (LPVOID *)&pvAudioPtr2, &dwAudioBytes2,
	                                 DSBLOCK_ENTIREBUFFER);
	if ( result == DS_OK ) {
		if ( wavefmt->wBitsPerSample == 8 ) {
			memset(pvAudioPtr1, 0x80, dwAudioBytes1);
		} else {
			memset(pvAudioPtr1, 0x00, dwAudioBytes1);
		}
		IDirectSoundBuffer_Unlock(*sndbuf,
		                          (LPVOID)pvAudioPtr1, dwAudioBytes1,
		                          (LPVOID)pvAudioPtr2, dwAudioBytes2);
	}

	/* We're ready to go */
	return(numchunks);
}

/* This function tries to set position notify events on the mixing buffer */
#ifdef USE_POSITION_NOTIFY
static int CreateAudioEvent(_THIS)
{
	DSBPOSITIONNOTIFY *notify_positions;
	int i, retval;
	HRESULT result;

	/* Default to fail on exit */
	retval = -1;
	 
	/* Allocate the notify structures */
	notify_positions = (DSBPOSITIONNOTIFY *)malloc(NUM_BUFFERS*
					sizeof(*notify_positions));
	if ( notify_positions == NULL ) {
		goto done;
	}

	/* Create the notify event */
	audio_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if ( audio_event == NULL ) {
		goto done;
	}

	/* Set up the notify structures */
	for ( i=0; i<NUM_BUFFERS; ++i ) {
		notify_positions[i].dwOffset = i*mixlen;
		notify_positions[i].hEventNotify = audio_event;
	}
	result = IDirectSoundNotify_SetNotificationPositions(mixbuf,
					NUM_BUFFERS, notify_positions);
	if ( result == DS_OK ) {
		retval = 0;
	}
done:
	//if ( notify != NULL ) {
	//	IDirectSoundNotify_Release(notify);
	//}
	return(retval);
}
#endif /* USE_POSITION_NOTIFY */

static int XboxDX_OpenAudio(_THIS, SDL_AudioSpec *spec)
{
	WAVEFORMATEX waveformat;

	mixbuf = NULL;

	/* Set basic WAVE format parameters */
	memset(&waveformat, 0, sizeof(waveformat));
	waveformat.wFormatTag = WAVE_FORMAT_PCM;

	/* Determine the audio parameters from the AudioSpec */
	switch ( spec->format & 0xFF ) {
		case 8:
			/* Unsigned 8 bit audio data */
			spec->format = AUDIO_U8;
			silence = 0x80;
			waveformat.wBitsPerSample = 8;
			break;
		case 16:
			/* Signed 16 bit audio data */
			spec->format = AUDIO_S16;
			silence = 0x00;
			waveformat.wBitsPerSample = 16;
			break;
		default:
			SDL_SetError("Unsupported audio format");
			return(-1);
	}
	waveformat.nChannels = spec->channels;
	waveformat.nSamplesPerSec = spec->freq;
	waveformat.nBlockAlign =
		waveformat.nChannels * (waveformat.wBitsPerSample/8);
	waveformat.nAvgBytesPerSec = 
		waveformat.nSamplesPerSec * waveformat.nBlockAlign;

	/* Update the fragment size as size in bytes */
	SDL_CalculateAudioSpec(spec);

	/* Create the audio buffer to which we write */
	NUM_BUFFERS = -1;
#ifdef USE_PRIMARY_BUFFER
	 
		NUM_BUFFERS = CreatePrimary(sound, 1, &mixbuf,
						&waveformat, spec->size);
	 
#endif /* USE_PRIMARY_BUFFER */
	if ( NUM_BUFFERS < 0 ) {
		NUM_BUFFERS = CreateSecondary(sound, 1, &mixbuf,
						&waveformat, spec->size);
		if ( NUM_BUFFERS < 0 ) {
			return(-1);
		}
#ifdef DEBUG_SOUND
		fprintf(stderr, "Using secondary audio buffer\n");
#endif
	}
#ifdef DEBUG_SOUND
	else
		fprintf(stderr, "Using primary audio buffer\n");
#endif

	/* The buffer will auto-start playing in DX5_WaitAudio() */
	playing = 0;
	mixlen = spec->size;

#ifdef USE_POSITION_NOTIFY
	/* See if we can use DirectX 6 event notification */
	if ( CreateAudioEvent(this) == 0 ) {
		this->WaitAudio = XboxDX_WaitAudio_EventWait;
	} else {
		this->WaitAudio = XboxDX_WaitAudio_BusyWait;
	}
#endif
	return(0);
}
