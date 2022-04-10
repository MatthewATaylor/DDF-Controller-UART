#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>
#include <sys/timeb.h>

#include <windows.h>

#define PI 3.14159265

// LED_ROWS is actually half the real number of rows for performance purposes
#define LED_ROWS 36
#define FULL_LED_ROWS 72
#define LED_COLS 165

#define NUM_KEYS 128
#define CMD_BYTE 255

// Serial command codes
#define SET_ROWS_COLOR_CODE 22
#define SET_PONG_DATA_CODE 23
#define SET_PONG_SCORE_CODE 24

#define RAINBOW_PERIOD_MS 800

#define COLOR_CHANGE_THRESHOLD 0.1
#define MIN_AUDIO_LEVEL 0

// focus = WAVE_SPEED * t
#define WAVE_SPEED 0.1

#define WAVE_SIZE 16
#define MAX_NUM_WAVES 4

#define SIN_LUT_SAMPLES 4096

// Pong
#define PADDLE_WIDTH 5
#define PADDLE_HEIGHT 16
#define PADDLE_SPEED 0.00006
#define BALL_WIDTH 6
#define BALL_HEIGHT 3
#define BALL_SPEED 0.000075
#define MAX_BALL_ANGLE 0.9

enum ColorMode {
	RAINBOW,
	RED,
	ORANGE,
	YELLOW,
	GREEN,
	BLUE,
	PURPLE,
	WHITE,
	RED_BLUE,
	GREEN_BLUE
};

enum AnimationMode {
	ANIMATION_OFF,
	ANIMATION_SOLID,
	ANIMATION_WAVE,
	ANIMATION_RAINBOW,
	ANIMATION_ALTERNATING,
	ANIMATION_PONG
};

enum WaveDirection {
	WAVE_DIR_UP,
	WAVE_DIR_DOWN
};

struct RGBColor {
	uint8_t r;  // [0, 255]
	uint8_t g;  // [0, 255]
	uint8_t b;  // [0, 255]
};
struct HSVColor {
	double h;  // [0, 360]
	double s;  // [0, 1]
	double v;  // [0, 1]
};

struct WaveData {
	int16_t focus;
	long animationStartingTime;
	enum WaveDirection direction;
	uint8_t animationIsFinished;
};

struct Paddle {
	uint8_t score;
	double y;
	struct RGBColor color;
};

struct Ball {
	double x, y;
	double vx, vy;
	struct RGBColor color;
};

struct RGBColor rowColors[LED_ROWS];
double sinLut[SIN_LUT_SAMPLES];

const struct RGBColor red = { 50, 0, 0 };
const struct RGBColor orange = { 49, 5, 0 };
const struct RGBColor yellow = { 30, 20, 0 };
const struct RGBColor green = { 0, 50, 0 };
const struct RGBColor blue = { 0, 0, 50 };
const struct RGBColor purple = { 25, 0, 25 };
const struct RGBColor white = { 16, 16, 16 };

unsigned long long pongStart = 0;
unsigned long long pongEnd = 0;

struct RGBColor hsvToRgb(struct HSVColor* color) {
	double c = color->v * color->s;
	double hPrime = color->h / 60.0f;
	double x = c * (1 - fabs(fmod(hPrime, 2.0) - 1));
	double r1, g1, b1;
	if (hPrime >= 5) {
		r1 = c;
		g1 = 0;
		b1 = x;
	}
	else if (hPrime >= 4) {
		r1 = x;
		g1 = 0;
		b1 = c;
	}
	else if (hPrime >= 3) {
		r1 = 0;
		g1 = x;
		b1 = c;
	}
	else if (hPrime >= 2) {
		r1 = 0;
		g1 = c;
		b1 = x;
	}
	else if (hPrime >= 1) {
		r1 = x;
		g1 = c;
		b1 = 0;
	}
	else {
		r1 = c;
		g1 = x;
		b1 = 0;
	}
	double m = color->v * color->s;
	struct RGBColor result;
	result.r = (uint8_t) ((r1 + m) * 255);
	result.g = (uint8_t) ((g1 + m) * 255);
	result.b = (uint8_t) ((b1 + m) * 255);
	return result;
}

