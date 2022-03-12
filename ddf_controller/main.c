#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>
#include <sys/timeb.h>

#include <windows.h>

#define LED_ROWS 72
#define NUM_KEYS 128
#define CMD_BYTE 255

// Animation codes
#define SOLID_CODE 0
#define ROW_CODE 1
#define SET_R1_CODE 22
#define SET_G1_CODE 23
#define SET_B1_CODE 24
#define SET_ROW_R1_CODE 25
#define SET_ROW_G1_CODE 26
#define SET_ROW_B1_CODE 27
#define SET_FLOOR_IS_ON_CODE 30
#define SET_CONTROL_ROW_CODE 31

#define PI 3.14159265

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

uint8_t keyWasPressed[NUM_KEYS] = { 0 };
long millis = 0;

struct RGBColor hsvToRgb(struct HSVColor* color) {
	double c = color->v * color->s;
	double hPrime = color->h / 60.0f;
	double x = c * (1 - fabs(fmod(	hPrime, 2.0) - 1));
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
	printf("%d  %d  %d\n", result.r, result.g, result.b);
	return result;
}

void setAnimationMode(HANDLE hSerial, uint8_t code) {
	uint8_t packet[2];
	
	packet[0] = CMD_BYTE;
	packet[1] = code;

	DWORD bytesWritten;
	WriteFile(hSerial, packet, 2, &bytesWritten, NULL);
}

void setColor(HANDLE hSerial, struct RGBColor* color) {
	uint8_t packet[9];

	packet[0] = CMD_BYTE;
	packet[1] = SET_R1_CODE;
	packet[2] = color->r;
	packet[3] = CMD_BYTE;
	packet[4] = SET_G1_CODE;
	packet[5] = color->g;
	packet[6] = CMD_BYTE;
	packet[7] = SET_B1_CODE;
	packet[8] = color->b;

	DWORD bytesWritten;
	WriteFile(hSerial, packet, 9, &bytesWritten, NULL);
}

void setRowColor(HANDLE hSerial, uint8_t row, struct RGBColor *color) {
	uint8_t packet[12];

	packet[0] = CMD_BYTE;
	packet[1] = SET_CONTROL_ROW_CODE;
	packet[2] = row;
	packet[3] = CMD_BYTE;
	packet[4] = SET_ROW_R1_CODE;
	packet[5] = color->r;
	packet[6] = CMD_BYTE;
	packet[7] = SET_ROW_G1_CODE;
	packet[8] = color->g;
	packet[9] = CMD_BYTE;
	packet[10] = SET_ROW_B1_CODE;
	packet[11] = color->b;

	DWORD bytesWritten;
	WriteFile(hSerial, packet, 12, &bytesWritten, NULL);
}

int main() {
	HANDLE hSerial = CreateFileA("\\\\.\\COM4", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hSerial == INVALID_HANDLE_VALUE) {
		printf("ERROR: Failed to open serial port\n");
	}
	else {
		printf("Successfully opened serial port\n");
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

	struct timeb start, end;
	ftime(&start);

	uint8_t animationMode = SOLID_CODE;
	uint8_t spaceWasPressed = 0;
	uint8_t packetLen = 0;
	uint8_t focusRow = 0;

	while (1) {
		ftime(&end);
		millis = (long) (1000.0 * (end.time - start.time) + (end.millitm - start.millitm));

		// Loop through ASCII characters
		for (uint8_t i = 1; i < NUM_KEYS; ++i) {
			if ((GetKeyState(i) & 0x8000) && !keyWasPressed[i]) {
				// Pressed

				// Animation codes
				if (i >= '1' && i <= '9') {
					animationMode = i - '1';
					setAnimationMode(hSerial, animationMode);
				}

				keyWasPressed[i] = 1;
				printf("Pressed: %u\n", i);
			}
			else if (!(GetKeyState(i) & 0x8000) && keyWasPressed[i]) {
				// Released
				keyWasPressed[i] = 0;
				printf("Released: %u\n", i);
			}
		}

		if (animationMode == SOLID_CODE) {
			struct RGBColor color;
			color.r = (uint8_t) (20 * sin(2 * PI / 2000.0 * millis) + 20);
			color.g = 0;
			color.b = (uint8_t) (20 * sin(2 * PI / 2000.0 * millis + PI) + 20);
			setColor(hSerial, &color);
		}
		else if (animationMode == ROW_CODE) {
			focusRow = LED_ROWS / 2 * sin(2 * PI / 2000.0 * millis) + LED_ROWS / 2;
			for (uint8_t i = 0; i < LED_ROWS; ++i) {
				//struct HSVColor hsvColor;
				//hsvColor.h = 180 * sin(2 * PI / 5000.0 * millis + (double) i / LED_ROWS * 2 * PI) + 180;
				//hsvColor.s = 0.7;
				//hsvColor.v = 0.02;
				//struct RGBColor rgbColor = hsvToRgb(&hsvColor);
				
				struct RGBColor rgbColor;
				if (i == focusRow) {
					rgbColor.r = 32;
					rgbColor.g = 0;
					rgbColor.b = 0;
				}
				else {
					rgbColor.r = 0;
					rgbColor.g = 0;
					rgbColor.b = 0;
				}
				setRowColor(hSerial, i, &rgbColor);
			}
		}
	}

	Sleep(1);

	CloseHandle(hSerial);
	return 0;
}
