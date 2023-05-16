#pragma once

// Yet another replacement for std::vector, this time for use in graphics queues.
// Its major difference is that you can append uninitialized structures and initialize them after.
// This is not allows by std::vector but is very useful for our sometimes oversized unions.
// Also, copies during resize are done by memcpy, not by any move constructor or similar.

#include <cstdlib>
#include <cstring>

template<class T>
class FastVec {
public:
	FastVec() {}
	FastVec(size_t initialCapacity) {
		capacity_ = initialCapacity;
		data_ = (T *)malloc(initialCapacity * sizeof(T));
	}
	~FastVec() { if (data_) free(data_); }

	T *push_back() {
		if (size_ < capacity_) {
			size_++;
			return data_ + size_ - 1;
		} else {
			T *oldData = data_;
			size_t newCapacity = capacity_ * 2;
			if (newCapacity < 16) {
				newCapacity = 16;
			}
			data_ = (T *)malloc(sizeof(T) * newCapacity);
			if (capacity_ != 0) {
				memcpy(data_, oldData, sizeof(T) * size_);
				free(oldData);
			}
			size_++;
			capacity_ = newCapacity;
			return data_ + size_ - 1;
		}
	}

	void push_back(const T &t) {
		T *dest = push_back();
		*dest = t;
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

	T *begin() { return data_; }
	T *end() { return data_ + size_; }
	const T *begin() const { return data_; }
	const T *end() const { return data_ + size_; }

	// Out of bounds (past size() - 1) is undefined behavior.
	T &operator[] (const size_t index) { return data_[index]; }
	const T &operator[] (const size_t index) const { return data_[index]; }

	// These two are invalid if empty().
	const T &back() const { return (*this)[size() - 1]; }
	const T &front() const { return (*this)[0]; }

private:
	T *data_ = nullptr;
	size_t size_ = 0;
	size_t capacity_ = 0;
};
