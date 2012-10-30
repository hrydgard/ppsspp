#include "base/basictypes.h"
#include "base/logging.h"
#include "audio/wav_read.h"
#include "file/chunk_file.h"

short *wav_read(const char *filename,
	int *num_samples, int *sample_rate,
	int *num_channels)
{
	ChunkFile cf(filename, true);
	if (cf.failed()) {
		WLOG("ERROR: Wave file %s could not be opened", filename);
		return 0;
	}

	short *data = 0;
	int samplesPerSec, avgBytesPerSec,wBlockAlign,wBytesPerSample;
	if (cf.descend('RIFF')) {
		cf.readInt(); //get past 'WAVE'
		if (cf.descend('fmt ')) { //enter the format chunk
			int temp = cf.readInt();
			int format = temp & 0xFFFF;
			if (format != 1) {
				cf.ascend();
				cf.ascend();
				ELOG("Error - bad format");
				return NULL;
			}
			*num_channels = temp >> 16;
			samplesPerSec	= cf.readInt();
			avgBytesPerSec = cf.readInt();

			temp					 = cf.readInt();
			wBlockAlign		= temp & 0xFFFF;
			wBytesPerSample = temp >> 16;
			cf.ascend();
			// ILOG("got fmt data: %i", samplesPerSec);
		} else {
			ELOG("Error - no format chunk in wav");
			cf.ascend();
			return NULL;
		}

		if (cf.descend('data')) {			//enter the data chunk
			int numBytes = cf.getCurrentChunkSize();
			int numSamples = numBytes / wBlockAlign;
			data = (short *)malloc(sizeof(short) * numSamples * *num_channels);
			*num_samples = numSamples;
			if (wBlockAlign == 2 && *num_channels == 1) {
				cf.readData((uint8*)data,numBytes);
			} else {
				ELOG("Error - bad blockalign or channels");
				free(data);
				return NULL;
			}
			cf.ascend();
		} else {
			ELOG("Error - no data chunk in wav");
			cf.ascend();
			return NULL;
		}
		cf.ascend();
	} else {
		ELOG("Could not descend into RIFF file");
		return NULL;
	}
	*sample_rate = samplesPerSec;
	ILOG("read wav %s, length %i, rate %i", filename, *num_samples, *sample_rate);
	return data;
}
