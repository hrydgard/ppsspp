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

enum class BucketState {
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
	}

	// Returns nullptr if no entry was found.
	Value Get(const Key &key) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = HashKey(key) & mask;
		// No? Let's go into search mode. Linear probing.
		uint32_t p = pos;
		while (true) {
			if (map[p].state == BucketState::TAKEN && KeyEquals(key, map[p].key))
				return map[p].value;
			else if (map[p].state == BucketState::FREE)
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
			if (map[p].state == BucketState::TAKEN) {
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
		if (map[p].state == BucketState::REMOVED) {
			removedCount_--;
		}
		map[p].state = BucketState::TAKEN;
		map[p].key = key;
		map[p].value = value;
		count_++;
		return true;
	}

	bool Remove(const Key &key) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = HashKey(key) & mask;
		uint32_t p = pos;
		while (map[p].state != BucketState::FREE) {
			if (map[p].state == BucketState::TAKEN && KeyEquals(key, map[p].key)) {
				// Got it! Mark it as removed.
				map[p].state = BucketState::REMOVED;
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
		for (auto &iter : map) {
			if (iter.state == BucketState::TAKEN) {
				func(iter.key, iter.value);
			}
		}
	}

	void Clear() {
		// TODO: Speedup?
		map.clear();
		map.resize(capacity_);
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
		capacity_ *= factor;
		map.clear();
		map.resize(capacity_);
		count_ = 0;  // Insert will update it.
		removedCount_ = 0;
		for (auto &iter : old) {
			if (iter.state == BucketState::TAKEN) {
				Insert(iter.key, iter.value);
			}
		}
	}
	struct Pair {
		BucketState state;
		Key key;
		Value value;
	};
	std::vector<Pair> map;
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
	}

	// Returns nullptr if no entry was found.
	Value Get(uint32_t hash) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = hash & mask;
		// No? Let's go into search mode. Linear probing.
		uint32_t p = pos;
		while (true) {
			if (map[p].state == BucketState::TAKEN && hash == map[p].hash)
				return map[p].value;
			else if (map[p].state == BucketState::FREE)
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
		while (map[p].state != BucketState::FREE) {
			if (map[p].state == BucketState::TAKEN) {
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
		if (map[p].state == BucketState::REMOVED) {
			removedCount_--;
		}
		map[p].state = BucketState::TAKEN;
		map[p].hash = hash;
		map[p].value = value;
		count_++;
		return true;
	}

	bool Remove(uint32_t hash) {
		uint32_t mask = capacity_ - 1;
		uint32_t pos = hash & mask;
		uint32_t p = pos;
		while (map[p].state != BucketState::FREE) {
			if (map[p].state == BucketState::TAKEN && hash == map[p].hash) {
				// Got it!
				map[p].state = BucketState::REMOVED;
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
		for (auto &iter : map) {
			if (iter.state == BucketState::TAKEN) {
				func(iter.hash, iter.value);
			}
		}
	}

	void Clear() {
		// TODO: Speedup?
		map.clear();
		map.resize(capacity_);
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
		capacity_ *= factor;
		map.clear();
		map.resize(capacity_);
		count_ = 0;  // Insert will update it.
		removedCount_ = 0;
		for (auto &iter : old) {
			if (iter.state == BucketState::TAKEN) {
				Insert(iter.hash, iter.value);
			}
		}
	}
	struct Pair {
		BucketState state;
		uint32_t hash;
		Value value;
	};
	std::vector<Pair> map;
	int capacity_;
	int count_ = 0;
	int removedCount_ = 0;
};
