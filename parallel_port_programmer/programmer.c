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

// A utility for programming PIC microcontrollers over a parallel port.
// Based on DS41196E "PIC16F627A/628A/648A EEPROM Memory Programming Specification" 

#include <stdio.h>
#include "io.h"

#define DEVICE_TYPE_F84A 0

#define MAX_PROGRAM_SIZE 0x2000
#define PROGRESS_BAR_WIDTH 50

// Commands
#define CMD_LOAD_DATA_PROGRAM 0x02
#define CMD_INCREMENT_ADDR 0x06
#if DEVICE_TYPE_F84A
	#define CMD_BEGIN_PROGRAM_ONLY_CYCLE 0x18
#else
	#define CMD_BEGIN_PROGRAM_ONLY_CYCLE 0x08
#endif
#define CMD_BULK_ERASE_PROGRAM 0x09
#define CMD_LOAD_DATA_CONFIG  0x00
#define CMD_READ_PROGRAM_MEMORY 0x04

// Timings (in microseconds)
#define TPPDP 10 		// 5 us minimum
#define TSET0 1 		// 100 ns minimum
#define THLD0 10		// 5 us minimum
#define TSET1 1 		// 100 ns minimum
#define THLD1 1 		// 100 ns minimum
#define TDLY1 2 		// 1 us minimum
#define TDLY2 2 		// 1 us minimum
#define TDLY3 1 		// 80 ns minimum
#define TPROG 3000 		// 2.5 ms minimum
#define TDPROG 10000	// 6 ms minimum
#define TERA 10000 		// 6 ms minimum 

// For debugging
static int pc = 0;
static int program_word =  0;

static int config_word = 0;

// debug levels
// 0 - no debug output
// 1 - display commands and data
// 2 - display logic level changes and delays
static int debug_level = 0;

static void Initiate84HighVoltageProgrammingMode(void);
static void Initiate628HighVoltageProgrammingMode(void);
static void InitiateLowVoltageProgrammingMode(void);
static void WriteBits(int c, int count);
static int ReadBits(int count);
static void LoadDataForProgramMemory(int instruction);
static void IncrementAddress(void);
static void BeginProgramOnlyCycle(void);
static void BulkEraseProgramMemory(void);
static void LoadDataForConfigurationMemory(int value);
static int LoadDataFromProgramMemory(void);
static void DrawProgressBar(int current, int max, const char *prefix);
static int WriteProgram(const unsigned short *codes, int count, int erase, int verify);
static void DetermineDeviceType(void);
static int TestProgrammerCircuit(void);
static int ReadHexFile(const char *filename, char *array, int *outMaxAddress);
static void DebugReadBits(void);

int main(int argc, const char *argv[])
{
	unsigned char data[MAX_PROGRAM_SIZE * 2 + 16];
	int maxAddress;
	unsigned short instructions[MAX_PROGRAM_SIZE];
	int i;

	if (argc != 2) {
		printf("enter a filename\n");
		return 1;
	}

	if (InitIo(debug_level == 2) < 0) {
		printf("error opening parallel port\n");
		return 1;
	}

	if (debug_level > 0)
		printf("Loading %s\n", argv[1]);

	memset(data, 0xff, sizeof(data));
	if (ReadHexFile(argv[1], data, &maxAddress) < 0)
		return 1;

	if (debug_level > 0)
		printf("%d instructions\n", maxAddress / 2);

	InitiateLowVoltageProgrammingMode();

	// The address corresponds to the byte offset in the file, not the actual
	// program address (because the PC address 14 bit data words).  Divide
	// the address encoded in this file by two to get the actual PC.
	//
	// The program words here are 14 bits LSB justified, but must be written
	// to the device with a zero bit as padding on each end.  Also, the bytes in the file
	// are little endian, so they need to be swapped before writing out to the
	// file
	// 
	// Padding get performed by LoadDataForProgramMemory
	for (i = 0; i < maxAddress / 2; i++)
		instructions[i] = (data[i * 2 + 1] << 8) | data[i * 2];

	config_word = (data[0x400f] << 8) | data[0x400e];
	
	if (debug_level > 0)
		printf("config_word = %04x\n", config_word);

	printf("program is %d instructions\n", maxAddress / 2);	
	if (WriteProgram(instructions, maxAddress / 2, 1, 1) < 0)
		return 1;

	/* Turn the chip off */
	SetLvp(LOW);
	SetClock(LOW);
	SetData(LOW);
	SetMclr(LOW);
	SetVdd(LOW);

	printf("\n\nChip Successfully Programmed\n");

	return 0;
}

