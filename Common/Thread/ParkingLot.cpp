#include "Common/Thread/ParkingLot.h"
#include <cstdlib>
#include <queues/mpsc_stack.h>
#include <rthreads/rthreads.h>

static constexpr int PARKING_SHARDS = 64;

namespace {

struct ParkingLotShards {
	retro_eventcount_t ec[PARKING_SHARDS];
	ParkingLotShards() {
		for (int i = 0; i < PARKING_SHARDS; i++)
			retro_eventcount_init(&ec[i]);
	}
	~ParkingLotShards() {
		for (int i = 0; i < PARKING_SHARDS; i++)
			retro_eventcount_free(&ec[i]);
	}
};

// Function-local so it exists before any static constructor that might wait.
ParkingLotShards &Shards() {
	static ParkingLotShards shards;
	return shards;
}

}  // namespace

retro_eventcount_t *ParkingLotFor(const void *addr) {
	uintptr_t h = (uintptr_t)addr;
	h ^= h >> 17;
	h *= (uintptr_t)0x9E3779B97F4A7C15ULL;
	h ^= h >> 29;
	return &Shards().ec[h & (PARKING_SHARDS - 1)];
}

// The bundled C decoder workers use the same process-lifetime parking shards.
extern "C" retro_eventcount_t *ff_retro_parking_for(const void *addr) {
	return ParkingLotFor(addr);
}

namespace {
struct CodecInitRequest {
	mpsc_stack_node_t node;
	int (*call)(void *);
	void *arg;
	int result;
	retro_atomic_int_t complete;
};
// Legacy codec initializers share tables. One owner executes those callbacks;
// decoder workers exchange requests without a lock, including recursive opens.
retro_atomic_int_t codecInitState{0};
retro_atomic_int_t codecInitStop{0};
retro_atomic_int_t codecInitExitRegistered{0};
mpsc_stack_t codecInitRequests;
sthread_t *codecInitThread = nullptr;

void CodecInitWorker(void *) {
	for (;;) {
		auto *node = mpsc_stack_reverse(mpsc_stack_drain(&codecInitRequests));
		while (node) {
			auto *request = reinterpret_cast<CodecInitRequest *>(node);
			node = node->next;
			const void *completion = &request->complete;
			request->result = request->call(request->arg);
			retro_atomic_store_release_int(&request->complete, 1);
			ParkingLotNotify(completion);
		}
		if (retro_atomic_load_acquire_int(&codecInitStop))
			return;
		ParkingLotWait(&codecInitRequests, [] {
			return !mpsc_stack_empty(&codecInitRequests) || retro_atomic_load_acquire_int(&codecInitStop);
		});
	}
}
}

extern "C" void ff_retro_serial_shutdown() {
	if (retro_atomic_load_acquire_int(&codecInitState) == 2) {
		// Hosts call this only after their codecs and decoder workers have closed.
		retro_atomic_store_release_int(&codecInitStop, 1);
		ParkingLotNotify(&codecInitRequests);
		sthread_join(codecInitThread);
		codecInitThread = nullptr;
	}
	retro_atomic_store_release_int(&codecInitState, 0);
}

extern "C" int ff_retro_serial_call(int (*call)(void *), void *arg) {
	int state = retro_atomic_load_acquire_int(&codecInitState);
	if (state != 2) {
		if (retro_atomic_cas_int(&codecInitState, 0, 1)) {
			// Initialize parking before starting any worker or registering shutdown.
			ParkingLotFor(&codecInitState);
			mpsc_stack_init(&codecInitRequests);
			retro_atomic_store_relaxed_int(&codecInitStop, 0);
			codecInitThread = sthread_create(CodecInitWorker, nullptr);
			retro_atomic_store_release_int(&codecInitState, codecInitThread ? 2 : 3);
			ParkingLotNotify(&codecInitState);
			if (retro_atomic_cas_int(&codecInitExitRegistered, 0, 1))
				std::atexit(ff_retro_serial_shutdown);
		} else {
			ParkingLotWait(&codecInitState, [] { return retro_atomic_load_acquire_int(&codecInitState) != 1; });
		}
		if (retro_atomic_load_acquire_int(&codecInitState) != 2)
			return -12;  // AVERROR(ENOMEM)
	}
	if (sthread_isself(codecInitThread))
		return call(arg);
	CodecInitRequest request{};
	request.call = call;
	request.arg = arg;
	retro_atomic_int_init(&request.complete, 0);
	mpsc_stack_push(&codecInitRequests, &request.node);
	ParkingLotNotify(&codecInitRequests);
	ParkingLotWait(&request.complete, [&request] { return retro_atomic_load_acquire_int(&request.complete) != 0; });
	return request.result;
}
