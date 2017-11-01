#pragma once

#include <cstring>
#include <vector>

#include "ext/xxhash.h"
#include "Common/CommonFuncs.h"

// Whatever random value.
const uint32_t hashmapSeed = 0x23B58532;

// TODO: Try hardware CRC. Unfortunately not available on older Intels or ARM32.
// Seems to be ubiquitous on ARM64 though.
template<class K>
inline uint32_t HashKey(const K &k) {
	return XXH32(&k, sizeof(k), hashmapSeed);
}
template<class K>
inline bool KeyEquals(const K &a, const K &b) {
	return !memcmp(&a, &b, sizeof(K));
}

enum class BucketState : uint8_t {
	FREE,
	TAKEN,
	REMOVED,  // for linear probing to work (and removal during deletion) we need tombstones
};

// Uses linear probing for cache-friendliness. Not segregating values from keys because
// we always use very small values, so it's probably better to have them in the same
// cache-line as the corresponding key.
// Enforces that value are pointers to make sure that combined storage makes sense.
template <class Key, class Value, Value NullValue>
class DenseHashMap {
public:
	DenseHashMap(int initialCapacity) : capacity_(initialCapacity) {
		map.resize(initialCapacity);
		state.resize(initialCapacity);
	}

	// Returns nullptr if no entry was found.
	Value Get(const Key &key) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = HashKey(key) & mask;
		// No? Let's go into search mode. Linear probing.
		uint32_t p = pos;
		while (true) {
			if (state[p] == BucketState::TAKEN && KeyEquals(key, map[p].key))
				return map[p].value;
			else if (state[p] == BucketState::FREE)
				return NullValue;
			p = (p + 1) & mask;  // If the state is REMOVED, we just keep on walking. 
			if (p == pos)
				Crash();
		}
		return NullValue;
	}

	// Returns false if we already had the key! Which is a bit different.
	bool Insert(const Key &key, Value value) {
		// Check load factor, resize if necessary. We never shrink.
		if (count_ > capacity_ / 2) {
			Grow(2);
		}
		uint32_t mask = capacity_ - 1;
		uint32_t pos = HashKey(key) & mask;
		uint32_t p = pos;
		while (true) {
			if (state[p] == BucketState::TAKEN) {
				if (KeyEquals(key, map[p].key)) {
					Crash();  // Bad! We already got this one. Let's avoid this case.
					return false;
				}
				// continue looking....
			} else {
				// Got a place, either removed or FREE.
				break;
			}
			p = (p + 1) & mask;
			if (p == pos) {
				// FULL! Error. Should not happen thanks to Grow().
				Crash();
			}
		}
		if (state[p] == BucketState::REMOVED) {
			removedCount_--;
		}
		state[p] = BucketState::TAKEN;
		map[p].key = key;
		map[p].value = value;
		count_++;
		return true;
	}

	bool Remove(const Key &key) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = HashKey(key) & mask;
		uint32_t p = pos;
		while (state[p] != BucketState::FREE) {
			if (state[p] == BucketState::TAKEN && KeyEquals(key, map[p].key)) {
				// Got it! Mark it as removed.
				state[p] = BucketState::REMOVED;
				removedCount_++;
				count_--;
				return true;
			}
			p = (p + 1) & mask;
			if (p == pos) {
				// FULL! Error. Should not happen.
				Crash();
			}
		}
		return false;
	}

	size_t size() const {
		return count_;
	}

	template<class T>
	inline void Iterate(T func) {
		for (size_t i = 0; i < map.size(); i++) {
			if (state[i] == BucketState::TAKEN) {
				func(map[i].key, map[i].value);
			}
		}
	}

	void Clear() {
		memset(state.data(), (int)BucketState::FREE, state.size());
	}

	void Rebuild() {
		Grow(1);
	}

	void Maintain() {
		// Heuristic
		if (removedCount_ >= capacity_ / 4) {
			Rebuild();
		}
	}

