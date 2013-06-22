#include "OMAConvert.h"

namespace OMAConvert {

const u32 OMA_EA3_MAGIC = 0x45413301;
const u8 OMA_CODECID_ATRAC3P = 1;
const int OMAHeaderSize = 96;
const int FMT_CHUNK_MAGIC = 0x20746D66;
const int DATA_CHUNK_MAGIC = 0x61746164;
const int SMPL_CHUNK_MAGIC = 0x6C706D73;
const int FACT_CHUNK_MAGIC = 0x74636166;
const int AT3_MAGIC = 0x0270;
const int AT3_PLUS_MAGIC = 0xFFFE;

template <typename T> void BigEndianWriteBuf(u8* buf, T x, int &pos)
{
	int k = sizeof(T);
	for (int i = k - 1; i >= 0; i--)
	{
		buf[pos + i] = (u8)(x & 0xFF);
		x >>= 8;
	}
	pos += k;
}

template <typename T> inline T getBufValue(T* buf, int offsetbytes)
{
	return *(T*)(((u8*)buf) + offsetbytes);
}

inline void WriteBuf(u8* dst, int &pos, u8* src, int size)
{
	memcpy(dst + pos, src, size);
	pos += size;
}

bool isHeader(u8* audioStream, int offset)
{
	const u8 header1 = (u8)0x0F;
	const u8 header2 = (u8)0xD0;
	return (audioStream[offset] == header1) && (audioStream[offset+1] == header2);
}

// header set to the headerbuf, and return it's size
int getOmaHeader(u8 codecId, u8 headerCode0, u8 headerCode1, u8 headerCode2, u8* headerbuf)
{
	int pos = 0;
	BigEndianWriteBuf(headerbuf, (u32)OMA_EA3_MAGIC, pos);
	BigEndianWriteBuf(headerbuf, (u16)OMAHeaderSize, pos);
	BigEndianWriteBuf(headerbuf, (u16)-1, pos);

	// Unknown 24 bytes...
	BigEndianWriteBuf(headerbuf, (u32)0x00000000, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x010f5000, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x00040000, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x0000f5ce, pos);
	BigEndianWriteBuf(headerbuf, (u32)0xd2929132, pos);
	BigEndianWriteBuf(headerbuf, (u32)0x2480451c, pos);

	BigEndianWriteBuf(headerbuf, (u8)codecId, pos);
	BigEndianWriteBuf(headerbuf, (u8)headerCode0, pos);
	BigEndianWriteBuf(headerbuf, (u8)headerCode1, pos);
	BigEndianWriteBuf(headerbuf, (u8)headerCode2, pos);

	while (pos < OMAHeaderSize) BigEndianWriteBuf(headerbuf, (u8)0, pos);

	return pos;
}

int getNextHeaderPosition(u8* audioStream, int curpos, int limit, int frameSize)
{
	int endScan = limit - 1;

	// Most common case: the header can be found at each frameSize
	int offset = curpos + frameSize - 8;
	if (offset < endScan && isHeader(audioStream, offset))
		return offset;
	for (int scan = curpos; scan < endScan; scan++) {
		if (isHeader(audioStream, scan))
			return scan;
	}

	return -1;
}

void releaseStream(u8** stream)
{
	if (*stream) delete [] (*stream);
	*stream = 0;
}

// atrac3plus radio
// 352kbps 0xff
// 320kbps 0xe8
// 256kbps 0xb9
// 192kbps 0x8b
// 160kbps 0x74
// 128kbps 0x5c
//  96kbps 0x45
//  64kbps 0x2e
//  48kbps 0x22
const u8 atrac3plusradio[] = {0xff, 0xe8, 0xb9, 0x8b, 0x74, 0x5c, 0x45, 0x2e, 0x22};
const int atrac3plusradiosize = sizeof(atrac3plusradio);

int convertStreamtoOMA(u8* audioStream, int audioSize, u8** outputStream)
{
	if (!isHeader(audioStream, 0))
	{
		*outputStream = 0;
		return 0;
	}
	u8 headerCode1 = audioStream[2];
	u8 headerCode2 = audioStream[3];

	if (headerCode1 == 0x28)
	{
		bool bsupported = false;
		for (int i = 0; i < atrac3plusradiosize; i++) {
			if (atrac3plusradio[i] == headerCode2)
			{
				bsupported = true;
				break;
			}
		}
		if (bsupported == false)
		{
			*outputStream = 0;
			return 0;
		}
	}
	else
	{
		*outputStream = 0;
		return 0;
	}

	int frameSize = ((headerCode1 & 0x03) << 8) | (headerCode2 & 0xFF) * 8 + 0x10;
	int numCompleteFrames = audioSize / (frameSize + 8);
	int lastFrameSize = audioSize - (numCompleteFrames * (frameSize + 8));

	int omaStreamSize = OMAHeaderSize + numCompleteFrames * frameSize + lastFrameSize;

	// Allocate an OMA stream size large enough (better too large than too short)
	if (audioSize > omaStreamSize) omaStreamSize = audioSize;
	u8* oma = new u8[omaStreamSize];
	int omapos = 0;
	int audiopos = 0;

	omapos += getOmaHeader(OMA_CODECID_ATRAC3P, 0, headerCode1, headerCode2, oma);
	while (audioSize - audiopos > 8) {
		// Skip 8 bytes frame header
		audiopos += 8;
		int nextHeader = getNextHeaderPosition(audioStream, audiopos, audioSize, frameSize);
		u8* frame = audioStream + audiopos;
		int framelimit = audioSize - audiopos;
		if (nextHeader >= 0) {
			framelimit = nextHeader - audiopos;
			audiopos = nextHeader;
		} else
			audiopos = audioSize;
		WriteBuf(oma, omapos, frame, framelimit);
	}

	*outputStream = oma;
	return omapos;
}

int getChunkOffset(u8* riff, int limit, int chunkMagic, int offset) {
	for (int i = offset; i <= limit - 8;) {
		if (getBufValue((int*)riff, i) == chunkMagic)
			return i;
		// Move to next chunk
		int chunkSize = getBufValue((int*)riff, i + 4);
		i += chunkSize + 8;
	}

	return -1;
}

int convertRIFFtoOMA(u8* riff, int riffSize, u8** outputStream, int* readSize)
{
	const int firstChunkOffset = 12;
	int fmtChunkOffset = getChunkOffset(riff, riffSize, FMT_CHUNK_MAGIC, firstChunkOffset);
	if (fmtChunkOffset < 0) {
		*outputStream = 0;
		return 0;
	}
	u8 codecId = getBufValue(riff, fmtChunkOffset + 0x30);
	u8 headerCode0 = getBufValue(riff, fmtChunkOffset + 0x31);
	u8 headerCode1 = getBufValue(riff, fmtChunkOffset + 0x32);
	u8 headerCode2 = getBufValue(riff, fmtChunkOffset + 0x33);

	bool bsupported = false;
	u16 magic = getBufValue((u16*)riff, fmtChunkOffset + 0x08);
	if (magic == AT3_MAGIC)
	{
		u8 key = getBufValue((u8*)riff, fmtChunkOffset + 0x11);
		u16 channel = getBufValue((u16*)riff, fmtChunkOffset + 0x0a);
		switch (key)
		{
		case 0x20:
			{
				// 66kpbs
				codecId = 0;
				headerCode0 = 0x02;
				headerCode1 = 0x20;
				headerCode2 = 0x18;
			}
			break;
		case 0x40:
			{
				// 132kpbs
				codecId = 0;
				headerCode0 = 0x00;
				headerCode1 = 0x20;
				headerCode2 = 0x30;
			}
			break;
		default:
			{
			// 105kpbs
			codecId = 0;
			headerCode0 = 0x00;
			headerCode1 = 0x20;
			headerCode2 = 0x26;
			}
			break;
		}
		if (channel == 2)
			bsupported = true;
		else 
			bsupported = false;
	}
	else if (magic == AT3_PLUS_MAGIC && headerCode0 == 0x00 
		&& headerCode1 == 0x28)
	{
		for (int i = 0; i < atrac3plusradiosize; i++) {
			if (atrac3plusradio[i] == headerCode2)
			{
				bsupported = true;
				break;
			}
		}
	}

	if (bsupported == false)
	{
		*outputStream = 0;
		return 0;
	}

	int dataChunkOffset = getChunkOffset(riff, riffSize, DATA_CHUNK_MAGIC, firstChunkOffset);
	if (dataChunkOffset < 0) {
		*outputStream = 0;
		return 0;
	}
	int dataSize = getBufValue((int*)riff, dataChunkOffset + 4);
	int datapos = dataChunkOffset + 8;

	u8* oma = new u8[OMAHeaderSize + dataSize];
	int omapos = 0;

	omapos += getOmaHeader(codecId, headerCode0, headerCode1, headerCode2, oma);
	WriteBuf(oma, omapos, riff + datapos, dataSize);

	*outputStream = oma;
	if (readSize)
		*readSize = OMAHeaderSize + riffSize - datapos;
	return omapos;
}

int getOMANumberAudioChannels(u8* oma)
{
	int headerParameters = getBufValue((int*)oma, 0x20);
	int channels = (headerParameters >> 18) & 0x7;

	return channels;
}

int getRIFFSize(u8* riff, int bufsize)
{
	const int firstChunkOffset = 12;
	int fmtChunkOffset = getChunkOffset(riff, bufsize, FMT_CHUNK_MAGIC, firstChunkOffset);
	int dataChunkOffset = getChunkOffset(riff, bufsize, DATA_CHUNK_MAGIC, firstChunkOffset);
	if (fmtChunkOffset < 0 || dataChunkOffset < 0)
		return 0;
	int dataSize = getBufValue((int*)riff, dataChunkOffset + 4);
	return dataSize + dataChunkOffset + 8;
}

int getRIFFLoopNum(u8* riff, int bufsize, int *startsample, int *endsample)
{
	const int firstChunkOffset = 12;
	int dataChunkOffset = getChunkOffset(riff, bufsize, DATA_CHUNK_MAGIC, firstChunkOffset);
	if (dataChunkOffset < 0)
		return 0;
	int smplChunkOffset = getChunkOffset(riff, dataChunkOffset, SMPL_CHUNK_MAGIC, firstChunkOffset);
	if (smplChunkOffset < 0)
		return 0;
	int factChunkOffset = getChunkOffset(riff, dataChunkOffset, FACT_CHUNK_MAGIC, firstChunkOffset);
	int atracSampleOffset = 0;
	if (factChunkOffset >= 0) {
		int factChunkSize = getBufValue((int*)riff, factChunkOffset + 4);
		if (factChunkSize >= 8) {
			atracSampleOffset = getBufValue((int*)riff, factChunkOffset + 12);
		}
	}
	int smplChunkSize = getBufValue((int*)riff, smplChunkOffset + 4);
	int checkNumLoops = getBufValue((int*)riff, smplChunkOffset + 36);
	if (smplChunkSize >= 36 + checkNumLoops * 24)
	{
		// find loop info, simple return -1 now for endless loop
		int numLoops = checkNumLoops;
		int loopInfoAddr = smplChunkOffset + 44;
		int loopcounts = (numLoops > 1) ? -1 : 0;
		for (int i = 0; i < 1; i++) {
			if (startsample)
				*startsample = getBufValue((int*)riff, loopInfoAddr + 8) - atracSampleOffset;
			if (endsample)
				*endsample = getBufValue((int*)riff, loopInfoAddr + 12) - atracSampleOffset;
			int playcount = getBufValue((int*)riff, loopInfoAddr + 20);
			if (playcount != 1)
				loopcounts = -1;
			loopInfoAddr += 24;
		}
		return loopcounts;
	}
	return 0;
}

int getRIFFendSample(u8* riff, int bufsize)
{
	const int firstChunkOffset = 12;
	int dataChunkOffset = getChunkOffset(riff, bufsize, DATA_CHUNK_MAGIC, firstChunkOffset);
	if (dataChunkOffset < 0)
		return -1;
	int factChunkOffset = getChunkOffset(riff, dataChunkOffset, FACT_CHUNK_MAGIC, firstChunkOffset);
	if (factChunkOffset >= 0) {
		int factChunkSize = getBufValue((int*)riff, factChunkOffset + 4);
		if (factChunkSize >= 8) {
			int endSample = getBufValue((int*)riff, factChunkOffset + 8);
			return endSample;
		}
	}
	return -1;
}

int getRIFFChannels(u8* riff, int bufsize)
{
	const int firstChunkOffset = 12;
	int fmtChunkOffset = getChunkOffset(riff, bufsize, FMT_CHUNK_MAGIC, firstChunkOffset);
	if (fmtChunkOffset < 0) 
		return 0;
	u16 channel = getBufValue((u16*)riff, fmtChunkOffset + 0x0a);
	return channel;
}

} // namespace OMAConvert