static void Initiate84HighVoltageProgrammingMode(void)
{
	SetClock(LOW);
	SetData(LOW);
	SetMclr(LOW);
	SetVdd(LOW);
	Delay(1000000);
	SetVdd(HIGH);
	Delay(TPPDP);
	SetMclr(HIGH);
	Delay(THLD0);
}

static void Initiate628HighVoltageProgrammingMode(void)
{
	SetClock(LOW);
	SetData(LOW);
	SetMclr(LOW);
	SetVdd(LOW);
	Delay(1000000);
	SetMclr(HIGH);
	Delay(TPPDP);
	SetVdd(HIGH);
	Delay(THLD0);
}

static void InitiateLowVoltageProgrammingMode(void)
{
	SetClock(LOW);
	SetData(LOW);
	SetMclr(LOW);
	SetVdd(LOW);
	Delay(100000);
	SetVdd(HIGH);
	Delay(THLD0);
	SetLvp(HIGH);
	Delay(1);
	SetMclr(HIGH);	// MCLR
	Delay(TPPDP);
}

// Bit bang data to the microcontroller
// "The programming module operates on simple command sequences entered in serial fashion with the
// data being latched on the falling edge of the clock pulse. The sequences are entered serially, via the clock
// and data lines, which are Schmitt Trigger inputs in this mode. The general form for all command sequences
// consists of a 6-bit command and conditionally a 16-bit data word. Both command and data word are clocked
// LSb first."
static void WriteBits(int c, int count)
{
	int bit;

	if (debug_level > 1) {
		printf("WriteBits(%d) ", count);

		for (bit = 0; bit < count; bit++) {
			printf("%c", (c & (1 << bit)) != 0 ? '1' : '0');
			if ((bit & 7) == 7)
				printf(" ");
		}

		printf("\n");	
	}

	for (bit = 0; bit < count; bit++) {
		SetClock(HIGH);
		SetData((c & (1 << bit)) != 0 ? HIGH : LOW);
		Delay(TSET1);
		SetClock(LOW);
		Delay(THLD1);
	}
}

// Read some number of bits serially from program data (RB7)
// return 1 if the line is high (+5v)
// return 0 if the line is low (0v)
static int ReadBits(int count)
{
	int bit;
	int word = 0;

	SetData(HIGH);
	for (bit = 0; bit < count; bit++) {
		SetClock(HIGH);
		Delay(TDLY3);
		word = word | (ReadData() << bit);
		SetClock(LOW);
		Delay(THLD1);
	}

	return word;
}

static void DebugReadBits(void)
{
	int bit;
	int current_state = -1;
	int next_state = -1;
	int spin;

	WriteBits(CMD_READ_PROGRAM_MEMORY, 6);
	SetData(HIGH);

	for (bit = 0; bit < 16; bit++) {
		SetClock(HIGH);
		
		for (spin = 0; spin < 10000; spin++) {
			next_state = ReadData();
			if (next_state != current_state) {
				printf("%d ", next_state);
				current_state = next_state;
			}
		}

		SetClock(LOW);
		for (spin = 0; spin < 10000; spin++) {
			next_state = ReadData();
			if (next_state != current_state) {
				printf("%d ", next_state);
				current_state = next_state;
			}
		}
	}
}