private:
	void Grow(int factor) {
		// We simply move out the existing data, then we re-insert the old.
		// This is extremely non-atomic and will need synchronization.
		std::vector<Pair> old = std::move(map);
		std::vector<BucketState> oldState = std::move(state);
		capacity_ *= factor;
		map.resize(capacity_);
		state.resize(capacity_);
		count_ = 0;  // Insert will update it.
		removedCount_ = 0;
		for (size_t i = 0; i < old.size(); i++) {
			if (oldState[i] == BucketState::TAKEN) {
				Insert(old[i].key, old[i].value);
			}
		}
	}
	struct Pair {
		Key key;
		Value value;
	};
	std::vector<Pair> map;
	std::vector<BucketState> state;
	int capacity_;
	int count_ = 0;
	int removedCount_ = 0;
};

// Like the above, uses linear probing for cache-friendliness.
// Does not perform hashing at all so expects well-distributed keys.
template <class Value, Value NullValue>
class PrehashMap {
public:
	PrehashMap(int initialCapacity) : capacity_(initialCapacity) {
		map.resize(initialCapacity);
		state.resize(initialCapacity);
	}

	// Returns nullptr if no entry was found.
	Value Get(uint32_t hash) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = hash & mask;
		// No? Let's go into search mode. Linear probing.
		uint32_t p = pos;
		while (true) {
			if (state[p] == BucketState::TAKEN && hash == map[p].hash)
				return map[p].value;
			else if (state[p] == BucketState::FREE)
				return NullValue;
			p = (p + 1) & mask;  // If the state is REMOVED, we just keep on walking. 
			if (p == pos)
				Crash();
		}
		return NullValue;
	}

	// Returns false if we already had the key! Which is a bit different.
	bool Insert(uint32_t hash, Value value) {
		// Check load factor, resize if necessary. We never shrink.
		if (count_ > capacity_ / 2) {
			Grow(2);
		}
		uint32_t mask = capacity_ - 1;
		uint32_t pos = hash & mask;
		uint32_t p = pos;
		while (state[p] != BucketState::FREE) {
			if (state[p] == BucketState::TAKEN) {
				if (hash == map[p].hash)
					return false;  // Bad!
			} else {
				// Got a place, either removed or FREE.
				break;
			}
			p = (p + 1) & mask;
			if (p == pos) {
				// FULL! Error. Should not happen thanks to Grow().
				Crash();
			}
		}
		if (state[p] == BucketState::REMOVED) {
			removedCount_--;
		}
		state[p] = BucketState::TAKEN;
		map[p].hash = hash;
		map[p].value = value;
		count_++;
		return true;
	}

	bool Remove(uint32_t hash) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = hash & mask;
		uint32_t p = pos;
		while (state[p] != BucketState::FREE) {
			if (state[p] == BucketState::TAKEN && hash == map[p].hash) {
				// Got it!
				state[p] = BucketState::REMOVED;
				removedCount_++;
				count_--;
				return true;
			}
			p = (p + 1) & mask;
			if (p == pos) {
				Crash();
			}
		}
		return false;
	}

	size_t size() {
		return count_;
	}

	template<class T>
	void Iterate(T func) {
		for (size_t i = 0; i < map.size(); i++) {
			if (state[i] == BucketState::TAKEN) {
				func(map[i].hash, map[i].value);
			}
		}
	}

	void Clear() {
		memset(state.data(), (int)BucketState::FREE, state.size());
	}

	// Gets rid of REMOVED tombstones, making lookups somewhat more efficient.
	void Rebuild() {
		Grow(1);
	}

	void Maintain() {
		// Heuristic
		if (removedCount_ >= capacity_ / 4) {
			Rebuild();
		}
	}

private:
	void Grow(int factor) {
		// We simply move out the existing data, then we re-insert the old.
		// This is extremely non-atomic and will need synchronization.
		std::vector<Pair> old = std::move(map);
		std::vector<BucketState> oldState = std::move(state);
		capacity_ *= factor;
		map.resize(capacity_);
		state.resize(capacity_);
		count_ = 0;  // Insert will update it.
		removedCount_ = 0;
		for (size_t i = 0; i < old.size(); i++) {
			if (oldState[i] == BucketState::TAKEN) {
				Insert(old[i].hash, old[i].value);
			}
		}
	}
	struct Pair {
		uint32_t hash;
		Value value;
	};
	std::vector<Pair> map;
	std::vector<BucketState> state;
	int capacity_;
	int count_ = 0;
	int removedCount_ = 0;
};
