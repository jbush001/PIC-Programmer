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

#include "serial.h"
#include <stdio.h>

#define PROGRESS_BAR_WIDTH 60
#define MAX_PROGRAM_SIZE 0x2000
#define EXPECTED_PROTOCOL_VERSION 1

/// Write an 8 bit value to the port
/// @returns
///   - 1 if the octet was written successfully
///   - 0 if the octet was not written successfully
///
/// This will print an error message if an error occurs
int write_octet(int value)
{
	int result = write_serial(value & 0xff);
	if (result == -1)
	{
		printf("\nCan't write to serial device (OS returned error)\n");
		return 0;
	}
	else if (result == -2)
	{
		printf("\nA timeout occured trying to write the the programmer\n");
		return 0;
	}
	else if (result != 0)
	{
		printf("\nreceived unexpected result writing to programmer (bug in serial shim code)\n");
		return 0;
	}

	return 1;

}

/// Write a 16 bit value to the port, in bigendian format
/// @returns
///   - 1 if the short was written successfully
///   - 0 if the short was not written successfully
///
/// This will print an error message if an error occurs
int write_short(int value)
{
	if (!write_octet(value >> 8))
		return 0;

	return write_octet(value);
}

/// @returns
///   - 1 if the ack was retruned successfully
///   - 0 if an error occured
///
///  This function will print an error message if an error occurs
int wait_for_ack()
{
	int c = read_serial();

	if (c == '+')
		return 1;	// Success
	else if (c == -1)
	{
		printf("\nThe serial device is not communicating\n");
		return 0;
	}
	else if (c == -2)
	{
		printf("\nTimeout waiting for programmer response\n");
		return 0;
	}
	else if (c == 'E')
	{
		int error = read_serial();
		if (error == -1)
		{
			printf("\nThe serial device is not communicating\n");
			return 0;
		}
		else if (error == -2)
		{
			printf("\nTimeout waiting for programmer response\n");
			return 0;
		}

		switch (error)
		{
			case '1':
				printf("\nProgrammer has reported an overflow error\n");
				break;

			case '2':
				printf("\nProgrammer has reported a framing error\n");
				break;

			case '3':
				printf("\nProgrammer has reported that verification has failed\n");

				// Get word that was read back
				int msb = read_serial();
				int lsb = read_serial();

				printf("got 0x%02x%02x\n", msb, lsb);

				break;

			case '4':
				printf("\nProgrammer has reported that a command is not understood\n");
				break;
		}

		return 0;
	}
	else
	{
		printf("\nReceived unrecognized error from programmer: %c\n", c);
		return 0;
	}
}

///
/// Draw an ascii progress bar.
///
static void draw_progress_bar(int current, int max, const char *prefix)
{
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

///
/// Read Intel HEX format file
///
/// each line is:
/// :aabbbbccdddddddd...dddee
/// where a is the length of the line (hex)
/// b is the address
/// c is the record type
///   00 data
///   01 end of file
/// d is the data for the line
/// e is the checksum, the 2's complement sum of of the other bytes in the line
static int read_hex_file(const char *filename, char *array, int *outMaxAddress)
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

done:
	fclose(f);

	return result;
}

int main(int argc, const char *argv[])
{
	int i;
	int got_checksum;
	int computed_checksum_hi;
	int computed_checksum_lo;
	int version;
	unsigned char program_data[MAX_PROGRAM_SIZE * 2 + 16];
	int instruction_count;
	int config_word;

	if (argc != 2) {
		printf("enter a filename\n");
		return 1;
	}

	if (!init_serial())
		return 1;

	if (!write_octet('V'))
		return 1;

	version = read_serial();
	if (version == -1)
	{
		printf("Cannot communicate with serial device driver\n");
		return 1;
	}
	else if (version == -2)
	{
		printf("Programmer is not responding\n");
		return 1;
	}
	else if (version < 0)
	{
		printf("received unexpected result writing to programmer (bug in serial shim code)\n");
		return 1;
	}

	if (version != EXPECTED_PROTOCOL_VERSION)
	{
		printf("Programmer uses a different protocol version.  Cannot communicate\n");
		return 1;
	}

	memset(program_data, 0xff, sizeof(program_data));
	if (read_hex_file(argv[1], program_data, &instruction_count) < 0)
		return 1;

	instruction_count /= 2;	// Returned value was max address.  Divide by two to get count.
	printf("%d instructions\n", instruction_count);

	// Enter programming mode
	if (!write_octet('P'))
		return 1;

	if (!wait_for_ack())
		return 1;

	// Erase flash
	if (!write_octet('E'))
		return 1;

	if (!wait_for_ack())
		return 1;

	computed_checksum_hi = 0;
	computed_checksum_lo = 0;
	if (!write_octet('W'))
		return 1;

	if (!write_short(instruction_count))	// Number of program words to write
		return 1;

	if (!wait_for_ack())
		return 1;

	for (i = 0; i < instruction_count; i++)
	{
		draw_progress_bar(i + 1, instruction_count, "Programming");

		// The program words here are 14 bits LSB justified, but must be written
		// to the device with a zero bit as padding on each end.  Also, the bytes in the file
		// are little endian, so they need to be swapped before writing out to the
		// file
		int instruction = ((program_data[i * 2 + 1] << 8) | program_data[i * 2]) << 1;
		if (!write_short(instruction))
			return 1;

		if (!wait_for_ack())
		{
			printf("writing instruction @ %d (%04x)\n", i, instruction);
			return 1;
		}

		computed_checksum_lo = (computed_checksum_lo + ((instruction >> 8) & 0xff)) & 0xff;
		computed_checksum_hi = (computed_checksum_hi + computed_checksum_lo) & 0xff;
		computed_checksum_lo = (computed_checksum_lo + (instruction & 0xff)) & 0xff;
		computed_checksum_hi = (computed_checksum_hi + computed_checksum_lo) & 0xff;
	}

	if (read_serial() != 'D')
	{
		printf("Unexpected response waiting for checksum\n");
		return 1;
	}

	got_checksum = (read_serial() << 8) & 0xff00;
	got_checksum |= read_serial() & 0xff;

	if (((computed_checksum_hi << 8) | computed_checksum_lo) != got_checksum)
	{
		printf("Checksum mismatch.  Data was corrupted while being transferred\n");
		return 1;
	}

	if (!write_octet('C'))
		return 1;

	config_word = (program_data[0x400f] << 8) | program_data[0x400e];

	// Write configuration word
	if (!write_short(config_word << 1))
		return 1;

	if (!wait_for_ack())
	{
		printf("Writing configuration word\n");
		return 1;
	}

	// Exit programming mode
	if (!write_octet('X'))
		return 1;

	if (!wait_for_ack())
		return 1;

	printf("\nFlash programmed.\n");

	// Turn on chip
	write_octet('I');
	write_octet('2');

	if (!wait_for_ack())
		return 1;

	return 0;
}
