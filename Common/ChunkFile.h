// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

// Extremely simple serialization framework.
// Currently mis-named, a native ChunkFile is something different (a RIFF file)

// (mis)-features:
// + Super fast
// + Very simple
// + Same code is used for serialization and deserializaition (in most cases)
// + Sections can be versioned for backwards/forwards compatibility
// - Serialization code for anything complex has to be manually written.

#include <map>
#include <deque>
#include <list>
#include <set>
#if defined(IOS) || defined(MACGNUSTD)
#include <tr1/type_traits>
#else
#include <type_traits>
#endif

#include "Common.h"
#include "FileUtil.h"
#include "../ext/snappy/snappy-c.h"

#if defined(IOS) || defined(MACGNUSTD)
namespace std {
	using tr1::is_pointer;
}
#endif

template <class T>
struct LinkedListItem : public T
{
	LinkedListItem<T> *next;
};

class PointerWrap;

class PointerWrapSection
{
public:
	PointerWrapSection(PointerWrap &p, int ver, const char *title) : p_(p), ver_(ver), title_(title) {
	}
	~PointerWrapSection();
	
	bool operator == (const int &v) const { return ver_ == v; }
	bool operator != (const int &v) const { return ver_ != v; }
	bool operator <= (const int &v) const { return ver_ <= v; }
	bool operator >= (const int &v) const { return ver_ >= v; }
	bool operator <  (const int &v) const { return ver_ < v; }
	bool operator >  (const int &v) const { return ver_ > v; }

	operator bool() const  {
		return ver_ > 0;
	}

private:
	PointerWrap &p_;
	int ver_;
	const char *title_;
};

// Wrapper class
class PointerWrap
{
	// This makes it a compile error if you forget to define DoState() on non-POD.
	// Which also can be a problem, for example struct tm is non-POD on linux, for whatever reason...
#ifdef _MSC_VER
	template<typename T, bool isPOD = std::is_pod<T>::value, bool isPointer = std::is_pointer<T>::value>
#else
	template<typename T, bool isPOD = __is_pod(T), bool isPointer = std::is_pointer<T>::value>
#endif
	struct DoHelper
	{
		static void DoArray(PointerWrap *p, T *x, int count)
		{
			for (int i = 0; i < count; ++i)
				p->Do(x[i]);
		}

		static void Do(PointerWrap *p, T &x)
		{
			p->DoClass(x);
		}
	};

	template<typename T>
	struct DoHelper<T, true, false>
	{
		static void DoArray(PointerWrap *p, T *x, int count)
		{
			p->DoVoid((void *)x, sizeof(T) * count);
		}

		static void Do(PointerWrap *p, T &x)
		{
			p->DoVoid((void *)&x, sizeof(x));
		}
	};

public:
	enum Mode {
		MODE_READ = 1, // load
		MODE_WRITE, // save
		MODE_MEASURE, // calculate size
		MODE_VERIFY, // compare
	};

	enum Error {
		ERROR_NONE = 0,
		ERROR_WARNING = 1,
		ERROR_FAILURE = 2,
	};

	u8 **ptr;
	Mode mode;
	Error error;

public:
	PointerWrap(u8 **ptr_, Mode mode_) : ptr(ptr_), mode(mode_), error(ERROR_NONE) {}
	PointerWrap(unsigned char **ptr_, int mode_) : ptr((u8**)ptr_), mode((Mode)mode_), error(ERROR_NONE) {}

	PointerWrapSection Section(const char *title, int ver);

	// The returned object can be compared against the version that was loaded.
	// This can be used to support versions as old as minVer.
	// Version = 0 means the section was not found.
	PointerWrapSection Section(const char *title, int minVer, int ver);

	void SetMode(Mode mode_) {mode = mode_;}
	Mode GetMode() const {return mode;}
	u8 **GetPPtr() {return ptr;}
	void SetError(Error error_);

