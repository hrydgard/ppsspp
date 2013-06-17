#ifndef _ATRAC3PLUS_DECODER_
#define _ATRAC3PLUS_DECODER_

namespace Atrac3plus_Decoder {
	bool IsSupported();
	bool IsInstalled();
	bool CanAutoInstall();
	bool DoAutoInstall();
	std::string GetInstalledFilename();

	int Init();
	int Shutdown();

	typedef void* Context;

	Context OpenContext();
	int CloseContext(Context *context);
	bool Decode(Context context, void* inbuf, int inbytes, int *outbytes, void* outbuf);

	struct BufferQueue {
		BufferQueue(int size = 0x20000) {
			bufQueue = 0;
			alloc(size);
		}

		~BufferQueue() {
			if (bufQueue)
				delete [] bufQueue;
		}

		bool alloc(int size) {
			if (size < 0)
				return false;
			if (bufQueue)
				delete [] bufQueue;
			bufQueue = new unsigned char[size];
			start = 0;
			end = 0;
			bufQueueSize = size;
			return true;
		}

		void clear() {
			start = 0;
			end = 0;
		}

		inline int getQueueSize() {
			return (end + bufQueueSize - start) % bufQueueSize;
		}

		bool push(unsigned char *buf, int addsize) {
			int queuesz = getQueueSize();
			int space = bufQueueSize - queuesz;
			if (space < addsize || addsize < 0)
				return false;
			if (end + addsize <= bufQueueSize) {
				memcpy(bufQueue + end, buf, addsize);
			} else {
				int size = bufQueueSize - end;
				memcpy(bufQueue + end, buf, size);
				memcpy(bufQueue, buf + size, addsize - size);
			}
			end = (end + addsize) % bufQueueSize;
			return true;
		}

		int pop_front(unsigned char *buf, int wantedsize) {
			if (wantedsize <= 0)
				return 0;
			int bytesgot = getQueueSize();
			if (wantedsize < bytesgot)
				bytesgot = wantedsize;
			if (start + bytesgot <= bufQueueSize) {
				memcpy(buf, bufQueue + start, bytesgot);
			} else {
				int size = bufQueueSize - start;
				memcpy(buf, bufQueue + start, size);
				memcpy(buf + size, bufQueue, bytesgot - size);
			}
			start = (start + bytesgot) % bufQueueSize;
			return bytesgot;
		}

		unsigned char* bufQueue;
		int start, end;
		int bufQueueSize;
	};
}

#endif // _ATRAC3PLUS_DECODER_
