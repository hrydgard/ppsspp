#include <xtl.h>

#undef min
#undef max

extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)


extern "C" void _WriteBarrier();
#pragma intrinsic(_WriteBarrier)