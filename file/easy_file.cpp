#include <stdio.h>

#include "base/basictypes.h"
#include "file/easy_file.h"

LAMEFile::LAMEFile() : file_(NULL) {
	isOpen = false;
}

LAMEFile::~LAMEFile() { }

bool LAMEFile::open(const char *filename, eFileMode mode) {
	file_ = fopen(filename, mode == FILE_READ ? "rb" : "wb");

	if (!file_) {
		isOpen = false;
	} else {
		isOpen = true;
		if (mode == FILE_READ) {
			fseek(file_, 0, SEEK_END);
			size_ = ftell(file_);
			fseek(file_, 0, SEEK_SET);
		}
	}
	return isOpen;
}

void LAMEFile::close() {
	if (isOpen)	{
		//close the file and reset variables
		fclose(file_);
		file_ = NULL;
		isOpen=false;
	}
}

int LAMEFile::fileSize() {
	if (!isOpen) //of course
		return 0;
	else
		return size_;
}

std::string LAMEFile::readAll() {
	std::string s;
	size_t size = fileSize();
	s.resize(size);
	read(&s[0], size);
	return s;
}

int LAMEFile::write(const void *data, size_t size) {
	if (isOpen) {
		return fwrite(data, 1, size, file_); //we return the number of bytes that actually got written
	} else {
		return 0;
	}
}

int LAMEFile::read(void *data, size_t size) {
	if (isOpen) {
		return fread(data, 1, size, file_);
	} else {
		return 0;
	}
}

int LAMEFile::readInt() {
	int temp;
	if (read(&temp, sizeof(int)))
		return temp;
	else
		return 0;
}

void LAMEFile::writeInt(int i) {
	write(&i, sizeof(int));
}

char LAMEFile::readChar() {
	char temp;
	if (read(&temp, sizeof(char)))
		return temp;
	else
		return 0;
}

void LAMEFile::writeChar(char i) {
	write(&i,sizeof(char));
}
