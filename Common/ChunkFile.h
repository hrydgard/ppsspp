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

#ifndef _POINTERWRAP_H_
#define _POINTERWRAP_H_

// Extremely simple serialization framework.

// (mis)-features:
// + Super fast
// + Very simple
// + Same code is used for serialization and deserializaition (in most cases)
// - Zero backwards/forwards compatibility
// - Serialization code for anything complex has to be manually written.

#include <map>
#include <vector>
#include <deque>
#include <string>
#include <list>
#include <set>
#ifndef __SYMBIAN32__
#if defined(IOS) || defined(MACGNUSTD)
#include <tr1/type_traits>
#else
#include <type_traits>
#endif
#endif

#include "Common.h"
#include "FileUtil.h"
#include "../ext/snappy/snappy-c.h"

#if defined(IOS) || defined(MACGNUSTD)
namespace std {
	using tr1::is_pointer;
}
#endif
#ifdef __SYMBIAN32__
namespace std {
	template <bool bool_value>
	struct bool_constant {
		typedef bool_constant<bool_value> type;
		static const bool value = bool_value;
	};
	template <bool bool_value> const bool bool_constant<bool_value>::value;
	template <typename T> struct is_pointer : public bool_constant<false> {};
	template <typename T> struct is_pointer<T*> : public bool_constant<true> {};
}
#endif

template <class T>
struct LinkedListItem : public T
{
	LinkedListItem<T> *next;
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

	void SetMode(Mode mode_) {mode = mode_;}
	Mode GetMode() const {return mode;}
	u8 **GetPPtr() {return ptr;}
	void SetError(Error error_)
	{
		if (error < error_)
			error = error_;
		if (error > ERROR_WARNING)
			mode = PointerWrap::MODE_MEASURE;
	}

