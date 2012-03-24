#pragma once

template<class T>
class scoped_ptr {
 public:
  scoped_ptr() : ptr_(0) {}
  scoped_ptr(T *p) : ptr_(p) {}
  ~scoped_ptr() {
    delete ptr_;
  }
  void reset(T *p) {
		delete ptr_;
    ptr_ = p;
  }
	T *release() {
		T *p = ptr_;
		ptr_ = 0;
		return p;
	}
	T *operator->() { return ptr_; }
	const T *operator->() const { return ptr_; }

 private:
	// don't copy that floppy
	scoped_ptr(const scoped_ptr<T> &other);
	void operator=(const scoped_ptr<T> &other);
  T *ptr_;
};
