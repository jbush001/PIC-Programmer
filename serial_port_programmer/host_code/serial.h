#ifndef __SERIAL_H
#define __SERIAL_H

int init_serial();

/// @returns
///   - 0 on success
///   - -1 If there was an error communicating with the port
///   - -2 If there was a timeout reciving the character.
int write_serial(char c);

///
/// @returns
///   - 0x00 to 0xff for a valid 8 bit character read
///   - -1 If there was an error communicating with the port
///   - -2 If there was a timeout reciving the character.
int read_serial();

#endif