unsigned long long getMicros() {
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	unsigned long long tt = ft.dwHighDateTime;
	tt <<= 32;
	tt |= ft.dwLowDateTime;
	tt /= 10;
	return tt;
}

void initSinLut() {
	// Precompute sin for better performance
	for (uint16_t i = 0; i < SIN_LUT_SAMPLES; ++i) {
		sinLut[i] = sin((double) i / SIN_LUT_SAMPLES * 2 * PI);
	}
}

double getSinLut(double theta) {
	while (theta >= 2 * PI) {
		theta -= 2 * PI;
	}
	while (theta < 0) {
		theta += 2 * PI;
	}
	return sinLut[(uint16_t) (theta / (2 * PI) * SIN_LUT_SAMPLES)];
}

double getCosLut(double theta) {
	return getSinLut(theta + PI / 2.0);
}

void resetPong(struct Paddle *paddle1, struct Paddle *paddle2, struct Ball *ball, uint8_t serverIs1) {
	paddle1->y = FULL_LED_ROWS / 2.0 - PADDLE_HEIGHT / 2.0;
	paddle2->y = FULL_LED_ROWS / 2.0 - PADDLE_HEIGHT / 2.0;
	ball->x = LED_COLS / 2.0 - BALL_WIDTH / 2.0;
	ball->y = FULL_LED_ROWS / 2.0 - BALL_WIDTH / 2.0;
	
	if (serverIs1) {
		ball->vx = -BALL_SPEED;
	}
	else {
		ball->vx = BALL_SPEED;
	}
	
	ball->vy = 0;

	pongStart = getMicros();
	pongEnd = pongStart;
}

void resetPongAndScore(struct Paddle* paddle1, struct Paddle* paddle2, struct Ball* ball) {
	resetPong(paddle1, paddle2, ball, 1);
	paddle1->score = 0;
	paddle2->score = 0;
}

// Write contents of global rowColors array to FPGA
void setRowColors(HANDLE hSerial) {
	uint8_t packet[3 * LED_ROWS + 2];

	packet[0] = CMD_BYTE;
	packet[1] = SET_ROWS_COLOR_CODE;
	for (uint8_t i = 0; i < LED_ROWS; ++i) {
		packet[3 * i + 2] = rowColors[i].g;
		packet[3 * i + 3] = rowColors[i].r;
		packet[3 * i + 4] = rowColors[i].b;
	}

	DWORD bytesWritten;
	WriteFile(hSerial, packet, 3 * LED_ROWS + 2, &bytesWritten, NULL);
}

// Fill global rowColors array with color and write to FPGA
void setColor(HANDLE hSerial, struct RGBColor* color) {
	for (uint8_t i = 0; i < LED_ROWS; ++i) {
		rowColors[i].r = color->r;
		rowColors[i].g = color->g;
		rowColors[i].b = color->b;
	}
	setRowColors(hSerial);
}

// Send updated pong game state to FPGA (every frame)
void setPongData(HANDLE hSerial, struct Paddle *paddle1, struct Paddle *paddle2, struct Ball *ball) {
	uint8_t packet[6];
	packet[0] = CMD_BYTE;
	packet[1] = SET_PONG_DATA_CODE;
	packet[2] = (uint8_t) paddle1->y;
	packet[3] = (uint8_t) paddle2->y;
	packet[4] = (uint8_t) ball->x;
	packet[5] = (uint8_t) ball->y;

	DWORD bytesWritten;
	WriteFile(hSerial, packet, 6, &bytesWritten, NULL);
}

// Send updated pong score to FPGA (when point is scored)
void setPongScore(HANDLE hSerial, uint8_t score1, uint8_t score2) {
	uint8_t packet[4];
	packet[0] = CMD_BYTE;
	packet[1] = SET_PONG_SCORE_CODE;
	packet[2] = score1;
	packet[3] = score2;

	DWORD bytesWritten;
	WriteFile(hSerial, packet, 4, &bytesWritten, NULL);
}

// Fill global rowColors array with zeros and write to FPGA
void setOff(HANDLE hSerial) {
	struct RGBColor color = { 0, 0, 0 };
	setColor(hSerial, &color);
}

