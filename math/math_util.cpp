#include "math/math_util.h"
#include <stdlib.h>

#if defined(__ARM_ARCH_7A__) && !defined(BLACKBERRY)

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
#else
void EnableFZ()
{
	// TODO
}
#endif