	void DoVoid(void *data, int size)
	{
		switch (mode) {
		case MODE_READ:	memcpy(data, *ptr, size); break;
		case MODE_WRITE: memcpy(*ptr, data, size); break;
		case MODE_MEASURE: break;  // MODE_MEASURE - don't need to do anything
		case MODE_VERIFY: for(int i = 0; i < size; i++) _dbg_assert_msg_(COMMON, ((u8*)data)[i] == (*ptr)[i], "Savestate verification failure: %d (0x%X) (at %p) != %d (0x%X) (at %p).\n", ((u8*)data)[i], ((u8*)data)[i], &((u8*)data)[i], (*ptr)[i], (*ptr)[i], &(*ptr)[i]); break;
		default: break;  // throw an error?
		}
		(*ptr) += size;
	}
	
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
		T dv;
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
					K first = 0;
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
					Do(itr->first);
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
		T dv;
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
					K first;
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
		T dv;
		DoVector(x, dv);
	}


	template<class T>
	void DoPOD(std::vector<T> &x)
	{
		T dv;
		DoVectorPOD(x, dv);
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

	template<class T>
	void DoVectorPOD(std::vector<T> &x, T &default_val)
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
		T dv;
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
		T dv;
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
					T it;
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
	void Do(std::string &x) 
	{
		int stringLen = (int)x.length() + 1;
		Do(stringLen);
		
		switch (mode) {
		case MODE_READ:		x = (char*)*ptr; break;
		case MODE_WRITE:	memcpy(*ptr, x.c_str(), stringLen); break;
		case MODE_MEASURE: break;
		case MODE_VERIFY: _dbg_assert_msg_(COMMON, !strcmp(x.c_str(), (char*)*ptr), "Savestate verification failure: \"%s\" != \"%s\" (at %p).\n", x.c_str(), (char*)*ptr, ptr); break;
		}
		(*ptr) += stringLen;
	}

	void Do(std::wstring &x) 
	{
		int stringLen = sizeof(wchar_t)*((int)x.length() + 1);
		Do(stringLen);

		switch (mode) {
		case MODE_READ:		x = (wchar_t*)*ptr; break;
		case MODE_WRITE:	memcpy(*ptr, x.c_str(), stringLen); break;
		case MODE_MEASURE: break;
		case MODE_VERIFY: _dbg_assert_msg_(COMMON, x == (wchar_t*)*ptr, "Savestate verification failure: \"%ls\" != \"%ls\" (at %p).\n", x.c_str(), (wchar_t*)*ptr, ptr); break;
		}
		(*ptr) += stringLen;
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
	void DoPOD(T &x) {
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

	void DoMarker(const char* prevName, u32 arbitraryNumber=0x42)
	{
		u32 cookie = arbitraryNumber;
		Do(cookie);
		if(mode == PointerWrap::MODE_READ && cookie != arbitraryNumber)
		{
			PanicAlertT("Error: After \"%s\", found %d (0x%X) instead of save marker %d (0x%X). Aborting savestate load...", prevName, cookie, cookie, arbitraryNumber, arbitraryNumber);
			SetError(ERROR_FAILURE);
		}
	}
};


class CChunkFileReader
{
public:
	// Load file template
	template<class T>
	static bool Load(const std::string& _rFilename, int _Revision, T& _class, std::string* _failureReason) 
	{
		INFO_LOG(COMMON, "ChunkReader: Loading %s" , _rFilename.c_str());
		_failureReason->clear();
		_failureReason->append("LoadStateWrongVersion");

		if (!File::Exists(_rFilename)) {
			_failureReason->clear();
			_failureReason->append("LoadStateDoesntExist");
			ERROR_LOG(COMMON, "ChunkReader: File doesn't exist");
			return false;
		}
				
		// Check file size
		const u64 fileSize = File::GetSize(_rFilename);
		static const u64 headerSize = sizeof(SChunkHeader);
		if (fileSize < headerSize)
		{
			ERROR_LOG(COMMON,"ChunkReader: File too small");
			return false;
		}

		File::IOFile pFile(_rFilename, "rb");
		if (!pFile)
		{
			ERROR_LOG(COMMON,"ChunkReader: Can't open file for reading");
			return false;
		}

		// read the header
		SChunkHeader header;
		if (!pFile.ReadArray(&header, 1))
		{
			ERROR_LOG(COMMON,"ChunkReader: Bad header size");
			return false;
		}
		
		// Check revision
		if (header.Revision != _Revision)
		{
			ERROR_LOG(COMMON,"ChunkReader: Wrong file revision, got %d expected %d",
				header.Revision, _Revision);
			return false;
		}
		
		// get size
		const int sz = (int)(fileSize - headerSize);
		if (header.ExpectedSize != sz)
		{
			ERROR_LOG(COMMON,"ChunkReader: Bad file size, got %d expected %d",
				sz, header.ExpectedSize);
			return false;
		}
		
		// read the state
		u8* buffer = new u8[sz];
		if (!pFile.ReadBytes(buffer, sz))
		{
			ERROR_LOG(COMMON,"ChunkReader: Error reading file");
			return false;
		}

		u8 *ptr = buffer;
		u8 *buf = buffer;
		if (header.Compress) {
			u8 *uncomp_buffer = new u8[header.UncompressedSize];
			size_t uncomp_size = header.UncompressedSize;
			snappy_uncompress((const char *)buffer, sz, (char *)uncomp_buffer, &uncomp_size);
			if ((int)uncomp_size != header.UncompressedSize) {
				ERROR_LOG(COMMON,"Size mismatch: file: %i  calc: %i", (int)header.UncompressedSize, (int)uncomp_size);
			}
			ptr = uncomp_buffer;
			buf = uncomp_buffer;
			delete [] buffer;
		}

		PointerWrap p(&ptr, PointerWrap::MODE_READ);
		_class.DoState(p);
		delete[] buf;
		
		INFO_LOG(COMMON, "ChunkReader: Done loading %s" , _rFilename.c_str());
		return p.error != p.ERROR_FAILURE;
	}
	
	// Save file template
	template<class T>
	static bool Save(const std::string& _rFilename, int _Revision, T& _class)
	{
		INFO_LOG(COMMON, "ChunkReader: Writing %s" , _rFilename.c_str());

		File::IOFile pFile(_rFilename, "wb");
		if (!pFile)
		{
			ERROR_LOG(COMMON,"ChunkReader: Error opening file for write");
			return false;
		}

		bool compress = true;

		// Get data
		u8 *ptr = 0;
		PointerWrap p(&ptr, PointerWrap::MODE_MEASURE);
		_class.DoState(p);
		size_t const sz = (size_t)ptr;
		
		u8 * buffer = new u8[sz];
		ptr = &buffer[0];
		p.SetMode(PointerWrap::MODE_WRITE);
		_class.DoState(p);

		// Create header
		SChunkHeader header;
		header.Compress = compress ? 1 : 0;
		header.Revision = _Revision;
		header.ExpectedSize = (int)sz;
		header.UncompressedSize = (int)sz;
		
		// Write to file
		if (compress) {
			size_t comp_len = snappy_max_compressed_length(sz);
			u8 *compressed_buffer = new u8[comp_len];
			snappy_compress((const char *)buffer, sz, (char *)compressed_buffer, &comp_len);
			delete [] buffer;
			header.ExpectedSize = (int)comp_len;
			if (!pFile.WriteArray(&header, 1))
			{
				ERROR_LOG(COMMON,"ChunkReader: Failed writing header");
				return false;
			}
			if (!pFile.WriteBytes(&compressed_buffer[0], comp_len)) {
				ERROR_LOG(COMMON,"ChunkReader: Failed writing compressed data");
				return false;
			}	else {
				INFO_LOG(COMMON, "Savestate: Compressed %i bytes into %i", (int)sz, (int)comp_len);
			}
			delete [] compressed_buffer;
		} else {
			if (!pFile.WriteArray(&header, 1))
			{
				ERROR_LOG(COMMON,"ChunkReader: Failed writing header");
				return false;
			}
			if (!pFile.WriteBytes(&buffer[0], sz))
			{
				ERROR_LOG(COMMON,"ChunkReader: Failed writing data");
				return false;
			}
			delete [] buffer;
		}
		
		INFO_LOG(COMMON,"ChunkReader: Done writing %s", 
				 _rFilename.c_str());
		return p.error != p.ERROR_FAILURE;
	}
	
	template <class T>
	static bool Verify(T& _class)
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

		return true;
	}

private:
	struct SChunkHeader
	{
		int Revision;
		int Compress;
		int ExpectedSize;
		int UncompressedSize;
	};
};

#endif  // _POINTERWRAP_H_
