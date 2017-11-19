#pragma once

#include <sys/types.h>

#if defined(_MSC_VER) && !defined(ssize_t)
typedef intptr_t ssize_t;
#endif

typedef unsigned char byte;
class ByteArray
{
public:
	ByteArray();
	ByteArray(const ByteArray& other);
	ByteArray(byte* data, size_t size);
	ByteArray(ByteArray&& other);
	~ByteArray();
	ByteArray& operator=(ByteArray& other);
	ByteArray& operator=(ByteArray&& other);

	size_t append(const ByteArray& other);
	size_t append(void* data, size_t size);
	size_t appendByte(byte b) { return append(&b,1); };
	void replaceByte(size_t pos, byte b) { data_[pos] = b; };
	void replaceBytes(size_t pos, byte* data, size_t size);
	void reserveBytes(size_t count, byte value = 0);
	void alignSize(size_t alignment);

	int getWord(size_t pos, bool bigEndian = false) const
	{
		if (pos+1 >= this->size()) return -1;
		unsigned char* d = (unsigned char*) this->data();

		if (bigEndian == false)
		{
			return d[pos+0] | (d[pos+1] << 8);
		} else {
			return d[pos+1] | (d[pos+0] << 8);
		}
	}

	int getDoubleWord(size_t pos, bool bigEndian = false) const
	{
		if (pos+3 >= this->size()) return -1;
		unsigned char* d = (unsigned char*) this->data();

		if (bigEndian == false)
		{
			return d[pos+0] | (d[pos+1] << 8) | (d[pos+2] << 16) | (d[pos+3] << 24);
		} else {
			return d[pos+3] | (d[pos+2] << 8) | (d[pos+1] << 16) | (d[pos+0] << 24);
		}
	}
	
	void replaceWord(size_t pos, unsigned int w, bool bigEndian = false)
	{
		if (pos+1 >= this->size()) return;
		unsigned char* d = (unsigned char*) this->data();

		if (bigEndian == false)
		{
			d[pos+0] = w & 0xFF;
			d[pos+1] = (w >> 8) & 0xFF;
		} else {
			d[pos+0] = (w >> 8) & 0xFF;
			d[pos+1] = w & 0xFF;
		}
	}

	void replaceDoubleWord(size_t pos, unsigned int w, bool bigEndian = false)
	{
		if (pos+3 >= this->size()) return;
		unsigned char* d = (unsigned char*) this->data();
		
		if (bigEndian == false)
		{
			d[pos+0] = w & 0xFF;
			d[pos+1] = (w >> 8) & 0xFF;
			d[pos+2] = (w >> 16) & 0xFF;
			d[pos+3] = (w >> 24) & 0xFF;
		} else {
			d[pos+0] = (w >> 24) & 0xFF;
			d[pos+1] = (w >> 16) & 0xFF;
			d[pos+2] = (w >> 8) & 0xFF;
			d[pos+3] = w & 0xFF;
		}
	}

	byte& operator [](size_t index)
	{
		return data_[index];
	};
	
	const byte& operator [](size_t index) const
	{
		return data_[index];
	};

	size_t size() const { return size_; };
	byte* data(size_t pos = 0) const { return &data_[pos]; };
	void clear() { size_ = 0; };
	void resize(size_t newSize);
	ByteArray mid(size_t start, ssize_t length = 0);
	ByteArray left(size_t length) { return mid(0,length); };
	ByteArray right(size_t length) { return mid(size_-length,length); };

	static ByteArray fromFile(const std::wstring& fileName, long start = 0, size_t size = 0);
	bool toFile(const std::wstring& fileName);
private:
	void grow(size_t neededSize);
	byte* data_;
	size_t size_;
	size_t allocatedSize_;
};
