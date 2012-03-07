#include <windows.h>
#include "io.h"

#define LPT1_BASE 0x278
#define LPT_DATA LPT1_BASE
#define LPT_STATUS (LPT1_BASE + 1)
#define LPT_CONTROL (LPT1_BASE + 2)

#define BIT_PGD  1
#define BIT_PGC 2
#define BIT_VDD 4
#define BIT_VPP 8
#define BIT_LVP 16

static unsigned char _stdcall (*DlPortReadPortUchar)(unsigned long Port);
static void _stdcall (*DlPortWritePortUchar)(unsigned long Port, unsigned char Value);

static HANDLE hDlPortIoLibrary;
static unsigned char set_bits;
static int debug = 0;

// Load the DlPortIO library and find the addresses to functions to read
// and write low level IO ports
int InitIo(int debug_output)
{
	hDlPortIoLibrary = LoadLibrary("c:\\windows\\system32\\DlPortIo.dll");
	if (hDlPortIoLibrary == NULL) {
		printf("error opening library\n");
		return -1;
	}

	DlPortReadPortUchar = GetProcAddress(hDlPortIoLibrary, "DlPortReadPortUchar");
	if (DlPortReadPortUchar == NULL) {
		printf("error finding callback\n");
		return -1;
	}

	DlPortWritePortUchar = GetProcAddress(hDlPortIoLibrary, "DlPortWritePortUchar");
	if (DlPortWritePortUchar == NULL) {
		printf("error finding callback\n");
		return -1;
	}

	// Enable LPT port
	DlPortWritePortUchar(LPT_CONTROL, 1);

	debug = debug_output;

	return 0;
}

// The granularity of the delays here is pretty conservative.  A delay loop
// could potentially speed this up.
void Delay(int microseconds)
{
	int i;

	if (debug)
		printf("Delay %d\n", microseconds);

	if (microseconds < 1000) {
//		Sleep(1);

		for (i = 0; i < 200000 * microseconds; i++) {
			__asm("nop");
		}

	} else
		Sleep(microseconds / 1000);
}

// VPP (_MCLR)  control is attached to D3.  It is an non-inverting input
// VPP is high when D3 is low
void SetMclr(int level)
{
	if (debug)
		printf("VPP %s\n", level ? "HIGH" : "LOW");

	if (level == HIGH)
		set_bits |= BIT_VPP;
	else
		set_bits &= ~BIT_VPP;

	DlPortWritePortUchar(LPT_DATA, set_bits);
}

// VDD is attached to D2.  it is an inverting input
void SetVdd(int level)
{
	if (debug)
		printf("VDD %s\n", level ? "HIGH" : "LOW");

	if (level == LOW)
		set_bits |= BIT_VDD;
	else
		set_bits &= ~BIT_VDD;

	DlPortWritePortUchar(LPT_DATA, set_bits);
}

// Clock is attached to D1.  It is non-inverting.
void SetClock(int level)
{
	if (debug)
		printf("Clock %s\n", level == HIGH ? "HIGH" : "LOW");

	if (level == HIGH)
		set_bits |= BIT_PGC;
	else
		set_bits &= ~BIT_PGC;

	DlPortWritePortUchar(LPT_DATA, set_bits);
}

// Data output is attached to D0.  It is inverting.
void SetData(int level)
{
	if (debug)
		printf("Data %s\n", level == HIGH ? "HIGH" : "LOW");

	if (level == LOW)
		set_bits |= BIT_PGD;
	else
		set_bits &= ~BIT_PGD;

	DlPortWritePortUchar(LPT_DATA, set_bits);
}

// Data input is attached to ACK.  It is non-inverting.
int ReadData(void)
{
	int value;

	value = ((DlPortReadPortUchar(LPT_STATUS) & 0x40) != 0);

	if (debug)
		printf("Read %s\n", value == HIGH ? "HIGH" : "LOW");

	return value;
}

// Low voltage programming is attached to D4.  It is non-inverting.
void SetLvp(int level)
{
	if (debug)
		printf("LVP %s\n", level == HIGH ? "HIGH" : "LOW");

	if (level == HIGH)
		set_bits |= BIT_LVP;
	else
		set_bits &= ~BIT_LVP;

	DlPortWritePortUchar(LPT_DATA, set_bits);
}

