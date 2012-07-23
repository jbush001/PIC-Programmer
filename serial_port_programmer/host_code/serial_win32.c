// 
// Copyright 2005-2012 Jeff Bush
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// 

#include <windows.h>
#include "serial.h"

#define SERIAL_TIMEOUT 1500

static HANDLE serialPort = 0;
static HANDLE readEvent = 0;
static HANDLE writeEvent = 0;

static void print_error()
{
	char messageBuffer[256];

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, GetLastError(), 0, messageBuffer, sizeof(messageBuffer), NULL);

	printf("%s\n", messageBuffer);
}

int init_serial()
{
	DCB portState;

	serialPort = CreateFile("COM4", GENERIC_READ | GENERIC_WRITE,
		0, 0, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
	if (serialPort == INVALID_HANDLE_VALUE)
	{
		printf("Error opening serial port\n");
		print_error();
		return 0;
	}

	readEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!readEvent)
	{
		printf("Error creating read event\n");
		print_error();
		return 0;
	}

	writeEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!writeEvent)
	{
		printf("Error creating write event\n");
		print_error();
		return 0;
	}

	if (!GetCommState(serialPort, &portState))
	{
		printf("GetCommState failed\n");
		print_error();
		return 0;
	}

	portState.BaudRate = CBR_9600;
	portState.StopBits = ONESTOPBIT;
	portState.ByteSize = 8;
	portState.fParity = FALSE;
	portState.fOutxCtsFlow = FALSE;
	portState.fOutxDsrFlow = FALSE;
	portState.fDtrControl = DTR_CONTROL_DISABLE;
	portState.fDsrSensitivity = FALSE;
	portState.fOutX = FALSE;
	portState.fInX = FALSE;
	portState.fRtsControl = RTS_CONTROL_DISABLE;
	portState.Parity = NOPARITY;


	if (!SetCommState(serialPort, &portState))
	{
		printf("SetCommState failed\n");
		print_error();
		return 0;
	}

	return 1;
}

int write_serial(char c)
{
	OVERLAPPED overlap;
	DWORD written;

	overlap.hEvent = writeEvent;
	overlap.Offset = 0;
	overlap.OffsetHigh = 0;

	if (!WriteFile(serialPort, &c, 1, &written, &overlap) && GetLastError() != ERROR_IO_PENDING)
	{
		printf("WriteFile\n");
		print_error();
		return -1;
	}

	if (WaitForSingleObject(writeEvent, SERIAL_TIMEOUT) != WAIT_OBJECT_0)
	{
		printf("Write timeout\n");
		return -2;
	}

	return 0;
}

int read_serial()
{
	OVERLAPPED overlap;
	unsigned char c = 0x55;
	DWORD bytesRead;

	overlap.hEvent = readEvent;
	overlap.Offset = 0;
	overlap.OffsetHigh = 0;

	if (!ReadFile(serialPort, &c, 1, &bytesRead, &overlap) && GetLastError() != ERROR_IO_PENDING)
	{
		print_error();
		return -1;
	}

	if (WaitForSingleObject(readEvent, SERIAL_TIMEOUT) != WAIT_OBJECT_0)
	{
		printf("Read timeout\n");
		return -2;
	}

	return c;
}


