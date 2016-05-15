#include "Core/MIPS/IR/IRInst.h"
#include "Common/x64Emitter.h"

namespace MIPSComp {

class IRToNativeInterface {
public:
	virtual ~IRToNativeInterface() {}

	virtual void ConvertIRToNative(const IRInst *instructions, int count, const u32 *constants) = 0;
};


class IRToX86 : public IRToNativeInterface {
public:
	void SetCodeBlock(Gen::XCodeBlock *code) { code_ = code; }
	virtual void ConvertIRToNative(const IRInst *instructions, int count, const u32 *constants) override;

private:
	Gen::XCodeBlock *code_;
};

}  // namespace
