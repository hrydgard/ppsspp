#pragma once

#include <vector>

// Insert-only small-set implementation. Performs no allocation unless MaxFastSize is exceeded.
// Can also be used as a small vector, then use push_back (or push_in_place) instead of insert.
// Duplicates are thus allowed if you use that, but not if you exclusively use insert.
template <class T, int MaxFastSize>
struct TinySet {
	~TinySet() { delete slowLookup_; }
	inline void insert(const T &t) {
		// Fast linear scan.
		for (int i = 0; i < fastCount_; i++) {
			if (fastLookup_[i] == t)
				return;  // We already have it.
		}
		// Fast insertion
		if (fastCount_ < MaxFastSize) {
			fastLookup_[fastCount_++] = t;
			return;
		}
		// Fall back to slow path.
		insertSlow(t);
	}
	inline void push_back(const T &t) {
		if (fastCount_ < MaxFastSize) {
			fastLookup_[fastCount_++] = t;
			return;
		}
		if (!slowLookup_) {
			slowLookup_ = new std::vector<T>();
		}
		slowLookup_->push_back(t);
	}
	inline T *add_back() {
		if (fastCount_ < MaxFastSize) {
			return &fastLookup_[fastCount_++];
		}
		if (!slowLookup_) {
			slowLookup_ = new std::vector<T>();
		}
		T t;
		slowLookup_->push_back(t);
		return slowLookup_->back();
	}
	void append(const TinySet<T, MaxFastSize> &other) {
		size_t otherSize = other.size();
		if (size() + otherSize <= MaxFastSize) {
			// Fast case
			for (size_t i = 0; i < otherSize; i++) {
				fastLookup_[fastCount_ + i] = other.fastLookup_[i];
			}
			fastCount_ += other.fastCount_;
		} else {
			for (size_t i = 0; i < otherSize; i++) {
				push_back(other[i]);
			}
		}
	}
	bool contains(T t) const {
		for (int i = 0; i < fastCount_; i++) {
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
		for (int i = 0; i < fastCount_; i++) {
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
		// TODO: Keep slowLookup_ around? That would be more similar to real vector behavior.
		delete slowLookup_;
		slowLookup_ = nullptr;
		fastCount_ = 0;
	}
	bool empty() const {
		return fastCount_ == 0;
	}
	size_t size() const {
		if (!slowLookup_) {
			return fastCount_;
		} else {
			return slowLookup_->size() + MaxFastSize;
		}
	}
	T &operator[] (size_t index) {
		if (index < MaxFastSize) {
			return fastLookup_[index];
		} else {
			return (*slowLookup_)[index - MaxFastSize];
		}
	}
	const T &operator[] (size_t index) const {
		if (index < MaxFastSize) {
			return fastLookup_[index];
		} else {
			return (*slowLookup_)[index - MaxFastSize];
		}
	}
	const T &back() const {
		return (*this)[size() - 1];
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
	int fastCount_ = 0;  // first in the struct just so it's more visible in the VS debugger.
	T fastLookup_[MaxFastSize];
	std::vector<T> *slowLookup_ = nullptr;
};
