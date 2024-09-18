#pragma once

#include <vector>

#include "Common/Log.h"

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
	inline T &push_uninitialized() {
		if (fastCount_ < MaxFastSize) {
			return fastLookup_[fastCount_++];
		}
		if (!slowLookup_) {
			slowLookup_ = new std::vector<T>();
		}

		// The slow lookup is also slow at adding.
		T t;
		slowLookup_->push_back(t);
		return *slowLookup_->back();
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

template <class T, int MaxSize>
struct FixedVec {
	~FixedVec() {}
	// WARNING: Can fail if you exceed MaxSize!
	inline bool push_back(const T &t) {
		if (count_ < MaxSize) {
			data_[count_++] = t;
			return true;
		} else {
			return false;
		}
	}

	// WARNING: Can fail if you exceed MaxSize!
	inline T &push_uninitialized() {
		if (count_ < MaxSize) {
			return &data_[count_++];
		}
		_dbg_assert_(false);
		return *data_[MaxSize - 1];  // BAD
	}

	// Invalid if empty().
	void pop_back() { count_--;	}

	// Unlike TinySet, we can trivially support begin/end as pointers.
	T *begin() { return data_; }
	T *end() { return data_ + count_; }
	const T *begin() const { return data_; }
	const T *end() const { return data_ + count_; }

	size_t capacity() const { return MaxSize; }
	void clear() { count_ = 0; }
	bool empty() const { return count_ == 0; }
	size_t size() const { return count_; }

	bool contains(T t) const {
		for (int i = 0; i < count_; i++) {
			if (data_[i] == t)
				return true;
		}
		return false;
	}

	// Out of bounds (past size() - 1) is undefined behavior.
	T &operator[] (const size_t index) { return data_[index]; }
	const T &operator[] (const size_t index) const { return data_[index]; }

	// These two are invalid if empty().
	const T &back() const { return (*this)[size() - 1]; }
	const T &front() const { return (*this)[0]; }

	bool operator == (const FixedVec<T, MaxSize> &other) const {
		if (count_ != other.count_)
			return false;
		for (int i = 0; i < count_; i++) {
			if (!(data_[i] == other.data_[i])) {
				return false;
			}
		}
		return true;
	}

private:
	int count_ = 0;  // first in the struct just so it's more visible in the VS debugger.
	T data_[MaxSize];
};
