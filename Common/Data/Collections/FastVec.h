#pragma once

// Yet another replacement for std::vector, this time for use in graphics queues.
// Its major difference is that you can append uninitialized structures and initialize them after.
// This is not allows by std::vector but is very useful for our sometimes oversized unions.
// Also, copies during resize are done by memcpy, not by any move constructor or similar.

#include <cstdlib>
#include <cstring>

#include "Common/Log.h"

template<class T>
class FastVec {
public:
	FastVec() {}
	FastVec(size_t initialCapacity) {
		capacity_ = initialCapacity;
		data_ = (T *)malloc(initialCapacity * sizeof(T));
		_assert_(data_ != nullptr);
	}
	~FastVec() { if (data_) free(data_); }

	T &push_uninitialized() {
		if (size_ < capacity_) {
			size_++;
			return data_[size_ - 1];
		} else {
			ExtendByOne();
			return data_[size_ - 1];
		}
	}

	void push_back(const T &t) {
		T &dest = push_uninitialized();
		dest = t;
	}

	// Move constructor
	FastVec(FastVec &&other) {
		data_ = other.data_;
		size_ = other.size_;
		capacity_ = other.capacity_;
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	FastVec &operator=(FastVec &&other)	{
		if (this != &other) {
			delete[] data_;
			data_ = other.data_;
			size_ = other.size_;
			capacity_ = other.capacity_;
			other.data_ = nullptr;
			other.size_ = 0;
			other.capacity_ = 0;
		}
		return *this;
	}

	// No copy constructor.
	FastVec(const FastVec &other) = delete;
	FastVec &operator=(const FastVec &other) = delete;

	size_t size() const { return size_; }
	size_t capacity() const { return capacity_; }
	void clear() { size_ = 0; }
	bool empty() const { return size_ == 0; }

	const T *data() const { return data_; }

	T *begin() { return data_; }
	T *end() { return data_ + size_; }
	const T *begin() const { return data_; }
	const T *end() const { return data_ + size_; }

	// Out of bounds (past size() - 1) is undefined behavior.
	T &operator[] (const size_t index) { return data_[index]; }
	const T &operator[] (const size_t index) const { return data_[index]; }
	T &at(const size_t index) { return data_[index]; }
	const T &at(const size_t index) const { return data_[index]; }

	// These two are invalid if empty().
	const T &back() const { return (*this)[size() - 1]; }
	const T &front() const { return (*this)[0]; }

	// Limited functionality for inserts and similar, add as needed.
	T &insert(T *iter) {
		int pos = iter - data_;
		ExtendByOne();
		if (pos + 1 < (int)size_) {
			memmove(data_ + pos + 1, data_ + pos, (size_ - pos) * sizeof(T));
		}
		return data_[pos];
	}

	void insert(T *destIter, const T *beginIter, const T *endIter) {
		int pos = destIter - data_;
		if (beginIter == endIter)
			return;
		size_t newItems = endIter - beginIter;
		IncreaseCapacityTo(size_ + newItems);
		memmove(data_ + pos + newItems, data_ + pos, (size_ - pos) * sizeof(T));
		memcpy(data_ + pos, beginIter, newItems * sizeof(T));
		size_ += newItems;
	}

	void resize(size_t size) {
		if (size < size_) {
			size_ = size;
		} else {
			// TODO
		}
	}

	void reserve(size_t newCapacity) {
		IncreaseCapacityTo(newCapacity);
	}

	void extend(const T *newData, size_t count) {
		IncreaseCapacityTo(size_ + count);
		memcpy(data_ + size_, newData, count * sizeof(T));
		size_ += count;
	}

	T *extend_uninitialized(size_t count) {
		size_t sz = size_;
		if (size_ + count <= capacity_) {
			size_ += count;
			return &data_[sz];
		} else {
			size_t newCapacity = size_ + count * 2;  // Leave some extra room when growing in all cases
			if (newCapacity < capacity_ * 2) {
				// Standard amortized O(1).
				newCapacity = capacity_ * 2;
			}
			IncreaseCapacityTo(newCapacity);
			size_ += count;
			return &data_[sz];
		}
	}

	void LockCapacity() {
#ifdef _DEBUG
		capacityLocked_ = true;
#endif
	}

private:
	void IncreaseCapacityTo(size_t newCapacity) {
#ifdef _DEBUG
		_dbg_assert_(!capacityLocked_);
#endif
		if (newCapacity <= capacity_)
			return;
		T *oldData = data_;
		data_ = (T *)malloc(sizeof(T) * newCapacity);
		_assert_msg_(data_ != nullptr, "%d", (int)newCapacity);
		if (capacity_ != 0) {
			memcpy(data_, oldData, sizeof(T) * size_);
			free(oldData);
		}
		capacity_ = newCapacity;
	}

	void ExtendByOne() {
		// We don't really extend capacity by one though - instead we use
		// the usual doubling amortization.
		size_t newCapacity = capacity_ * 2;
		if (newCapacity < 16) {
			newCapacity = 16;
		}
		IncreaseCapacityTo(newCapacity);
		size_++;
	}

	size_t size_ = 0;
	size_t capacity_ = 0;
	T *data_ = nullptr;
#ifdef _DEBUG
	bool capacityLocked_ = false;
#endif
};

// Simple cyclical vector.
template <class T, size_t size>
class HistoryBuffer {
public:
	T &Add(size_t index) {
#ifdef _DEBUG
		_dbg_assert_((int64_t)index >= 0);
#endif
		if (index > maxIndex_)
			maxIndex_ = index;
		T &entry = data_[index % size];
		entry = T{};
		return entry;
	}

	const T &Back(size_t index) const {
#ifdef _DEBUG
		_dbg_assert_(index < maxIndex_ && index < size);
#endif
		return data_[(maxIndex_ - index) % size];
	}

	// Out of bounds (past size() - 1) is undefined behavior.
	T &operator[] (const size_t index) {
#ifdef _DEBUG
		_dbg_assert_(index <= maxIndex_);
#endif
		return data_[index % size];
	}
	const T &operator[] (const size_t index) const {
#ifdef _DEBUG
		_dbg_assert_(index <= maxIndex_);
#endif
		return data_[index % size];
	}
	size_t MaxIndex() const {
		return maxIndex_;
	}

private:
	T data_[size]{};
	size_t maxIndex_ = 0;
};
