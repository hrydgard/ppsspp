#include "math/math_util.h"
#include <math.h>
#include <stdlib.h>

/*
static unsigned int randSeed = 22222;	// Change this for different random sequences. 

void SetSeed(unsigned int seed) {
	randSeed = seed * 382792592;
}

unsigned int GenerateRandomNumber() {
	randSeed = (randSeed * 196314165) + 907633515;
	randSeed ^= _rotl(randSeed, 13);
	return randSeed;
}*/

#include <math.h>

#if defined(ARMV7)

void EnableFZ()
{
	int x;
	asm(
		"fmrx %[result],FPSCR \r\n"
		"orr %[result],%[result],#16777216 \r\n"
		"fmxr FPSCR,%[result]"
		:[result] "=r" (x) : :
	);
	//printf("ARM FPSCR: %08x\n",x);
}

void DisableFZ( )
{
	__asm__ volatile(
		"fmrx r0, fpscr\n"
		"bic r0, $(1 << 24)\n"
		"fmxr fpscr, r0" : : : "r0");
}
#else

void EnableFZ()
{
	// TODO
}

void DisableFZ()
{
	// TODO
}

#endif
