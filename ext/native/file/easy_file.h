#pragma once

// A dumb wrapper around stdio, for convenience. Very old.

#include <stdio.h>
#include <string>

#include "base/basictypes.h"


// Raw file paths, does not go through VFS.

enum eFileMode {
	FILE_READ=5,
	FILE_WRITE=6
};

// TODO: Rename.
class LAMEFile {
public:
	LAMEFile();
	virtual ~LAMEFile();

	bool open(const char *filename, eFileMode mode);
	bool open(std::string filename, eFileMode mode) {
		return open(filename.c_str(), mode);
	}
	void close();

	void writeInt(int i);
	void writeChar(char i);
	int	write(const void *data, size_t size);
	void write(const std::string &str) {
		write((void *)str.data(), str.size());
	}

	int	readInt();
	char readChar();
	int	read(void *data, size_t size);

	std::string readAll();

	int	fileSize();

	void seekBeg(int pos) {
		if (isOpen)	fseek(file_,pos,SEEK_SET);
	}
	void seekEnd(int pos) {
		if (isOpen)	fseek(file_,pos,SEEK_END);
	}
	void seekCurrent(int pos) {
		if (isOpen)	fseek(file_,pos,SEEK_CUR);
	}
private:
	FILE *file_;
	bool isOpen;
	int size_;
};