// Load data for program memory 
// Receives a 14 bit word and readies it to be programmed at the PC location.
// 0, data(14), 0
static void LoadDataForProgramMemory(int instruction)
{
	if (debug_level > 0) {
		printf("LoadDataForProgramMemory(%04x)\n", instruction);
		program_word = instruction;
	}

	WriteBits(CMD_LOAD_DATA_PROGRAM, 6);
	Delay(TDLY2);
	WriteBits((instruction & 0x3fff) << 1, 16);
}

// Increment Address
// The PC is incremented when this command is received
static void IncrementAddress(void)
{
	if (debug_level > 0) {
		printf("IncrementAddress\n");
		pc++;
	}

	WriteBits(CMD_INCREMENT_ADDR, 6);
	Delay(TDLY2);
}

// Begin programming only cycle, program memory
// Programs the previously loaded word into the appropriate memory
// (User program, Data, or Configuration Memory).  A load command
// must be given before every program command.
static void BeginProgramOnlyCycle(void)
{
	if (debug_level > 0) {
		printf("BeginProgramOnlyCycle\n");
		printf("%04x <= %04x\n", pc, program_word);
	}
	
	WriteBits(CMD_BEGIN_PROGRAM_ONLY_CYCLE, 6);
	Delay(TPROG);
}

// Bulk erase program memory
static void BulkEraseProgramMemory(void)
{
	if (debug_level > 0) 
		printf("BulkEraseProgramMemory\n");

	WriteBits(CMD_BULK_ERASE_PROGRAM, 6);
	Delay(TERA);
}

// Load data for configuration memory
// Advances the PC to the start of configuration memory (0x2000-0x200F)
// and loads the data for the first ID location.  Once it is set to the configuration
// region, only exiting and re-entering Program/Verify mode will reset PC 
// to the user memory space.
static void LoadDataForConfigurationMemory(int value)
{
	if (debug_level > 0) {
		printf("LoadDataForConfigurationMemory(%04x)\n", value);
		program_word =  value;
		pc  = 0x2000;
	}
	
	WriteBits(CMD_LOAD_DATA_CONFIG, 6);
	Delay(TDLY2);
	WriteBits((value & 0x3fff) << 1, 16);
}

static int LoadDataFromProgramMemory(void)
{
	int word;

	if (debug_level > 0)
		printf("LoadDataFromProgramMemory()\n");

	WriteBits(CMD_READ_PROGRAM_MEMORY, 6);
	Delay(TDLY2);
	word = ReadBits(16);

	return (word >> 1) & 0x3fff;
}

static void DrawProgressBar(int current, int max, const char *prefix)
{
	if (debug_level == 0) {
		int i;
		int dotCount;

		printf("\r%s [", prefix);

		dotCount = PROGRESS_BAR_WIDTH * current / max;
		
		for (i = 0; i < dotCount; i++)
			printf(".");

		for (; i < PROGRESS_BAR_WIDTH; i++)
			printf(" ");

		printf("]");
		fflush(stdout);
	}
}

// Write out the entire program section
// Each program word is stored in the hex file as 2 bytes, LSB justified
// little endian.
// Returns -1 if there is an error, 0 otherwise
static int WriteProgram(const unsigned short *codes, int count, int erase, int verify)
{
	int i;
	int readback;

	if (erase) {
		LoadDataForProgramMemory(0x3fff);	/* data to store in memory locations */
		BulkEraseProgramMemory();
	}

	for (i = 0; i < count; i++) {
		if (codes[i] != 0xffff) {
			LoadDataForProgramMemory(codes[i]);
			BeginProgramOnlyCycle();
			if (verify) {
				readback = LoadDataFromProgramMemory();
				if (readback < 0)
					return -1;	/* an error occured during readback */
				
				if (readback != codes[i]) {
					fprintf(stderr, "\n\nVerify failed PC %04x wrote %04x read %04x\n",
						i, codes[i], readback);
					return -1;
				}
			}
		}
		
		IncrementAddress();
		DrawProgressBar(i, count - 1, "Programming");
	}

	if (erase) {
		/* Rewrite the configuration word */
	 	LoadDataForConfigurationMemory(0);	/* now we're at 0x2000 */

		/* Skip ahead to 2007h */
		for (i = 0; i < 7; i++)
			IncrementAddress();

		// Note: it seems like this should be LoadDataForConfigurationMemory,
		// However, that does not work.  The datasheet is a little vague about
		// this.
		LoadDataForProgramMemory(config_word);
		BeginProgramOnlyCycle();

		readback = LoadDataFromProgramMemory();
		if (readback != config_word)
			printf("failed to write config word %04x != %04x\n", config_word, readback);
	}

	return 0;
 }