	// Same as DoVoid, except doesn't advance pointer if it doesn't match on read.
	bool ExpectVoid(void *data, int size);
	void DoVoid(void *data, int size);
	
	template<class K, class T>
	void Do(std::map<K, T *> &x)
	{
		if (mode == MODE_READ)
		{
			for (auto it = x.begin(), end = x.end(); it != end; ++it)
			{
				if (it->second != NULL)
					delete it->second;
			}
		}
		T *dv = NULL;
		DoMap(x, dv);
	}

	template<class K, class T>
	void Do(std::map<K, T> &x)
	{
		T dv = T();
		DoMap(x, dv);
	}

	template<class K, class T>
	void DoMap(std::map<K, T> &x, T &default_val)
	{
		unsigned int number = (unsigned int)x.size();
		Do(number);
		switch (mode) {
		case MODE_READ:
			{
				x.clear();
				while (number > 0)
				{
					K first = K();
					Do(first);
					T second = default_val;
					Do(second);
					x[first] = second;
					--number;
				}
			}
			break;
		case MODE_WRITE:
		case MODE_MEASURE:
		case MODE_VERIFY:
			{
				typename std::map<K, T>::iterator itr = x.begin();
				while (number > 0)
				{
					K first = itr->first;
					Do(first);
					Do(itr->second);
					--number;
					++itr;
				}
			}
			break;
		}
	}

	template<class K, class T>
	void Do(std::multimap<K, T *> &x)
	{
		if (mode == MODE_READ)
		{
			for (auto it = x.begin(), end = x.end(); it != end; ++it)
			{
				if (it->second != NULL)
					delete it->second;
			}
		}
		T *dv = NULL;
		DoMultimap(x, dv);
	}

	template<class K, class T>
	void Do(std::multimap<K, T> &x)
	{
		T dv = T();
		DoMultimap(x, dv);
	}

	template<class K, class T>
	void DoMultimap(std::multimap<K, T> &x, T &default_val)
	{
		unsigned int number = (unsigned int)x.size();
		Do(number);
		switch (mode) {
		case MODE_READ:
			{
				x.clear();
				while (number > 0)
				{
					K first = K();
					Do(first);
					T second = default_val;
					Do(second);
					x.insert(std::make_pair(first, second));
					--number;
				}
			}
			break;
		case MODE_WRITE:
		case MODE_MEASURE:
		case MODE_VERIFY:
			{
				typename std::multimap<K, T>::iterator itr = x.begin();
				while (number > 0)
				{
					Do(itr->first);
					Do(itr->second);
					--number;
					++itr;
				}
			}
			break;
		}
	}

	// Store vectors.
	template<class T>
	void Do(std::vector<T *> &x)
	{
		T *dv = NULL;
		DoVector(x, dv);
	}

	template<class T>
	void Do(std::vector<T> &x)
	{
		T dv = T();
		DoVector(x, dv);
	}

	template<class T>
	void Do(std::vector<T> &x, T &default_val)
	{
		DoVector(x, default_val);
	}

	template<class T>
	void DoVector(std::vector<T> &x, T &default_val)
	{
		u32 vec_size = (u32)x.size();
		Do(vec_size);
		x.resize(vec_size, default_val);
		if (vec_size > 0)
			DoArray(&x[0], vec_size);
	}

	// Store deques.
	template<class T>
	void Do(std::deque<T *> &x)
	{
		T *dv = NULL;
		DoDeque(x, dv);
	}

	template<class T>
	void Do(std::deque<T> &x)
	{
		T dv = T();
		DoDeque(x, dv);
	}

	template<class T>
	void DoDeque(std::deque<T> &x, T &default_val)
	{
		u32 deq_size = (u32)x.size();
		Do(deq_size);
		x.resize(deq_size, default_val);
		u32 i;
		for(i = 0; i < deq_size; i++)
			Do(x[i]);
	}

