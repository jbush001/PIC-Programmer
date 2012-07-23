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

