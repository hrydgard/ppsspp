// It can simply convert a at3+ file or stream to oma format
// Thanks to JPCSP project

#pragma once

#include "../../Globals.h"

namespace OMAConvert {

	// output OMA to outputStream, and return it's size. You need to release it by use releaseStream()
	int convertStreamtoOMA(u8* audioStream, int audioSize, u8** outputStream);
	// output OMA to outputStream, and return it's size. You need to release it by use releaseStream()
	int convertRIFFtoOMA(u8* riff, int riffSize, u8** outputStream, int *readSize = 0);

	void releaseStream(u8** stream);

	int getOMANumberAudioChannels(u8* oma);

	int getRIFFSize(u8* riff, int bufsize);

	int getRIFFLoopNum(u8* riff, int bufsize, int *startsample = 0, int *endsample = 0);

	int getRIFFendSample(u8* riff, int bufsize);

	int getRIFFChannels(u8* riff, int bufsize);
} // namespace OMAConvert

