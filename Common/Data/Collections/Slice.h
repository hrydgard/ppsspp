#pragma once

#include <vector>

// Like a const begin/end pair, just more convenient to use (and can only be used for linear array data).
// Inspired by Rust's slices and Google's StringPiece.
template <class T>
struct Slice {
	Slice() : data_(nullptr), size_(0) {}

	// View some memory as a slice.
	Slice(const T *data, size_t size) : data_(data), size_(size) {}

	// Intentionally non-explicit.
	// View a const array as a slice.
	template<size_t N>
	Slice(const T(&data)[N]) : data_(data), size_(N) {}

	// Intentionally non-explicit.
	// View a const array as a slice.
	Slice(const std::vector<T> &data) : data_(data.data()), size_(data.size()) {}

	const T &operator[](size_t index) const {
		return data_[index];
	}

	size_t size() const {
		return size_;
	}

	// "Iterators"
	const T *begin() const {
		return data_;
	}
	const T *end() const {
		return data_ + size_;
	}

	static Slice empty() {
		return Slice<T>(nullptr, 0);
	}

	bool is_empty() const {
		return size_ == 0;
	}

private:
	const T *data_;
	size_t size_;
};
