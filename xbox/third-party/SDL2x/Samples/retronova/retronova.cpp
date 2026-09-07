#include <xtl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "SDL_test_common.h"

#define WINDOW_WIDTH 640
#define WINDOW_HEIGHT 480
#define NUM_STARS 200
#define NUM_VERTICES 8
#define SINE_TABLE_SIZE 360

static SDLTest_CommonState* state;
int done;
float sineTable[SINE_TABLE_SIZE];

// Starfield data
typedef struct {
	float x, y, z;
	Uint8 r, g, b;
} Star;

Star stars[NUM_STARS];

// Initialize sine table for smooth animations
void InitSineTable() {
	for (int i = 0; i < SINE_TABLE_SIZE; i++) {
		sineTable[i] = sin(i * M_PI / 180.0f);
	}
}

// Get sine value from the table
float GetSine(int angle) {
	return sineTable[angle % SINE_TABLE_SIZE];
}

// Initialize stars for scrolling starfield
void InitStars() {
	for (int i = 0; i < NUM_STARS; i++) {
		stars[i].x = (float)(rand() % WINDOW_WIDTH - WINDOW_WIDTH / 2);
		stars[i].y = (float)(rand() % WINDOW_HEIGHT - WINDOW_HEIGHT / 2);
		stars[i].z = (float)(rand() % 200 + 1); // Start at random depth
		stars[i].r = rand() % 256;
		stars[i].g = rand() % 256;
		stars[i].b = rand() % 256;
	}
}

// Update star positions for scrolling effect
void UpdateStars() {
	for (int i = 0; i < NUM_STARS; i++) {
		stars[i].z -= 2; // Move stars closer
		if (stars[i].z <= 0) {
			stars[i].x = (float)(rand() % WINDOW_WIDTH - WINDOW_WIDTH / 2);
			stars[i].y = (float)(rand() % WINDOW_HEIGHT - WINDOW_HEIGHT / 2);
			stars[i].z = 200; // Reset to farthest depth
		}
	}
}

// Draw the starfield
void DrawStars(SDL_Renderer* renderer) {
	for (int i = 0; i < NUM_STARS; i++) {
		// Project 3D coordinates to 2D screen space
		int screen_x = (int)((stars[i].x / stars[i].z) * 100 + WINDOW_WIDTH / 2);
		int screen_y = (int)((stars[i].y / stars[i].z) * 100 + WINDOW_HEIGHT / 2);

		// Scale star size based on depth
		int size = (int)((1.0f - stars[i].z / 200.0f) * 3);

		// Set star color and draw
		SDL_SetRenderDrawColor(renderer, stars[i].r, stars[i].g, stars[i].b, 255);
		SDL_Rect rect = { screen_x - size / 2, screen_y - size / 2, size, size };
		SDL_RenderFillRect(renderer, &rect);
	}
}

// Draw rotating 3D cube
void Draw3DCube(SDL_Renderer* renderer, Uint32 time) {
	static float vertices[NUM_VERTICES][3] = {
		{-50, -50, -50}, {50, -50, -50}, {50, 50, -50}, {-50, 50, -50},
		{-50, -50,  50}, {50, -50,  50}, {50, 50,  50}, {-50, 50,  50}
	};
	static int edges[12][2] = {
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	// Rotate the cube
	float angle = time / 1000.0f;
	float cosA = cos(angle), sinA = sin(angle);

	for (int i = 0; i < 12; i++) {
		int v0 = edges[i][0];
		int v1 = edges[i][1];

		float x0 = vertices[v0][0], y0 = vertices[v0][1], z0 = vertices[v0][2];
		float x1 = vertices[v1][0], y1 = vertices[v1][1], z1 = vertices[v1][2];

		// Apply rotation
		float tempX0 = x0 * cosA - z0 * sinA, tempZ0 = x0 * sinA + z0 * cosA;
		float tempX1 = x1 * cosA - z1 * sinA, tempZ1 = x1 * sinA + z1 * cosA;
		x0 = tempX0; z0 = tempZ0;
		x1 = tempX1; z1 = tempZ1;

		// Project 3D to 2D
		int screen_x0 = (int)((x0 / (z0 + 200)) * 300 + WINDOW_WIDTH / 2);
		int screen_y0 = (int)((y0 / (z0 + 200)) * 300 + WINDOW_HEIGHT / 2);
		int screen_x1 = (int)((x1 / (z1 + 200)) * 300 + WINDOW_WIDTH / 2);
		int screen_y1 = (int)((y1 / (z1 + 200)) * 300 + WINDOW_HEIGHT / 2);

		SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
		SDL_RenderDrawLine(renderer, screen_x0, screen_y0, screen_x1, screen_y1);
	}
}

// Draw a thicker colorful sine wave
void DrawSineWave(SDL_Renderer* renderer, Uint32 time) {
	int waveAmplitude = 100;
	int waveFrequency = 6;
	int thickness = 3; // Thickness of the sine wave

	for (int x = 0; x < WINDOW_WIDTH; x++) {
		int y = (int)(WINDOW_HEIGHT / 2 +
			waveAmplitude * GetSine((x * waveFrequency + time / 5) % SINE_TABLE_SIZE));

		Uint8 r = (Uint8)((GetSine((x + time / 10) % SINE_TABLE_SIZE) + 1.0f) * 127);
		Uint8 g = (Uint8)((GetSine((x + time / 15) % SINE_TABLE_SIZE) + 1.0f) * 127);
		Uint8 b = (Uint8)((GetSine((x + time / 20) % SINE_TABLE_SIZE) + 1.0f) * 127);

		SDL_SetRenderDrawColor(renderer, r, g, b, 255);

		// Draw thicker sine wave by adding vertical lines
		for (int t = -thickness / 2; t <= thickness / 2; t++) {
			SDL_RenderDrawPoint(renderer, x, y + t);
		}
	}
}

// Main loop
void loop() {
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		SDLTest_CommonEvent(state, &event, &done);
	}

	for (int i = 0; i < state->num_windows; i++) {
		SDL_Renderer* renderer = state->renderers[i];
		if (!renderer) continue;

		Uint32 time = SDL_GetTicks();

		// Clear the screen
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderClear(renderer);

		// Draw visuals
		UpdateStars();
		DrawStars(renderer);
		DrawSineWave(renderer, time);
		Draw3DCube(renderer, time);

		SDL_RenderPresent(renderer);
	}
}

// Main entry point
int main(int argc, char* argv[]) {
	srand((unsigned int)time(NULL));

	state = SDLTest_CommonCreateState(argv, SDL_INIT_VIDEO);
	if (!state) return 1;
	if (!SDLTest_CommonInit(state)) return 2;

	InitSineTable();
	InitStars();

	done = 0;
	while (!done) {
		loop();
	}

	SDLTest_CommonQuit(state);
	return 0;
}