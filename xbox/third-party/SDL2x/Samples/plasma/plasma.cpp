#include <xtl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "SDL_test_common.h"
#include <string>
#include <iostream>

#define LOW_RES_WIDTH 160
#define LOW_RES_HEIGHT 120
#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
#define SINE_TABLE_SIZE 360

static SDLTest_CommonState* state;
int done;
SDL_Texture* plasmaTexture = NULL;
float sineTable[SINE_TABLE_SIZE];

// Precompute sine values
void InitSineTable() {
	for (int i = 0; i < SINE_TABLE_SIZE; i++) {
		sineTable[i] = sin(i * M_PI / 180.0f);
	}
}

// Get sine value from the table
float GetSine(int angle) {
	return sineTable[angle % SINE_TABLE_SIZE];
}

// Initialize the plasma effect texture
void InitPlasma(SDL_Renderer* renderer) {
	plasmaTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, LOW_RES_WIDTH, LOW_RES_HEIGHT);
	if (!plasmaTexture) {
		SDL_Log("Failed to create texture: %s", SDL_GetError());
		exit(1);
	}
}

// Update the plasma texture with optimized calculations
void UpdatePlasma(Uint32 time) {
	Uint32* pixels;
	int pitch;

	SDL_LockTexture(plasmaTexture, NULL, (void**)&pixels, &pitch);

	for (int y = 0; y < LOW_RES_HEIGHT; y++) {
		for (int x = 0; x < LOW_RES_WIDTH; x++) {
			int angle1 = (x + (time / 10)) % SINE_TABLE_SIZE;
			int angle2 = (y + (time / 15)) % SINE_TABLE_SIZE;
			int angle3 = ((x + y) + (time / 20)) % SINE_TABLE_SIZE;

			float value = GetSine(angle1) + GetSine(angle2) + GetSine(angle3);
			Uint8 color = (Uint8)((value + 3) * 42); // Normalize to 0-255

			// Set pixel color
			pixels[y * (pitch / 4) + x] = (color << 16) | (color / 2 << 8) | (255 - color);
		}
	}

	SDL_UnlockTexture(plasmaTexture);
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

		// Update plasma texture
		UpdatePlasma(time);

		// Clear screen
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		// Render plasma texture scaled up to full screen
		SDL_Rect dstRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
		SDL_RenderCopy(renderer, plasmaTexture, NULL, NULL);

		// Present the renderer
		SDL_RenderPresent(renderer);
	}
}

// Main entry point
int main(int argc, char* argv[]) {
	state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
	if (!state) {
		return 1;
	}
	if (!SDLTest_CommonInit(state)) {
		SDLTest_CommonQuit(state);
		return 2;
	}

	// Initialize sine table and plasma
	InitSineTable();
	InitPlasma(state->renderers[0]);

	done = 0;
	while (!done) {
		loop();
	}

	// Clean up
	if (plasmaTexture) {
		SDL_DestroyTexture(plasmaTexture);
	}
	SDLTest_CommonQuit(state);
	return 0;
}