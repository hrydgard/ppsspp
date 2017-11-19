#include "stdafx.h"
#include "ByteArray.h"
#include "Util/Util.h"

ByteArray::ByteArray()
{
	data_ = NULL;
	size_ = allocatedSize_ = 0;
}

ByteArray::ByteArray(const ByteArray& other)
{
	data_ = NULL;
	size_ = allocatedSize_ = 0;
	append(other);
}

ByteArray::ByteArray(byte* data, size_t size)
{
	data_ = NULL;
	size_ = allocatedSize_ = 0;
	append(data,size);
}

ByteArray::ByteArray(ByteArray&& other)
{
	data_ = other.data_;
	size_ = other.size_;
	allocatedSize_ = other.allocatedSize_;
	other.data_ = NULL;
	other.allocatedSize_ = other.size_ = 0;
}

ByteArray::~ByteArray()
{
	free(data_);
}

ByteArray& ByteArray::operator=(ByteArray& other)
{
	free(data_);
	data_ = NULL;
	size_ = allocatedSize_ = 0;
	append(other);

	return *this;
}

ByteArray& ByteArray::operator=(ByteArray&& other)
{
	data_ = other.data_;
	size_ = other.size_;
	allocatedSize_ = other.allocatedSize_;
	other.data_ = NULL;
	other.allocatedSize_ = other.size_ = 0;
	return *this;
}

void ByteArray::grow(size_t neededSize)
{
	if (neededSize < allocatedSize_) return;

	// align to next 0.5kb... it's a start
	allocatedSize_ = ((neededSize+511)/512)*512;
	if (data_ == NULL)
	{
		data_ = (byte*) malloc(allocatedSize_);
	} else {
		data_ = (byte*) realloc(data_,allocatedSize_);
	}
}

size_t ByteArray::append(const ByteArray& other)
{
	size_t oldSize = size();
	size_t otherSize = other.size();
	grow(size()+otherSize);
	memcpy(&data_[size_],other.data(),otherSize);
	size_ += otherSize;
	return oldSize;
}

size_t ByteArray::append(void* data, size_t size)
{
	size_t oldSize = this->size();
	grow(this->size()+size);
	memcpy(&data_[size_],data,size);
	this->size_ += size;
	return oldSize;
}

void ByteArray::replaceBytes(size_t pos, byte* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		replaceByte(pos+i,data[i]);
	}
}

void ByteArray::reserveBytes(size_t count, byte value)
{
	grow(this->size()+count);
	memset(&data_[size_],value,count);
	size_ += count;
}

void ByteArray::alignSize(size_t alignment)
{
	if (alignment <= 0) return;

	while (size_ % alignment)
	{
		appendByte(0);
	}
}

void ByteArray::resize(size_t newSize)
{
	grow(newSize);
	size_ = newSize;
}

ByteArray ByteArray::mid(size_t start, ssize_t length)
{
	ByteArray ret;

	if (length < 0)
		length = size_-start;

	if (start >= size_)
		return ret;

	ret.grow(length);
	ret.size_ = length;
	memcpy(ret.data_,&data_[start],length);
	return ret;
}

ByteArray ByteArray::fromFile(const std::wstring& fileName, long start, size_t size)
{
	ByteArray ret;
	
	FILE* input = openFile(fileName,OpenFileMode::ReadBinary);
	if (input == NULL)
		return ret;

	fseek(input,0,SEEK_END);
	long fileSize = ftell(input);

	if (start >= fileSize)
	{
		fclose(input);
		return ret;
	}

	if (size == 0 || start+(long)size > fileSize)
		size = fileSize-start;

	fseek(input,start,SEEK_SET);

	ret.grow(size);
	ret.size_ = fread(ret.data(),1,size,input);
	fclose(input);

	return ret;
}

bool ByteArray::toFile(const std::wstring& fileName)
{
	FILE* output = openFile(fileName,OpenFileMode::WriteBinary);
	if (output == NULL) return false;
	size_t length = fwrite(data_,1,size_,output);
	fclose(output);
	return length == size_;
}