	// Store STL lists.
	template<class T>
	void Do(std::list<T *> &x)
	{
		T *dv = NULL;
		Do(x, dv);
	}

	template<class T>
	void Do(std::list<T> &x)
	{
		T dv = T();
		DoList(x, dv);
	}

	template<class T>
	void Do(std::list<T> &x, T &default_val)
	{
		DoList(x, default_val);
	}

	template<class T>
	void DoList(std::list<T> &x, T &default_val)
	{
		u32 list_size = (u32)x.size();
		Do(list_size);
		x.resize(list_size, default_val);

		typename std::list<T>::iterator itr, end;
		for (itr = x.begin(), end = x.end(); itr != end; ++itr)
			Do(*itr);
	}

	// Store STL sets.
	template <class T>
	void Do(std::set<T *> &x)
	{
		if (mode == MODE_READ)
		{
			for (auto it = x.begin(), end = x.end(); it != end; ++it)
			{
				if (*it != NULL)
					delete *it;
			}
		}
		DoSet(x);
	}

	template <class T>
	void Do(std::set<T> &x)
	{
		DoSet(x);
	}

	template <class T>
	void DoSet(std::set<T> &x)
	{
		unsigned int number = (unsigned int)x.size();
		Do(number);

		switch (mode)
		{
		case MODE_READ:
			{
				x.clear();
				while (number-- > 0)
				{
					T it = T();
					Do(it);
					x.insert(it);
				}
			}
			break;
		case MODE_WRITE:
		case MODE_MEASURE:
		case MODE_VERIFY:
			{
				typename std::set<T>::iterator itr = x.begin();
				while (number-- > 0)
					Do(*itr++);
			}
			break;

		default:
			ERROR_LOG(COMMON, "Savestate error: invalid mode %d.", mode);
		}
	}

	// Store strings.
	void Do(std::string &x);
	void Do(std::wstring &x);

	void Do(tm &t);

	template<typename T, typename F>
	void Do(swap_struct_t<T, F> &x) {
		T v = x.swap();
		Do(v);
		x = v;
	}

	template<class T>
	void DoClass(T &x) {
		x.DoState(*this);
	}

	template<class T>
	void DoClass(T *&x) {
		if (mode == MODE_READ)
		{
			if (x != NULL)
				delete x;
			x = new T();
		}
		x->DoState(*this);
	}

	template<class T>
	void DoArray(T *x, int count) {
		DoHelper<T>::DoArray(this, x, count);
	}

	template<class T>
	void Do(T &x) {
		DoHelper<T>::Do(this, x);
	}

	template<class T>
	void DoPointer(T* &x, T*const base) {
		// pointers can be more than 2^31 apart, but you're using this function wrong if you need that much range
		s32 offset = x - base;
		Do(offset);
		if (mode == MODE_READ)
			x = base + offset;
	}

	template<class T, LinkedListItem<T>* (*TNew)(), void (*TFree)(LinkedListItem<T>*), void (*TDo)(PointerWrap&, T*)>
	void DoLinkedList(LinkedListItem<T>*& list_start, LinkedListItem<T>** list_end=0)
	{
		LinkedListItem<T>* list_cur = list_start;
		LinkedListItem<T>* prev = 0;

		while (true)
		{
			u8 shouldExist = (list_cur ? 1 : 0);
			Do(shouldExist);
			if (shouldExist == 1)
			{
				LinkedListItem<T>* cur = list_cur ? list_cur : TNew();
				TDo(*this, (T*)cur);
				if (!list_cur)
				{
					if (mode == MODE_READ)
					{
						cur->next = 0;
						list_cur = cur;
						if (prev)
							prev->next = cur;
						else
							list_start = cur;
					}
					else
					{
						TFree(cur);
						continue;
					}
				}
			}
			else
			{
				if (mode == MODE_READ)
				{
					if (prev)
						prev->next = 0;
					if (list_end)
						*list_end = prev;
					if (list_cur)
					{
						if (list_start == list_cur)
							list_start = 0;
						do
						{
							LinkedListItem<T>* next = list_cur->next;
							TFree(list_cur);
							list_cur = next;
						}
						while (list_cur);
					}
				}
				break;
			}
			prev = list_cur;
			list_cur = list_cur->next;
		}
	}

