#pragma once

#include "../../Globals.h"

// pool allocator
template <class T, int size>
class Pool
{
	T pool[size];
	int count;
public:
	Pool()
	{
		Reset();
	}
	void Reset()
	{
		count=0;
	}
	T* Alloc()
	{
		_dbg_assert_msg_(CPU,count<size-1,"Pool allocator overrun!");
		return &pool[count++];
	}
};
