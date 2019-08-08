#pragma once

int sceKernelCreateHeap(int partitionId, int size, int flags, const char *Name);
int sceKernelAllocHeapMemory(int heapId, int size);
int sceKernelDeleteHeap(int heapId);
