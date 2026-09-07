#include <xtl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "SDL_test_common.h"

#define LOW_RES_WIDTH 320
#define LOW_RES_HEIGHT 240
#define SINE_TABLE_SIZE 360

static SDLTest_CommonState* state;
int done;
SDL_Texture* waveTexture = NULL;
Uint32 frameBuffer[LOW_RES_WIDTH][LOW_RES_HEIGHT] = { 0 };
float sineTable[SINE_TABLE_SIZE] = { 0 };

static int WINDOW_WIDTH = 640;
static int WINDOW_HEIGHT = 480;

// Precompute sine values
void InitSineTable() {
	for (int i = 0; i < SINE_TABLE_SIZE; i++) {
		sineTable[i] = (float) sin(i * M_PI / 180.0f);
	}
}

// Get sine value from the table
float GetSine(int angle) {
	return sineTable[angle % SINE_TABLE_SIZE];
}

// Initialize the texture and frame buffer
void InitWaves(SDL_Renderer* renderer) {
	waveTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, LOW_RES_WIDTH, LOW_RES_HEIGHT);
	if (!waveTexture) {
		SDL_Log("Failed to create texture: %s", SDL_GetError());
		exit(1);
	}

	// Clear frame buffer to black initially
	for (int y = 0; y < LOW_RES_HEIGHT; y++) {
		for (int x = 0; x < LOW_RES_WIDTH; x++) {
			frameBuffer[x][y] = 0; // Ensure all pixels are cleared
		}
	}
}

// Update the wave texture
void UpdateWaves(Uint32 time) {
	Uint32* pixels;
	int pitch;

	SDL_LockTexture(waveTexture, NULL, (void**)&pixels, &pitch);

	// Iterate through each pixel in the resolution
	for (int y = 0; y < LOW_RES_HEIGHT; y++) {
		for (int x = 0; x < LOW_RES_WIDTH; x++) {
			// Offset calculation for animation
			int offset = (time / 10 + x + y) % SINE_TABLE_SIZE;

			// Calculate colors dynamically
			Uint8 r = (Uint8)((GetSine(offset) + 1.0f) * 127);
			Uint8 g = (Uint8)(((float)x / LOW_RES_WIDTH) * 255);
			Uint8 b = (Uint8)(((float)y / LOW_RES_HEIGHT) * 255);

			frameBuffer[x][y] = (r << 16) | (g << 8) | b;
			pixels[y * (pitch / 4) + x] = frameBuffer[x][y];
		}
	}

	SDL_UnlockTexture(waveTexture);
}

// Main rendering loop
void loop() {
	SDL_Event event;

	// Handle events
	while (SDL_PollEvent(&event)) {
		SDLTest_CommonEvent(state, &event, &done);
	}

	for (int i = 0; i < state->num_windows; ++i) {
		SDL_Renderer* renderer = state->renderers[i];
		if (state->windows[i] == NULL) {
			continue;
		}

		Uint32 time = SDL_GetTicks();

		// Update wave texture
		UpdateWaves(time);

		// Clear the screen
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		// Render wave texture scaled to full screen
		SDL_Rect dstRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
		SDL_RenderCopy(renderer, waveTexture, NULL, NULL);

		// Present the renderer
		SDL_RenderPresent(renderer);
	}
}

// Main entry point
int main(int argc, char* argv[]) {
	srand((unsigned int)time(NULL));

	// Initialize SDLTest framework
	state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
	if (!state) {
		return 1;
	}
	if (!SDLTest_CommonInit(state)) {
		SDLTest_CommonQuit(state);
		return 2;
	}

	int displayIndex = 0; // Always 0 for the xbox
	int displayModeCt = SDL_GetNumDisplayModes(displayIndex);
	SDL_DisplayMode mode;
	
	SDL_Log("Available display modes\n");

	for (int j = 0; j < displayModeCt; ++j) {
		SDL_GetDisplayMode(displayIndex, j, &mode);
		if (j == 0) {
			SDL_Log(" ** Mode %d: %dx%d@%dHz\n", j, mode.w, mode.h, mode.refresh_rate);
		}
		else {
			SDL_Log("    Mode %d: %dx%d@%dHz\n", j, mode.w, mode.h, mode.refresh_rate);
		}
	}

	// Initialize sine table and waves
	InitSineTable();
	InitWaves(state->renderers[0]);

	done = 0;
	while (!done) {
		loop();
	}

	// Clean up
	if (waveTexture) {
		SDL_DestroyTexture(waveTexture);
	}
	SDLTest_CommonQuit(state);
	return 0;
}