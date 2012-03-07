//
// This file contains the hardware/os dependent interface for controlling
// the programming lines.
//

#ifndef __IO_H
#define __IO_H

#define HIGH 1
#define LOW 0

int InitIo(int debug);
void SetMclr(int level);
void SetVdd(int level);
void SetClock(int level);
void SetData(int level);
void SetLvp(int level);

// Note: you must set data HIGH before reading data
int ReadData(void);
void Delay(int microseconds);

#endif