HANDLE connectSerial(LPCSTR port) {
	// Open serial port using Windows API

	HANDLE hSerial = CreateFileA(port, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hSerial == INVALID_HANDLE_VALUE) {
		printf("ERROR: Failed to open serial port %s\n", port);
	}
	else {
		printf("Successfully opened serial port %s\n", port);
	}

	COMMTIMEOUTS timeouts = { 0 };
	timeouts.ReadIntervalTimeout = 0;
	timeouts.ReadTotalTimeoutConstant = 100;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 100;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	SetCommTimeouts(hSerial, &timeouts);

	DCB state = { 0 };
	state.DCBlength = sizeof(DCB);
	state.BaudRate = 115200;
	state.ByteSize = 8;
	state.Parity = NOPARITY;
	state.StopBits = ONESTOPBIT;
	SetCommState(hSerial, &state);

	return hSerial;
}

int main() {
	initSinLut();

	HANDLE fpgaSerial = connectSerial("\\\\.\\COM4");  // For interfacing with LEDs
	HANDLE arduinoSerial = connectSerial("\\\\.\\COM5");  // For interfacing with Arduino beat tracking

	struct timeb start, end;
	ftime(&start);
	long millis = 0;

	unsigned long frameTime = 0;

	uint8_t keyWasPressed[NUM_KEYS] = { 0 };
	uint8_t isAcceptingInput = 1;

	enum ColorMode colorMode = RED;
	enum AnimationMode animationMode = ANIMATION_OFF;
	long animationStartTime = 0;
	struct RGBColor solidColor = { 0, 0, 0 };
	double audioLevel = MIN_AUDIO_LEVEL;
	uint8_t hasChangedRainbow = 0;
	double brightness = 1.0;

	double waveBrightnesses[WAVE_SIZE] = { 0 };
	double fadeBrightness = 0;  // [0, 1] multiplier
	long rainbowPeriodStartTime = 0;
	uint8_t rainbowSegment = 0;
	struct WaveData waveData[MAX_NUM_WAVES];
	uint8_t nextWaveIndex = 0;

	// Pong
	struct Paddle paddle1;
	struct Paddle paddle2;
	struct Ball ball;
	resetPong(&paddle1, &paddle2, &ball, 1);
	paddle1.color = white;
	paddle2.color = white;
	ball.color = white;

	const double RAINBOW_OMEGA = 2 * PI / (2 * RAINBOW_PERIOD_MS / 3.0);

	// Initialize wave brightness levels
	for (uint8_t i = 0; i < WAVE_SIZE; ++i) {
		waveBrightnesses[i] = getSinLut(i / (double) WAVE_SIZE * PI);
	}

	// Initialize wave data
	for (uint8_t i = 0; i < MAX_NUM_WAVES; ++i) {
		waveData[i].animationStartingTime = 0;
		waveData[i].direction = 0;
		waveData[i].focus = 0;
		waveData[i].animationIsFinished = 1;
	}

	while (1) {
		ftime(&end);
		millis = (long) (1000.0 * (end.time - start.time) + (end.millitm - start.millitm));

		// Get audio level via Arduino serial
		uint8_t arduinoSerialByte;
		DWORD arduinoBytesRead = 0;
		if (ReadFile(arduinoSerial, &arduinoSerialByte, 1, &arduinoBytesRead, NULL) && arduinoBytesRead) {
			if (GetKeyState('P') & 0x8000) {
				printf("Arduino serial byte: %d\n", arduinoSerialByte);
			}
			
			double newAudioLevel = arduinoSerialByte / 255.0;  // [0, 1]
			newAudioLevel *= 1 - MIN_AUDIO_LEVEL;  // [0, 1 - MIN_AUDIO_LEVEL]
			newAudioLevel += MIN_AUDIO_LEVEL;  // [MIN_AUDIO_LEVEL, 1]

			audioLevel = 0.45 * audioLevel + 0.55 * newAudioLevel;

			if (audioLevel <= COLOR_CHANGE_THRESHOLD) {
				hasChangedRainbow = 0;
			}
		}

		// Loop through ASCII characters
		for (uint8_t i = 1; i < NUM_KEYS; ++i) {
			if ((GetKeyState(i) & 0x8000) && !keyWasPressed[i]) {
				// Pressed

				if (i == 17) {
					// Ctrl key - toggle if keyboard input is enabled
					isAcceptingInput = !isAcceptingInput;
					keyWasPressed[i] = 1;
				}

				if (isAcceptingInput) {
					keyWasPressed[i] = 1;
					printf("Pressed: %u\n", i);

					if (i == 'G') {
						// Toggle pong
						if (animationMode == ANIMATION_PONG) {
							animationMode = ANIMATION_OFF;
						}
						else {
							animationMode = ANIMATION_PONG;
							resetPongAndScore(&paddle1, &paddle2, &ball);
							setPongScore(fpgaSerial, paddle1.score, paddle2.score);
						}
					}

					if (animationMode != ANIMATION_PONG) {
						if (i >= '0' && i <= '9') {
							colorMode = i - '0';
							if (colorMode == RAINBOW || colorMode == RED_BLUE || colorMode == GREEN_BLUE) {
								rainbowPeriodStartTime = millis;
								rainbowSegment = 0;
							}
						}
						else if (i == 'L') {
							// Reconnect serial
							CloseHandle(fpgaSerial);
							CloseHandle(arduinoSerial);
							fpgaSerial = connectSerial("\\\\.\\COM4");
							arduinoSerial = connectSerial("\\\\.\\COM5");
						}
						else if (i == 37) {
							brightness -= 0.05;
							if (brightness < 0) {
								brightness = 0;
							}
						}
						else if (i == 39) {
							brightness += 0.05;
							if (brightness > 1.5) {
								brightness = 1.5;
							}
						}
						else if (i == 'Z') {
							// Toggle solid
							if (animationMode == ANIMATION_SOLID) {
								animationMode = ANIMATION_OFF;
							}
							else {
								animationMode = ANIMATION_SOLID;
							}
						}
						else if (i == 'X') {
							// Toggle rainbow
							if (animationMode == ANIMATION_RAINBOW) {
								animationMode = ANIMATION_OFF;
							}
							else {
								animationMode = ANIMATION_RAINBOW;
								animationStartTime = millis;
							}
						}
						else if (i == 'C') {
							if (animationMode == ANIMATION_ALTERNATING) {
								animationMode = ANIMATION_OFF;
							}
							else {
								animationMode = ANIMATION_ALTERNATING;
							}
						}
						else if (i == 38 || i == 40) {
							// Wave

							animationMode = ANIMATION_WAVE;

							struct WaveData newWaveData;
							newWaveData.animationStartingTime = millis;
							if (i == 38) {
								newWaveData.direction = WAVE_DIR_UP;
								newWaveData.focus = LED_ROWS - 1;
							}
							else {
								newWaveData.direction = WAVE_DIR_DOWN;
								newWaveData.focus = 0;
							}
							newWaveData.animationIsFinished = 0;
							waveData[nextWaveIndex] = newWaveData;

							++nextWaveIndex;
							if (nextWaveIndex >= MAX_NUM_WAVES) {
								nextWaveIndex = 0;
							}
						}
					}
				}
			}
			else if (!(GetKeyState(i) & 0x8000) && keyWasPressed[i]) {
				// Released
				keyWasPressed[i] = 0;
				printf("Released: %u\n", i);
			}
		}

		switch (colorMode) {
		case RED:
			solidColor = red;
			break;
		case ORANGE:
			solidColor = orange;
			break;
		case YELLOW:
			solidColor = yellow;
			break;
		case GREEN:
			solidColor = green;
			break;
		case BLUE:
			solidColor = blue;
			break;
		case PURPLE:
			solidColor = purple;
			break;
		case WHITE:
			solidColor = white;
			break;
		case RAINBOW:
			// Divide the rainbow period into three sinusoidal segments
			// One period of the sinusoid is 2/3 the period of the rainbow animation
			if (animationMode == ANIMATION_SOLID) {
				switch (rainbowSegment) {
				case 0:
					solidColor = red;
					break;
				case 1:
					solidColor = orange;
					break;
				case 2:
					solidColor = yellow;
					break;
				case 3:
					solidColor = green;
					break;
				case 4:
					solidColor = blue;
					break;
				case 5:
					solidColor = purple;
					break;
				}
				if (audioLevel > COLOR_CHANGE_THRESHOLD && !hasChangedRainbow) {
					++rainbowSegment;
					if (rainbowSegment > 5) {
						rainbowSegment = 0;
					}
					hasChangedRainbow = 1;
				}
			}
			else {
				if (millis - rainbowPeriodStartTime < RAINBOW_PERIOD_MS / 3) {
					double cosine = getCosLut(RAINBOW_OMEGA * (millis - rainbowPeriodStartTime));
					if (rainbowSegment == 0) {
						solidColor.r = (uint8_t)(20 * cosine + 20);
						solidColor.g = (uint8_t)(20 * -cosine + 20);
						solidColor.b = 0;
					}
					else if (rainbowSegment == 1) {
						solidColor.r = 0;
						solidColor.g = (uint8_t)(20 * cosine + 20);
						solidColor.b = (uint8_t)(20 * -cosine + 20);
					}
					else if (rainbowSegment == 2) {
						solidColor.r = (uint8_t)(20 * -cosine + 20);
						solidColor.g = 0;
						solidColor.b = (uint8_t)(20 * cosine + 20);
					}
				}
				else {
					rainbowPeriodStartTime = millis;
					++rainbowSegment;
					if (rainbowSegment > 2) {
						rainbowSegment = 0;
					}
				}
			}
			break;
		case RED_BLUE:
			if (animationMode == ANIMATION_SOLID) {
				switch (rainbowSegment) {
				case 0:
					solidColor = red;
					break;
				case 1:
					solidColor = blue;
					break;
				}
				if (audioLevel > COLOR_CHANGE_THRESHOLD && !hasChangedRainbow) {
					++rainbowSegment;
					if (rainbowSegment > 1) {
						rainbowSegment = 0;
					}
					hasChangedRainbow = 1;
				}
			}
			else {
				if (millis - rainbowPeriodStartTime < RAINBOW_PERIOD_MS / 3) {
					solidColor.r = (uint8_t)(
						20 * getCosLut(2 * PI / RAINBOW_PERIOD_MS * (millis - rainbowPeriodStartTime) + ((rainbowSegment == 0) ? 0 : PI)) + 20
					);
					solidColor.g = 0;
					solidColor.b = (uint8_t)(
						20 * getCosLut(2 * PI / RAINBOW_PERIOD_MS * (millis - rainbowPeriodStartTime) + ((rainbowSegment == 0) ? PI : 0)) + 20
					);
				}
				else {
					rainbowPeriodStartTime = millis;
					++rainbowSegment;
					if (rainbowSegment > 1) {
						rainbowSegment = 0;
					}
				}
			}
			break;
		case GREEN_BLUE:
			if (animationMode == ANIMATION_SOLID) {
				switch (rainbowSegment) {
				case 0:
					solidColor = green;
					break;
				case 1:
					solidColor = blue;
					break;
				}
				if (audioLevel > COLOR_CHANGE_THRESHOLD && !hasChangedRainbow) {
					++rainbowSegment;
					if (rainbowSegment > 1) {
						rainbowSegment = 0;
					}
					hasChangedRainbow = 1;
				}
			}
			else {
				if (millis - rainbowPeriodStartTime < RAINBOW_PERIOD_MS / 3) {
					solidColor.r = 0;
					solidColor.g = (uint8_t)(
						20 * getCosLut(2 * PI / RAINBOW_PERIOD_MS * (millis - rainbowPeriodStartTime) + ((rainbowSegment == 0) ? 0 : PI)) + 20
					);
					solidColor.b = (uint8_t)(
						20 * getCosLut(2 * PI / RAINBOW_PERIOD_MS * (millis - rainbowPeriodStartTime) + ((rainbowSegment == 0) ? PI : 0)) + 20
						);
				}
				else {
					rainbowPeriodStartTime = millis;
					++rainbowSegment;
					if (rainbowSegment > 1) {
						rainbowSegment = 0;
					}
				}
			}
			break;
		}

		solidColor.r = (uint8_t)(solidColor.r * brightness);
		solidColor.g = (uint8_t)(solidColor.g * brightness);
		solidColor.b = (uint8_t)(solidColor.b * brightness);

		// Adjust brightness based on audio level
		if (animationMode != ANIMATION_WAVE) {
			solidColor.r = (uint8_t)(solidColor.r * audioLevel);
			solidColor.g = (uint8_t)(solidColor.g * audioLevel);
			solidColor.b = (uint8_t)(solidColor.b * audioLevel);
		}

		uint8_t sine1 = 0;
		uint8_t sine2 = 0;
		switch (animationMode) {
		case ANIMATION_OFF:
			setOff(fpgaSerial);
			break;
		case ANIMATION_SOLID:
			setColor(fpgaSerial, &solidColor);
			break;
		case ANIMATION_WAVE:
			// Clear rowColors
			for (uint8_t i = 0; i < LED_ROWS; ++i) {
				rowColors[i].r = 0;
				rowColors[i].g = 0;
				rowColors[i].b = 0;
			}

			for (uint8_t i = 0; i < MAX_NUM_WAVES; ++i) {
				if (waveData[i].animationIsFinished) {
					continue;
				}

				if (waveData[i].direction == WAVE_DIR_DOWN) {
					waveData[i].focus = (uint8_t)((millis - waveData[i].animationStartingTime) * WAVE_SPEED);
					if (waveData[i].focus >= LED_ROWS + WAVE_SIZE - 1) {
						waveData[i].animationIsFinished = 1;
						continue;
					}
					for (int8_t j = 0; j < LED_ROWS; ++j) {
						if (j <= waveData[i].focus && j > waveData[i].focus - WAVE_SIZE) {
							double rowBrightness = waveBrightnesses[waveData[i].focus - j];
							rowColors[j].r += (uint8_t)(solidColor.r * rowBrightness);
							rowColors[j].g += (uint8_t)(solidColor.g * rowBrightness);
							rowColors[j].b += (uint8_t)(solidColor.b * rowBrightness);
						}
					}
				}
				else {
					waveData[i].focus = LED_ROWS - 1 - (uint8_t)((millis - waveData[i].animationStartingTime) * WAVE_SPEED);
					if (waveData[i].focus <= -WAVE_SIZE) {
						waveData[i].animationIsFinished = 1;
						continue;
					}
					for (int8_t j = 0; j < LED_ROWS; ++j) {
						if (j >= waveData[i].focus && j < waveData[i].focus + WAVE_SIZE) {
							double rowBrightness = waveBrightnesses[j - waveData[i].focus];
							rowColors[j].r += (uint8_t)(solidColor.r * rowBrightness);
							rowColors[j].g += (uint8_t)(solidColor.g * rowBrightness);
							rowColors[j].b += (uint8_t)(solidColor.b * rowBrightness);
						}
					}
				}
			}
			setRowColors(fpgaSerial);
			break;
		case ANIMATION_RAINBOW:
			for (uint8_t i = 0; i < LED_ROWS; ++i) {
				uint8_t adjustedI = i;//(uint8_t)(i + (millis - animationStartTime) / 12.0);
				while (adjustedI >= LED_ROWS) {
					adjustedI -= LED_ROWS;
				}
				if (adjustedI<= LED_ROWS / 3) {
					double cosine = getCosLut((double)adjustedI / LED_ROWS * 2 * PI);
					rowColors[i].r = (uint8_t)((20 * cosine + 20) * brightness );
					rowColors[i].g = (uint8_t)((20 * -cosine + 20) * brightness);
					rowColors[i].b = 0;
				}
				else if (adjustedI <= 2 * LED_ROWS / 3) {
					double cosine = getCosLut((double)adjustedI / LED_ROWS * 2 * PI - 2 * PI / 3);
					rowColors[i].r = 0;
					rowColors[i].g = (uint8_t)((20 * cosine + 20) * brightness);
					rowColors[i].b = (uint8_t)((20 * -cosine + 20) * brightness);
				}
				else {
					double cosine = getCosLut((double)adjustedI / LED_ROWS * 2 * PI - 4 * PI / 3);
					rowColors[i].r = (uint8_t)((20 * -cosine + 20) * brightness);
					rowColors[i].g = 0;
					rowColors[i].b = (uint8_t)((20 * cosine + 20) * brightness);
				}
			}
			setRowColors(fpgaSerial);
			break;
		case ANIMATION_ALTERNATING:
			sine1 = (uint8_t)(20 * getSinLut(2 * PI / 600 * millis) + 20);
			sine2 = (uint8_t)(20 * getSinLut(2 * PI / 600 * millis + PI / 2) + 20);
			for (uint8_t i = 0; i < LED_ROWS; ++i) {
				if (i % 2 == 0) {
					rowColors[i].r = sine1;
					rowColors[i].g = 0;
					rowColors[i].b = sine2;
				}
				else {
					rowColors[i].r = sine2;
					rowColors[i].g = 0;
					rowColors[i].b = sine1;
				}
			}
			setRowColors(fpgaSerial);
			break;
		case ANIMATION_PONG:
			pongEnd = getMicros();
			frameTime = (unsigned long) (pongEnd - pongStart);
			pongStart = pongEnd;

			// Control paddles
			if (keyWasPressed['Q']) {
				paddle1.y -= PADDLE_SPEED * frameTime;
			}
			else if (keyWasPressed['A']) {
				paddle1.y += PADDLE_SPEED * frameTime;
			}
			if (keyWasPressed['O']) {
				paddle2.y -= PADDLE_SPEED * frameTime;
			}
			else if (keyWasPressed['L']) {
				paddle2.y += PADDLE_SPEED * frameTime;
			}

			// Paddle bounds
			if (paddle1.y < 0) {
				paddle1.y = 0;
			}
			else if (paddle1.y > FULL_LED_ROWS - PADDLE_HEIGHT) {
				paddle1.y = FULL_LED_ROWS - PADDLE_HEIGHT;
			}
			if (paddle2.y < 0) {
				paddle2.y = 0;
			}
			else if (paddle2.y > FULL_LED_ROWS - PADDLE_HEIGHT) {
				paddle2.y = FULL_LED_ROWS - PADDLE_HEIGHT;
			}

			// Ball + wall collisions
			if (ball.y < 0) {
				ball.y = 0;
				ball.vy *= -1;
			}
			else if (ball.y > FULL_LED_ROWS - BALL_HEIGHT) {
				ball.y = FULL_LED_ROWS - BALL_HEIGHT;
				ball.vy *= -1;
			}

			// Ball + left paddle collisions
			if (ball.x < PADDLE_WIDTH && ball.y > paddle1.y - BALL_HEIGHT && ball.y < paddle1.y + PADDLE_HEIGHT) {
				double theta = ((paddle1.y + PADDLE_HEIGHT / 2.0) - (ball.y + BALL_HEIGHT / 2.0)) / (PADDLE_HEIGHT / 2.0) * MAX_BALL_ANGLE;
				ball.vx = BALL_SPEED * cos(theta);
				ball.vy = BALL_SPEED * -sin(theta);
			}

			// Ball + right paddle collisions
			else if (ball.x > LED_COLS - PADDLE_WIDTH - BALL_WIDTH && ball.y > paddle2.y - BALL_HEIGHT && ball.y < paddle2.y + PADDLE_HEIGHT) {
				double theta = ((paddle2.y + PADDLE_HEIGHT / 2.0) - (ball.y + BALL_HEIGHT / 2.0)) / (PADDLE_HEIGHT / 2.0) * MAX_BALL_ANGLE;
				ball.vx = -BALL_SPEED * cos(theta);
				ball.vy = BALL_SPEED * -sin(theta);
			}

			uint8_t scoreWasUpdated = 0;

			// Ball past left paddle
			if (ball.x < 0) {
				paddle2.score += 4;
				resetPong(&paddle1, &paddle2, &ball, 0);
				scoreWasUpdated = 1;
			}

			// Ball past right paddle
			else if (ball.x > LED_COLS - BALL_WIDTH) {
				paddle1.score += 4;
				resetPong(&paddle1, &paddle2, &ball, 1);
				scoreWasUpdated = 1;
			}

			if (scoreWasUpdated) {
				if (paddle1.score > 36 || paddle2.score > 36) {
					resetPongAndScore(&paddle1, &paddle2, &ball);
				}
				setPongScore(fpgaSerial, paddle1.score, paddle2.score);
			}

			// Move ball
			ball.x += ball.vx * frameTime;
			ball.y += ball.vy * frameTime;

			setPongData(fpgaSerial, &paddle1, &paddle2, &ball);

			Sleep(1);

			break;
		}
	}

	CloseHandle(fpgaSerial);
	CloseHandle(arduinoSerial);

	return 0;
}
