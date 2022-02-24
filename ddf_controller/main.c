#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>

#include <windows.h>

#define NUM_KEYS 128

uint8_t keyWasPressed[NUM_KEYS] = { 0 };

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

	uint8_t spaceWasPressed = 0;

	while (1) {
		// Loop through ASCII characters
		for (uint8_t i = 1; i < NUM_KEYS; ++i) {
			if ((GetKeyState(i) & 0x8000) && !keyWasPressed[i]) {
				// Pressed
				uint8_t byteToTransmit = i;
				DWORD bytesWritten;
				WriteFile(hSerial, &byteToTransmit, 1, &bytesWritten, NULL);
				keyWasPressed[i] = 1;
				printf("Pressed: %u\n", i);
			}
			else if (!(GetKeyState(i) & 0x8000) && keyWasPressed[i]) {
				// Released
				uint8_t byteToTransmit = i + 128;
				DWORD bytesWritten;
				WriteFile(hSerial, &byteToTransmit, 1, &bytesWritten, NULL);
				keyWasPressed[i] = 0;
				printf("Released: %u\n", i);
			}
		}
	}

	CloseHandle(hSerial);
	return 0;
}