static void DetermineDeviceType(void)
{
	int address;
	int device_id;

	LoadDataForConfigurationMemory(0x7fff);

	/* Increment to 2006h */
	for (address = 0; address < 7; address++) {
		printf("%04x => %04x\n", address + 0x2000,
			LoadDataFromProgramMemory());
		IncrementAddress();
	}

	device_id = LoadDataFromProgramMemory();
	if (device_id < 0) {
		printf("Cannot read device ID word\n");
		return;
	}

	switch (device_id >> 5) {
		case 0x2b:
			printf("PIC16F84A\n");
			break;
			
		case 0x82:
			printf("PIC16F627A\n");
			break;

		case 0x83:
			printf("PIC16F628A\n");
			break;

		case 0x88:
			printf("PIC16F648A\n");
			break;

		default:
			printf("Unknown device type\n");
	}
}

static int TestProgrammerCircuit(void)
{
	SetData(LOW);
	if (ReadData() != LOW)
		return 0;

	SetData(HIGH);
	if (ReadData() != HIGH)
		return 0;

	return 1;
}

//
// each line is:
// :aabbbbccdddddddd...dddee 
// where a is the length of the line (hex)
// b is the address
// c is the record type
//   00 data
//   01 end of file
// d is the data for the line
// e is the checksum, the 2's complement sum of of the other bytes in the line 
//
static int ReadHexFile(const char *filename, char *array, int *outMaxAddress)
{
	FILE *f;
	int dataLength;
	int address;
	int recordType;
	int checksum;
	int computedChecksum;
	int datum;
	int i;
	char data[128];
	int line;
	int result = 0;
	
	f = fopen(filename, "r");
	if (f == NULL) {
		perror("error opening file");
		return;
	}

	*outMaxAddress = 0;
	
	for (line = 1; ; line++) {
		if (fscanf(f, ":%02x%04x%02x", &dataLength, &address, &recordType) < 0) {
			fprintf(stderr, "premature end of file\n");
			result = -1;
			break;
		}

		computedChecksum = dataLength + (address >> 8) + (address & 0xff) + recordType;

		if (address + dataLength > *outMaxAddress && address < 0x4000)
			*outMaxAddress = address + dataLength;

		for (i = 0; i < dataLength; i++) {
			if (fscanf(f, "%02x", &datum) < 0) {
				fprintf(stderr, "premature end of file\n");
				result = -1;
				goto done;
			}

			array[address + i] = (char) datum;
			computedChecksum += datum;
		}
			
		if (fscanf(f, "%02x\n", &checksum) < 0) {
			fprintf(stderr, "premature end of file\n");
			result = -1;
			break;
		}

		computedChecksum = (1 + ~(computedChecksum & 0xff)) & 0xff;

		if (checksum != computedChecksum) {
			fprintf(stderr, "checksum mismatch on line %d (file %02x computed %02x)\n", line, checksum, computedChecksum);
			result = -1;
			break;
		}
		
		if (recordType == 1)
			break;
	}

	if (debug_level > 0)
		printf("read %d lines from %s\n", line, filename);

done:
	fclose(f);

	return result;
}

