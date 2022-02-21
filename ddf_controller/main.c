#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>

#include <windows.h>

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
		uint8_t byteToTransmit = 0;
		if (GetKeyState('1') & 0x8000) {
			// Wave mode
			byteToTransmit = 1;
		}
		else if (GetKeyState('2') & 0x8000) {
			// Strobe mode
			byteToTransmit = 2;
		}
		else if (GetKeyState('3') & 0x8000) {
			// Solid mode
			byteToTransmit = 3;
		}
		else if (GetKeyState('Q') & 0x8000) {
			byteToTransmit = 10;
		}
		else if (GetKeyState('W') & 0x8000) {
			byteToTransmit = 11;
		}
		else if (GetKeyState('W') & 0x8000) {
			byteToTransmit = 12;
		}
		else if (GetKeyState('W') & 0x8000) {
			byteToTransmit = 13;
		}
		else if (GetKeyState('W') & 0x8000) {
			byteToTransmit = 14;
		}
		else if (GetKeyState('W') & 0x8000) {
			byteToTransmit = 15;
		}
		else if (!spaceWasPressed && (GetKeyState(VK_SPACE) & 0x8000)) {
			byteToTransmit = 100;
			spaceWasPressed = 1;
			printf("a\n");
		}
		else if (spaceWasPressed && !(GetKeyState(VK_SPACE) & 0x8000)) {
			// Space released
			byteToTransmit = 101;
			spaceWasPressed = 0;
			printf("b\n");
		}

		DWORD bytesWritten;
		if (byteToTransmit) {
			WriteFile(hSerial, &byteToTransmit, 1, &bytesWritten, NULL);
		}
	}

	CloseHandle(hSerial);
	return 0;
}
