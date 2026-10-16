#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"

struct DepthScreenVertex {
	int x;
	int y;
	int z;
};

// X11.h, sigh.
#ifdef Always
#undef Always
#endif

// We only need to support these three modes.
enum class ZCompareMode : u8 {
	Greater,  // Most common
	Less,  // Less common
	Always,  // Mostly used for clears
};

struct DepthScissor {
	u16 x1;
	u16 y1;
	u16 x2;
	u16 y2;

	DepthScissor Tile(int tile, int numTiles) const;
};

struct DepthDraw {
	u32 depthAddr;
	u16 depthStride;
	u8 cullMode;
	GEPrimitiveType prim;
	ZCompareMode compareMode;
	bool cullEnabled;
	DepthScissor scissor;
	int vertexOffset;
	int indexOffset;
	int vertexCount;
};

// Specialized, very limited depth-only rasterizer.
// Meant to run in parallel with hardware rendering, in games that read back the depth buffer
// for effects like lens flare.
// So, we can be quite inaccurate without any issues, and skip a lot of functionality.

class VertexDecoder;
struct TransformedVertex;

// maxOutCount is the capacity of each of tx/ty/tz, in elements.
int DepthRasterClipIndexedTriangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw, const DepthScissor scissor, int maxOutCount);
int DepthRasterClipIndexedRectangles(int *tx, int *ty, float *tz, const float *transformed, const uint16_t *indexBuffer, const DepthDraw &draw, const DepthScissor scissor);
void DecodeAndTransformForDepthRaster(float *dest, const float *worldviewproj, const void *vertexData, int indexLowerBound, int indexUpperBound, const VertexDecoder *dec, u32 vertTypeID);
void TransformPredecodedForDepthRaster(float *dest, const float *worldviewproj, const void *decodedVertexData, const VertexDecoder *dec, int count);
void ConvertPredecodedThroughForDepthRaster(float *dest, const void *decodedVertexData, const VertexDecoder *dec, int count);
void DepthRasterScreenVerts(uint16_t *depth, int depthStride, const int *tx, const int *ty, const float *tz, int count, const DepthDraw &draw, const DepthScissor scissor, bool lowQ);

// A batch of queued depth draws, with their transformed vertices and indices.
struct DepthBatch {
	float *transformed = nullptr;
	uint16_t *indices = nullptr;
	std::vector<DepthDraw> draws;
	int vertexCount = 0;
	int indexCount = 0;
	int vertexCapacity = 0;
	int indexCapacity = 0;
	bool lowQ = false;
};

// Rasterizes batches of depth draws on a thread of its own, in the order they're submitted, so
// that it overlaps with emulating the CPU and the rest of the GE. The PSP memory it writes is only
// the depth buffers of the draws, so anything reading those has to Wait() first (or WaitIfWriting).
class DepthRasterWorker {
public:
	DepthRasterWorker(int maxVerts, int maxIndices);
	~DepthRasterWorker();

	// The batch to add draws to.
	DepthBatch &Current() { return *current_; }
	// Makes room in the current batch for this many more vertices and indices.
	void Reserve(int vertices, int indices);
	// Starts rasterizing the current batch (if it has draws) and makes an empty one current.
	void Submit();
	// Submits the current batch and waits until all are rasterized.
	void Wait();
	// Waits until the batches already submitted are rasterized.
	void WaitForSubmitted();
	// Waits only if a batch being rasterized may write to [addr, addr + size).
	void WaitIfWriting(u32 addr, u32 size);

	// Time spent culling and rasterizing since the last call, for the stats.
	void TakeTimes(double *cull, double *raster);

private:
	DepthBatch *NewBatch();
	static void FreeBatch(DepthBatch *batch);
	void Run();
	void Rasterize(const DepthBatch &batch, double *cull, double *raster);

	const int maxVerts_;
	const int maxIndices_;
	int *screenVerts_ = nullptr;

	std::vector<DepthBatch *> all_;
	std::vector<DepthBatch *> free_;
	std::vector<DepthBatch *> queue_;  // Oldest first.
	DepthBatch *current_ = nullptr;
	int running_ = 0;  // Batches taken off the queue and not yet done.

	// The depth buffer bytes the submitted, unfinished batches can write.
	// The depth buffers being written (VRAM offsets, end exclusive), and their span, which only grows
	// while busy_, so it can be checked without the lock.
	std::vector<std::pair<u32, u32>> busyRanges_;
	std::atomic<u32> busyLow_{};
	std::atomic<u32> busyHigh_{};
	std::atomic<bool> busy_{};

	double cullTime_ = 0.0;
	double rasterTime_ = 0.0;

	bool quit_ = false;
	std::mutex mutex_;
	std::condition_variable work_;
	std::condition_variable done_;
	std::thread thread_;
};
