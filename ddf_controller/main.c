#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>
#include <sys/timeb.h>

#include <windows.h>

#define PI 3.14159265

// LED_ROWS is actually half the real number of rows for performance purposes
#define LED_ROWS 36
#define NUM_KEYS 128
#define CMD_BYTE 255

// Serial command codes
#define SET_ROWS_COLOR_CODE 22

#define RAINBOW_PERIOD_MS 800

#define COLOR_CHANGE_THRESHOLD 0.1
#define MIN_AUDIO_LEVEL 0

// focus = WAVE_SPEED * t
#define WAVE_SPEED 0.1

#define WAVE_SIZE 16
#define MAX_NUM_WAVES 4

#define SIN_LUT_SAMPLES 4096

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
	ANIMATION_RAINBOW
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

struct RGBColor rowColors[LED_ROWS];
double sinLut[SIN_LUT_SAMPLES];

const struct RGBColor red = { 50, 0, 0 };
const struct RGBColor orange = { 49, 5, 0 };
const struct RGBColor yellow = { 30, 20, 0 };
const struct RGBColor green = { 0, 50, 0 };
const struct RGBColor blue = { 0, 0, 50 };
const struct RGBColor purple = { 25, 0, 25 };
const struct RGBColor white = { 16, 16, 16 };

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

void initSinLut() {
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

// Fill global rowColors array with zeros and write to FPGA
void setOff(HANDLE hSerial) {
	struct RGBColor color = { 0, 0, 0 };
	setColor(hSerial, &color);
}

HANDLE connectSerial(LPCSTR port) {
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

	HANDLE fpgaSerial = connectSerial("\\\\.\\COM4");
	HANDLE arduinoSerial = connectSerial("\\\\.\\COM5");

	struct timeb start, end;
	ftime(&start);
	long millis = 0;

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

					if (i >= '0' && i <= '9') {
						colorMode = i - '0';
						if (colorMode == RAINBOW || colorMode == RED_BLUE || colorMode == GREEN_BLUE) {
							rainbowPeriodStartTime = millis;
							rainbowSegment = 0;
						}
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
				uint8_t adjustedI = (uint8_t)(i + (millis - animationStartTime) / 12.0);
				while (adjustedI >= LED_ROWS) {
					adjustedI -= LED_ROWS;
				}
				if (adjustedI<= LED_ROWS / 3) {
					double cosine = getCosLut((double)adjustedI / LED_ROWS * 2 * PI);
					rowColors[i].r = (uint8_t)((20 * cosine + 20) * brightness);
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
		}
	}

	CloseHandle(fpgaSerial);
	CloseHandle(arduinoSerial);

	return 0;
}
