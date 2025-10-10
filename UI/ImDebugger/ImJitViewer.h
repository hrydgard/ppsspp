#pragma once

#include <cstdint>
#include <vector>
#include "ext/imgui/imgui.h"

#include "Core/Debugger/DebugInterface.h"
#include "Core/MIPS/JitCommon/JitBlockCache.h"
#include "Core/MIPS/IR/IRJit.h"

struct ImConfig;
struct ImControl;

class ImJitViewerWindow {
public:
	void Draw(ImConfig &cfg, ImControl &control);
	const char *Title() const {
		return "JIT Viewer";
	}

	void GoToBlockAtAddr(u32 addr);

private:
	struct CachedBlock {
		u32 addr;
		int sizeInBytes;
		int blockNum;
#ifdef IR_PROFILING
		JitBlockProfileStats profileStats;
#endif
	};
	std::vector<CachedBlock> blockList_;
	int curBlockNum_ = -1;

	int lastCpuStepCount_ = -1;
	int blockSortColumn_ = 0;

	bool refresh_ = false;
	int core_ = -1;

	ImGuiTableSortSpecs *sortSpecs_ = nullptr;
	JitBlockDebugInfo debugInfo_;
};
