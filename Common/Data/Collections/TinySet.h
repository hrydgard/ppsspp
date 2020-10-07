#pragma once

#include <vector>

// Insert-only small-set implementation. Performs no allocation unless MaxFastSize is exceeded.
template <class T, int MaxFastSize>
struct TinySet {
	~TinySet() { delete slowLookup_; }
	inline void insert(T t) {
		// Fast linear scan.
		for (int i = 0; i < fastCount; i++) {
			if (fastLookup_[i] == t)
				return;  // We already have it.
		}
		// Fast insertion
		if (fastCount < MaxFastSize) {
			fastLookup_[fastCount++] = t;
			return;
		}
		// Fall back to slow path.
		insertSlow(t);
	}
	bool contains(T t) const {
		for (int i = 0; i < fastCount; i++) {
			if (fastLookup_[i] == t)
				return true;
		}
		if (slowLookup_) {
			for (auto x : *slowLookup_) {
				if (x == t)
					return true;
			}
		}
		return false;
	}
	bool contains(const TinySet<T, MaxFastSize> &otherSet) {
		// Awkward, kind of ruins the fun.
		for (int i = 0; i < fastCount; i++) {
			if (otherSet.contains(fastLookup_[i]))
				return true;
		}
		if (slowLookup_) {
			for (auto x : *slowLookup_) {
				if (otherSet.contains(x))
					return true;
			}
		}
		return false;
	}
	void clear() {
		delete slowLookup_;
		slowLookup_ = nullptr;
		fastCount = 0;
	}

private:
	void insertSlow(T t) {
		if (!slowLookup_) {
			slowLookup_ = new std::vector<T>();
		} else {
			for (size_t i = 0; i < slowLookup_->size(); i++) {
				if ((*slowLookup_)[i] == t)
					return;
			}
		}
		slowLookup_->push_back(t);
	}
	T fastLookup_[MaxFastSize];
	int fastCount = 0;
	std::vector<T> *slowLookup_ = nullptr;
};
