#include "base/logging.h"
#include "file/chunk_file.h"
#include "file/zip_read.h"

#ifdef __SYMBIAN32__
#undef UNICODE
#endif

//#define CHUNKDEBUG

ChunkFile::ChunkFile(const char *filename, bool _read) {
	data=0;
	fn = filename;
	fastMode=false;
	numLevels=0;
	read=_read;
	pos=0;
	didFail=false;

	fastMode = read ? true : false;

	if (fastMode) {
		size_t size;
		ILOG("VFSReadFile %s", filename);
		data = (uint8_t *)VFSReadFile(filename, &size);
		if (!data) {
			ELOG("Chunkfile fail: %s", filename);
			didFail = true;
			return;
		}
		eof = size;
		return;
	}

	if (file.open(filename, FILE_WRITE)) {
		didFail=false;
		eof=file.fileSize();
	}	else {
		didFail=true;
		return;
	}
}

ChunkFile::~ChunkFile() {
	if (fastMode && data)
		delete [] data;
	else
		file.close();
}

int ChunkFile::readInt() {
	if (pos<eof) {
		/*
		int temp = *(int *)(data+pos);
		pos+=4;
		*/
		pos+=4;
		if (fastMode)
			return *(int *)(data+pos-4);
		else
			return file.readInt();
	}	else {
		return 0;
	}
}

void ChunkFile::writeInt(int i) {
	/*
	*(int *)(data+pos) = i;
	pos+=4;
	*/
#ifndef DEMO_VERSION	//if this is missing.. heheh
	file.writeInt(i);
	pos+=4;
#endif
}

//let's get into the business
bool ChunkFile::descend(uint32 id) {
	id=flipID(id);
	if (read) {
		bool found = false;

		//save information to restore after the next Ascend
		stack[numLevels].parentStartLocation = pos;
		stack[numLevels].parentEOF = eof;

		ChunkInfo temp = stack[numLevels];

		int firstID = 0;
		//let's search through children..
		while(pos<eof) {
			stack[numLevels].ID = readInt();
			if (firstID == 0) firstID=stack[numLevels].ID|1;
			stack[numLevels].length = readInt();
			stack[numLevels].startLocation = pos;

			if (stack[numLevels].ID == id)
			{
				found = true;
				break;
			} else {
				seekTo(pos + stack[numLevels].length); //try next block
			}
		} 

		//if we found nothing, return false so the caller can skip this
		if (!found) {
#ifdef CHUNKDEBUG
			ILOG("Couldn't find %c%c%c%c", id, id>>8, id>>16, id>>24);
#endif
			stack[numLevels]=temp;
			seekTo(stack[numLevels].parentStartLocation);
			return false;
		}

		//descend into it
		//pos was set inside the loop above
		eof = stack[numLevels].startLocation + stack[numLevels].length;
		numLevels++;
#ifdef CHUNKDEBUG
		ILOG("Descended into %c%c%c%c", id, id>>8, id>>16, id>>24);
#endif
		return true;
	} else {
#ifndef DEMO_VERSION	//if this is missing.. heheh
		//write a chunk id, and prepare for filling in length later
		writeInt(id);
		writeInt(0); //will be filled in by Ascend
		stack[numLevels].startLocation=pos;
		numLevels++;
		return true;
#else
		return true;
#endif
	}
}

void ChunkFile::seekTo(int _pos) {
	if (!fastMode)
		file.seekBeg(_pos);
	pos=_pos;
}

//let's ascend out
void ChunkFile::ascend() {
	if (read) {
		//ascend, and restore information
		numLevels--;
		seekTo(stack[numLevels].parentStartLocation);
		eof = stack[numLevels].parentEOF;
#ifdef CHUNKDEBUG
		int id = stack[numLevels].ID;
		ILOG("Ascended out of %c%c%c%c", id, id>>8, id>>16, id>>24);
#endif
		} else {
		numLevels--;
		//now fill in the written length automatically
		int posNow = pos;
		seekTo(stack[numLevels].startLocation - 4);
		writeInt(posNow-stack[numLevels].startLocation);
		seekTo(posNow);
	}
}

//read a block
void ChunkFile::readData(void *what, int count) {
	if (fastMode)
		memcpy(what, data + pos, count);
	else
		file.read(what,count);

	pos+=count;
	char temp[4]; //discarded
	count &= 3;
	if (count) {
		count=4-count;
		if (!fastMode)
			file.read(temp,count);
		pos+=count;
	}
}

//write a block
void ChunkFile::writeData(const void *what, int count) {
	file.write(what, count);
	pos+=count;
	char temp[5]={0,0,0,0,0};
	count &= 3; 
	if (count)
	{
		count=4-count;
		file.write(temp,count);
		pos+=count;
	}
}

/*
void ChunkFile::writeWString(String str) {
	wchar_t *text;
	int len=str.length();
#ifdef UNICODE
#error
	text = str.getPointer();
#else
	text = new wchar_t[len+1];
	str.toUnicode(text);
#endif
	writeInt(len);
	writeData((char *)text, len * sizeof(wchar_t));
#ifndef UNICODE
	delete [] text;
#endif
}
*/

void ChunkFile::writeWString(const std::string &str) {
	unsigned short *text;
	size_t len = str.length();
#ifdef UNICODE
#error
	text = str.c_str();
#else
	text = new unsigned short[len+1];
	for (int i=0; i<len; i++)
		text[i]=str[i];
	text[len]=0;
#endif
	writeInt(len);
	writeData((char *)text, len * sizeof(unsigned short));
#ifndef UNICODE
	delete [] text;
#endif
}

static void toUnicode(const std::string &str, uint16 *t) {
	for (size_t i = 0; i < str.size(); i++) {
		*t++ = str[i];
	}
	*t++ = '\0';
}

static std::string fromUnicode(const uint16_t *src, int len) {
	std::string str;
	str.resize(len);
	for (int i=0; i<len; i++) {
		str[i] = src[i] > 255 ? ' ' : src[i];
	}
	return str;
}

std::string ChunkFile::readWString() {
	int len=readInt();
	uint16_t *text = new uint16_t[len+1];
	readData((char *)text, len*sizeof(uint16_t));
	text[len] = 0;
	std::string temp = fromUnicode(text, len);
	delete [] text;
	return temp;
}

void ChunkFile::writeString(const std::string &str) {
	uint16_t *text;
	int len = str.size();
	text=new uint16_t[len+1];
	toUnicode(str, text);
	writeInt(len);
	writeData((char *)text,len*sizeof(uint16_t));
	delete [] text;
}

std::string ChunkFile::readString() {
	int len=readInt();
	uint16_t *text = new uint16_t[len+1];
	readData((char *)text,len*sizeof(uint16_t));
	text[len]=0;
	std::string temp = fromUnicode(text, len);
	delete [] text;
	return temp;
}
int ChunkFile::getCurrentChunkSize() {
	if (numLevels)
		return stack[numLevels-1].length;
	else
		return 0;
}

