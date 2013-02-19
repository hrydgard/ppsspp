#include "../../MemMap.h"
#include "../MIPSAnalyst.h"

#include "ArmJit.h"
#include "ArmRegCache.h"


#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

// #define CONDITIONAL_DISABLE Comp_Generic(op); return;
#define CONDITIONAL_DISABLE ;
#define DISABLE Comp_Generic(op); return;

namespace MIPSComp
{
	void Jit::Comp_VPFX(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_SVQ(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_VVectorInit(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_VDot(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_VecDo3(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_VV2Op(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_Mftv(u32 op)
	{
		DISABLE;
	}

	void Jit::Comp_SV(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vmtvc(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vmmov(u32 op) {
		DISABLE;
	}

}
