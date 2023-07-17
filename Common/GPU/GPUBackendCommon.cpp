#include <mutex>
#include <set>

#include "Common/GPU/GPUBackendCommon.h"

// Global push buffer tracker for GPU memory profiling.
// Don't want to manually dig up all the active push buffers.
static std::mutex g_pushBufferListMutex;
static std::set<GPUMemoryManager *> g_pushBuffers;

std::vector<GPUMemoryManager *> GetActiveGPUMemoryManagers() {
	std::vector<GPUMemoryManager *> buffers;
	std::lock_guard<std::mutex> guard(g_pushBufferListMutex);
	for (auto iter : g_pushBuffers) {
		buffers.push_back(iter);
	}
	return buffers;
}

void RegisterGPUMemoryManager(GPUMemoryManager *manager) {
	std::lock_guard<std::mutex> guard(g_pushBufferListMutex);
	g_pushBuffers.insert(manager);
}

void UnregisterGPUMemoryManager(GPUMemoryManager *manager) {
	std::lock_guard<std::mutex> guard(g_pushBufferListMutex);
	g_pushBuffers.erase(manager);
}
