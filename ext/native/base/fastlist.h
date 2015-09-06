#pragma once

// An inline fastlist is a fixed size array with a position counter.
// Objects stored in a fastlist must be copyable.
// [] returns the objects in consecutive order, up to and not including
// the value of .size().
// Order is not preserved when removing objects.
template<class T, int max_size>
class InlineFastList {
public:
	InlineFastList() : count_(0) {}
	~InlineFastList() {}

	T& operator [](int index) { return data_[index]; }
	const T& operator [](int index) const { return data_[index]; }
	int size() const { return count_; }

	void Add(T t) {
		data_[count_++] = t;
	}

	void RemoveAt(int index) {
		data_[index] = data_[count_ - 1];
		count_--;
	}

	void Remove(T t) {	// Requires operator==
		for (int i = 0; i < count_; i++) {
			if (data_[i] == t) {
				RemoveAt(i);
				return;
			}
		}
	}

private:
	T data_[max_size];
	int count_;
};
