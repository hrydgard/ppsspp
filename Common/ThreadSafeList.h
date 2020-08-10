// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <list>

template < typename T, class Alloc = std::allocator<T> >
class ThreadSafeList {
public:
	explicit ThreadSafeList(const Alloc &a = Alloc()) : list(a) {}
	explicit ThreadSafeList(std::size_t n, const T &v = T(), const Alloc &a = Alloc()) : list(n, v, a) {}
	ThreadSafeList(const std::list<T, Alloc> &other) : list(other) {}
	ThreadSafeList(const ThreadSafeList &other) {
		std::lock_guard<std::mutex> guard(other.lock);
		list.assign(other.list);
	}

	template <class Iter>
	ThreadSafeList(Iter first, Iter last, const Alloc &a = Alloc()) : list(first, last, a) {}

	inline T front() const {
		std::lock_guard<std::mutex> guard(lock);
		return list.front();
	}

	inline void pop_front() {
		std::lock_guard<std::mutex> guard(lock);
		return list.pop_front();
	}

	inline void push_front(const T &v) {
		std::lock_guard<std::mutex> guard(lock);
		return list.push_front(v);
	}

	inline T back() const {
		std::lock_guard<std::mutex> guard(lock);
		return list.back();
	}

	inline void pop_back() {
		std::lock_guard<std::mutex> guard(lock);
		return list.pop_back();
	}

	inline void push_back(const T &v) {
		std::lock_guard<std::mutex> guard(lock);
		return list.push_back(v);
	}

	bool empty() const {
		std::lock_guard<std::mutex> guard(lock);
		return list.empty();
	}

	inline void clear() {
		std::lock_guard<std::mutex> guard(lock);
		return list.clear();
	}

	void DoState(PointerWrap &p) {
		std::lock_guard<std::mutex> guard(lock);
		Do(p, list);
	}

private:
	mutable std::mutex lock;
	std::list<T, Alloc> list;
};

