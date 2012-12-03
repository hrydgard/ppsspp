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

#if defined(ARM)

void EnableFZ()
{
	int x;
	asm(
	 "orr %[result],%[result],#16777216 \r\n"
	 :[result] "=r" (x) : :
	);
	
}

void DisableFZ( )
{
	__asm__ volatile(
		"bic r0, $(1 << 24)\n");
}
#else

void EnableFZ()
{


}
void DisableFZ()
{

}

#endif