	void DoMarker(const char *prevName, u32 arbitraryNumber = 0x42);
};

class CChunkFileReader
{
public:
	enum Error {
		ERROR_NONE,
		ERROR_BAD_FILE,
		ERROR_BROKEN_STATE,
	};

	// May fail badly if ptr doesn't point to valid data.
	template<class T>
	static Error LoadPtr(u8 *ptr, T &_class)
	{
		PointerWrap p(&ptr, PointerWrap::MODE_READ);
		_class.DoState(p);

		if (p.error != p.ERROR_FAILURE) {
			return ERROR_NONE;
		} else {
			return ERROR_BROKEN_STATE;
		}
	}

	template<class T>
	static size_t MeasurePtr(T &_class)
	{
		u8 *ptr = 0;
		PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
		_class.DoState(p);
		return (size_t)ptr;
	}

	// Expects ptr to have at least MeasurePtr bytes at ptr.
	template<class T>
	static Error SavePtr(u8 *ptr, T &_class)
	{
		PointerWrap p(&ptr, PointerWrap::MODE_WRITE);
		_class.DoState(p);

		if (p.error != p.ERROR_FAILURE) {
			return ERROR_NONE;
		} else {
			return ERROR_BROKEN_STATE;
		}
	}

	// Load file template
	template<class T>
	static Error Load(const std::string& _rFilename, int _Revision, const char *_VersionString, T& _class, std::string* _failureReason) 
	{
		*_failureReason = "LoadStateWrongVersion";

		u8 *ptr;
		size_t sz;
		Error error = LoadFile(_rFilename, _Revision, _VersionString, ptr, sz, _failureReason);
		if (error == ERROR_NONE) {
			u8 *buf = ptr;
			error = LoadPtr(ptr, _class);
			delete[] buf;
		}
		
		INFO_LOG(COMMON, "ChunkReader: Done loading %s" , _rFilename.c_str());
		if (error == ERROR_NONE) {
			_failureReason->clear();
		}
		return error;
	}

	// Save file template
	template<class T>
	static Error Save(const std::string& _rFilename, int _Revision, const char *_VersionString, T& _class)
	{
		// Get data
		size_t const sz = MeasurePtr(_class);
		u8 *buffer = new u8[sz];
		Error error = SavePtr(buffer, _class);

		if (error == ERROR_NONE)
			error = SaveFile(_rFilename, _Revision, _VersionString, buffer, sz);

		return error;
	}
	
	template <class T>
	static Error Verify(T& _class)
	{
		u8 *ptr = 0;

		// Step 1: Measure the space required.
		PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
		_class.DoState(p);
		size_t const sz = (size_t)ptr;
		std::vector<u8> buffer(sz);

		// Step 2: Dump the state.
		ptr = &buffer[0];
		p.SetMode(PointerWrap::MODE_WRITE);
		_class.DoState(p);

		// Step 3: Verify the state.
		ptr = &buffer[0];
		p.SetMode(PointerWrap::MODE_VERIFY);
		_class.DoState(p);

		return ERROR_NONE;
	}

private:
	static CChunkFileReader::Error LoadFile(const std::string& _rFilename, int _Revision, const char *_VersionString, u8 *&buffer, size_t &sz, std::string *_failureReason);
	static CChunkFileReader::Error SaveFile(const std::string& _rFilename, int _Revision, const char *_VersionString, u8 *buffer, size_t sz);

	struct SChunkHeader
	{
		int Revision;
		int Compress;
		u32 ExpectedSize;
		u32 UncompressedSize;
		char GitVersion[32];
	};
};
