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

#include <stdio.h>
#include "io.h"

int main(int argc, const char *argv[])
{
	if (InitIo(1) < 0)
		return -1;

	SetMclr(LOW);
	SetVdd(LOW);
	SetClock(LOW);
	SetData(LOW);
	SetLvp(LOW);

	printf("all lines low\n");
	getc(stdin);

	SetVdd(HIGH);
	printf("VDD high\n");
	getc(stdin);

	SetLvp(HIGH);
	printf("LVP high VDD high\n");
	getc(stdin);
	
	SetMclr(HIGH);
	printf("MCLR high\n");
	getc(stdin);

	SetClock(HIGH);
	printf("VPP high VDD high CLOCK high\n");
	getc(stdin);

	SetData(HIGH);
	printf("VPP high VDD high CLOCK high DATA high\n");
	getc(stdin);

	SetClock(LOW);
	printf("VPP high VDD high CLOCK low DATA high\n");
	getc(stdin);

	SetData(HIGH);
	printf("set data high\n");
	getc(stdin);
	printf("data = %s\n", ReadData() ? "HIGH" : "LOW");

	printf("set data low\n");	
	getc(stdin);
	printf("data = %s\n", ReadData() ? "HIGH" : "LOW");

	return 0;
}

