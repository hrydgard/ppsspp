#pragma once

#include <vector>

// Just an abstract thing to get debug information.
class GPUMemoryManager {
public:
	virtual ~GPUMemoryManager() {}

	virtual void GetDebugString(char *buffer, size_t bufSize) const = 0;
	virtual const char *Name() const = 0;  // for sorting
};

std::vector<GPUMemoryManager *> GetActiveGPUMemoryManagers();

void RegisterGPUMemoryManager(GPUMemoryManager *manager);
void UnregisterGPUMemoryManager(GPUMemoryManager *manager);
