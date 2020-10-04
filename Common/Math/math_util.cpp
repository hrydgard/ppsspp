#include "Common/Math/math_util.h"
#include <stdlib.h>

// QNX can only use RunFast mode and it is already the default.
#if defined(__ARM_ARCH_7A__)
// Enables 'RunFast' VFP mode.
void EnableFZ() {
	int x;
	asm(
		"fmrx %[result],FPSCR \r\n"
		"orr %[result],%[result],#16777216 \r\n"
		"fmxr FPSCR,%[result]"
		:[result] "=r" (x) : :
	);
	//printf("ARM FPSCR: %08x\n",x);
}

// New fastmode code from: http://pandorawiki.org/Floating_Point_Optimization
// These settings turbocharge the slow VFP unit on Cortex-A8 based chips by setting
// restrictions that permit running VFP instructions on the NEON unit.
// Denormal flush-to-zero, for example.
void FPU_SetFastMode() {
	static const unsigned int x = 0x04086060;
	static const unsigned int y = 0x03000000;
	int r;
	asm volatile (
		"fmrx	%0, fpscr			\n\t"	//r0 = FPSCR
		"and	%0, %0, %1			\n\t"	//r0 = r0 & 0x04086060
		"orr	%0, %0, %2			\n\t"	//r0 = r0 | 0x03000000
		"fmxr	fpscr, %0			\n\t"	//FPSCR = r0
		: "=r"(r)
		: "r"(x), "r"(y)
		);
}

#else

void EnableFZ() {
	// TODO
}

void FPU_SetFastMode() {}

#endif
