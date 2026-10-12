// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <cstring>

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Math/SIMDHeaders.h"
#include "Common/StringUtils.h"
#include "Core/System.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/HW/AvcDecoder.h"
#include "Core/HW/MediaEngine.h"
#include "Core/HLE/sceMpegbase.h"
#include "Core/MemMap.h"
#include "Common/LogOnce.h"
#include "GPU/GPUState.h"  // Used by TextureDecoder.h when templates get instanced
#include "GPU/Common/TextureDecoder.h"
#include "Core/HW/SimpleAudioDec.h"

#include <formats/rpsmf.h>

// A frame of a PSP movie, when its time stamp says nothing better.
static const s64 FRAME_PTS_DEFAULT = 3003;
// A whole pack of the program stream, which is what the ring holds.
static const int PS_PACK_BYTES = 2048;

static int getPixelFormatBytes(int pspFormat)
{
	switch (pspFormat)
	{
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		return 2;
	case GE_CMODE_32BIT_ABGR8888:
		return 4;

	default:
		ERROR_LOG(Log::ME, "Unknown pixel format");
		return 4;
	}
}

MediaEngine::MediaEngine() {
	m_mpegheaderSize = sizeof(m_mpegheader);
	m_audioType = PSP_CODEC_AT3PLUS; // in movie, we use only AT3+ audio
}

MediaEngine::~MediaEngine() {
	closeMedia();
}

void MediaEngine::closeMedia() {
	closeContext();
	delete m_pdata;
	delete m_demux;
	m_pdata = nullptr;
	m_demux = nullptr;
	AudioClose(&m_audioContext);
	m_isVideoEnd = false;
}

void MediaEngine::DoState(PointerWrap &p) {
	auto s = p.Section("MediaEngine", 1, 8);
	if (!s)
		return;

	Do(p, m_videoStream);
	Do(p, m_audioStream);

	DoArray(p, m_mpegheader, sizeof(m_mpegheader));
	if (s >= 4) {
		Do(p, m_mpegheaderSize);
	} else {
		m_mpegheaderSize = sizeof(m_mpegheader);
	}
	if (s >= 5) {
		Do(p, m_mpegheaderReadPos);
	} else {
		m_mpegheaderReadPos = m_mpegheaderSize;
	}
	if (s >= 6) {
		Do(p, m_expectedVideoStreams);
	} else {
		m_expectedVideoStreams = 0;
	}

	Do(p, m_ringbuffersize);

	u32 hasloadStream = m_pdata != nullptr;
	Do(p, hasloadStream);
	if (hasloadStream && p.mode == p.MODE_READ)
		reloadStream();
	u32 hasopencontext = m_contextOpen;
	Do(p, hasopencontext);
	if (m_pdata)
		m_pdata->DoState(p);
	if (m_demux)
		m_demux->DoState(p);

	Do(p, m_videopts);
	if (s >= 7) {
		Do(p, m_lastPts);
	} else {
		m_lastPts = m_videopts;
	}
	Do(p, m_audiopts);

	if (s >= 2) {
		Do(p, m_firstTimeStamp);
		Do(p, m_lastTimeStamp);
	}

	if (hasopencontext && p.mode == p.MODE_READ) {
		// The decoder starts again from what the ring holds: the pictures up to the next key
		// frame have nothing to be predicted from, as after any seek.
		openContext(true);
	}

	Do(p, m_isVideoEnd);
	bool noAudioDataRemoved;
	Do(p, noAudioDataRemoved);
	if (s >= 3) {
		Do(p, m_audioType);
	} else {
		m_audioType = PSP_CODEC_AT3PLUS;
	}
	// How much of the stream went to the demuxer, so it still knows where the end is. States
	// from before lose the drain of the last pictures.
	if (s >= 8) {
		Do(p, m_psFed);
	}
}

bool MediaEngine::SetupStreams() {
	for (int i = 0; i < 16; i++)
		m_videoStreamIds[i] = (u8)(PSMF_VIDEO_STREAM_ID | i);

	const u32 magic = *(u32_le *)&m_mpegheader[0];
	if (magic != PSMF_MAGIC) {
		WARN_LOG(Log::ME, "Could not setup streams, bad magic: %08x", magic);
		return false;
	}
	int numStreams = *(u16_be *)&m_mpegheader[0x80];
	if (numStreams <= 0 || numStreams > 8) {
		WARN_LOG(Log::ME, "Could not setup streams, unexpected stream count: %d", numStreams);
		return false;
	}

	// The video streams, in the order the header lists them. We demux the audio separately.
	int videoStreamNum = -1;
	for (int i = 0; i < numStreams; i++) {
		const u8 *const currentStreamAddr = m_mpegheader + 0x82 + i * 16;
		int streamId = currentStreamAddr[0];
		if ((streamId & PSMF_VIDEO_STREAM_ID) == PSMF_VIDEO_STREAM_ID) {
			++videoStreamNum;
			addVideoStream(videoStreamNum, streamId);
		}
	}
	// Add the streams to meet the expectation.
	for (int i = videoStreamNum + 1; i < m_expectedVideoStreams; i++) {
		addVideoStream(i);
	}
	return true;
}

bool MediaEngine::openContext(bool keepReadPos) {
	if (m_contextOpen || !m_pdata)
		return false;
	if (!keepReadPos) {
		m_mpegheaderReadPos = 0;
	}
	m_decodingsize = 0;

	m_psmf = rpsmf_init();
	if (m_avc)
		m_avc->Flush();
	else
		m_avc = new AvcDecoder();
	if (!m_psmf) {
		closeContext();
		return false;
	}
	clearUnits();
	m_psFed = 0;
	m_psSize = 0;
	{
		rpsmf_info_t info;
		if (m_mpegheaderSize >= RPSMF_HEADER_SIZE && rpsmf_parse_header(m_mpegheader, m_mpegheaderSize, &info))
			m_psSize = info.data_size;
	}
	m_contextOpen = true;

	SetupStreams();
	if (m_videoStream < 0 || m_videoStream >= 16)
		m_videoStream = 0;
	setVideoStream(m_videoStream, true);

	m_audioContext = CreateAudioDecoder((PSPAudioType)m_audioType);
	m_isVideoEnd = false;
	return true;
}

void MediaEngine::closeContext() {
	delete[] m_buffer;
	m_buffer = nullptr;
	if (m_psmf)
		rpsmf_free(m_psmf);
	m_psmf = nullptr;
	delete m_avc;
	m_avc = nullptr;
	clearUnits();
	m_contextOpen = false;
}

bool MediaEngine::loadStream(const u8 *buffer, int readSize, int RingbufferSize)
{
	closeMedia();

	m_videopts = 0;
	m_lastPts = -1;
	m_audiopts = 0;
	m_ringbuffersize = RingbufferSize;
	m_pdata = new BufferQueue(RingbufferSize + 2048);
	m_pdata->push(buffer, readSize);
	m_firstTimeStamp = getMpegTimeStamp(buffer + PSMF_FIRST_TIMESTAMP_OFFSET);
	m_lastTimeStamp = getMpegTimeStamp(buffer + PSMF_LAST_TIMESTAMP_OFFSET);
	int mpegoffset = (int)(*(s32_be*)(buffer + 8));
	m_demux = new MpegDemux(RingbufferSize + 2048, mpegoffset);
	m_demux->addStreamData(buffer, readSize);
	return true;
}

bool MediaEngine::reloadStream()
{
	return loadStream(m_mpegheader, 2048, m_ringbuffersize);
}

bool MediaEngine::addVideoStream(int streamNum, int streamId) {
	if (streamNum >= 0 && streamNum < 16 && streamId != -1)
		m_videoStreamIds[streamNum] = (u8)streamId;
	if (streamNum >= m_expectedVideoStreams) {
		++m_expectedVideoStreams;
	}
	return m_contextOpen;
}

int MediaEngine::addStreamData(const u8 *buffer, int addSize) {
	int size = addSize;
	if (size > 0 && m_pdata) {
		if (!m_pdata->push(buffer, size))
			size  = 0;
		if (m_demux) {
			m_demux->addStreamData(buffer, addSize);
		}
		if (!m_contextOpen && m_pdata->getQueueSize() >= 2048) {
			m_mpegheaderSize = m_pdata->get_front(m_mpegheader, sizeof(m_mpegheader));
			int streamOffset = (int)(*(s32_be *)(m_mpegheader + 8));
			if (streamOffset <= m_mpegheaderSize) {
				m_mpegheaderSize = streamOffset;
				m_pdata->pop_front(0, m_mpegheaderSize);
				openContext();
			}
		}

		// We added data, so... not the end anymore?
		m_isVideoEnd = false;
	}
	return size;
}

bool MediaEngine::seekTo(s64 timestamp, int videoPixelMode) {
	if (timestamp <= 0) {
		return true;
	}

	// Just doing it the not so great way to be sure audio is in sync.
	int timeout = 1000;
	while (getVideoTimeStamp() < timestamp - 3003) {
		if (getAudioTimeStamp() < getVideoTimeStamp() - 4180 * 2) {
			getNextAudioFrame(NULL, NULL, NULL);
		}
		if (!stepVideo(videoPixelMode, true)) {
			return false;
		}
		if (--timeout <= 0) {
			return true;
		}
	}

	while (getAudioTimeStamp() < getVideoTimeStamp() - 4180 * 2) {
		if (getNextAudioFrame(NULL, NULL, NULL) == 0) {
			return false;
		}
		if (--timeout <= 0) {
			return true;
		}
	}

	return true;
}

bool MediaEngine::setVideoStream(int streamNum, bool force) {
	if (m_videoStream == streamNum && !force) {
		// Yay, nothing to do.
		return true;
	}
	if (streamNum < 0 || streamNum >= 16)
		return false;

	if (m_contextOpen && m_videoStream != streamNum) {
		// Another stream's pictures predict from nothing this one decoded: start it afresh.
		if (m_avc)
			m_avc->Flush();
		clearUnits();
	}
	m_videoStream = streamNum;
	return true;
}

bool MediaEngine::setAudioStream(int streamNum) {
	if (m_audioStream == streamNum)
		return true;

	m_audioStream = streamNum;
	if (m_demux) {
		m_demux->setAudioChannel(streamNum);
	}
	return true;
}

bool MediaEngine::streamFed() const {
	return m_psSize > 0 && m_psFed >= m_psSize;
}

void MediaEngine::clearUnits() {
	m_au.clear();
	m_auBase = 0;
	m_auScan = 0;
	m_auHasSlice = false;
	m_auPts.clear();
	m_pendingPts.clear();
	m_packEnds.clear();
	m_packPending = false;
}

bool MediaEngine::demuxMore() {
	u8 pack[PS_PACK_BYTES];
	rpsmf_packet_t pkt;
	bool any = false;

	for (;;) {
		while (rpsmf_next(m_psmf, &pkt)) {
			if (pkt.kind != RPSMF_VIDEO || pkt.stream != (m_videoStreamIds[m_videoStream] & 0x0F))
				continue;
			if (pkt.pts != RPSMF_NO_PTS)
				m_auPts.push_back(std::make_pair(m_au.size(), (s64)pkt.pts));
			m_au.insert(m_au.end(), pkt.data, pkt.data + pkt.size);
			any = true;
		}
		if (m_packPending) {
			// Where the last pack's video ends: it is used up once units are taken past that.
			m_packEnds.push_back(m_auBase + m_au.size());
			m_packPending = false;
		}
		if (any)
			return true;
		// The ring a pack at a time, as the hardware reads it.
		const int got = m_pdata->pop_front(pack, PS_PACK_BYTES);
		if (got <= 0)
			return false;
		m_decodingsize = got;
		m_psFed += got;
		m_packPending = true;
		if (rpsmf_write(m_psmf, pack, got) != (size_t)got)
			WARN_LOG(Log::ME, "MediaEngine: demuxer refused program stream bytes");
	}
}

// H.264 network units, gathered into access units by where one must begin (7.4.1.2.3): an
// access unit delimiter, parameter set or SEI after a slice, or a slice starting at the first
// macroblock after another slice.
bool MediaEngine::takeAccessUnit(bool streamEnded, std::vector<u8> &unit, s64 *pts) {
	size_t end = 0;
	bool found = false;
	size_t i = m_auScan;
	while (i + 4 < m_au.size()) {
		if (m_au[i] != 0 || m_au[i + 1] != 0 || m_au[i + 2] != 1) {
			i++;
			continue;
		}
		const int type = m_au[i + 3] & 0x1F;
		const bool slice = type == 1 || type == 5;
		if (m_auHasSlice) {
			bool starts = type == 6 || type == 7 || type == 8 || type == 9 || (type >= 14 && type <= 18);
			// first_mb_in_slice is ue(v): zero is a single 1 bit.
			if (slice && (m_au[i + 4] & 0x80))
				starts = true;
			if (starts) {
				// A four byte start code's leading zero belongs to the next unit.
				end = (i > 0 && m_au[i - 1] == 0) ? i - 1 : i;
				found = true;
				break;
			}
		}
		if (slice)
			m_auHasSlice = true;
		i += 3;
	}
	if (!found) {
		m_auScan = i;
		if (!streamEnded || !m_auHasSlice)
			return false;
		// The last unit runs to the end of the stream.
		end = m_au.size();
	}

	*pts = -1;
	size_t keep = 0;
	for (size_t k = 0; k < m_auPts.size(); k++) {
		if (m_auPts[k].first < end) {
			if (*pts < 0)
				*pts = m_auPts[k].second;
		} else {
			m_auPts[keep++] = std::make_pair(m_auPts[k].first - end, m_auPts[k].second);
		}
	}
	m_auPts.resize(keep);
	unit.assign(m_au.begin(), m_au.begin() + end);
	m_au.erase(m_au.begin(), m_au.begin() + end);
	m_auBase += end;
	size_t used = 0;
	while (used < m_packEnds.size() && m_packEnds[used] <= m_auBase)
		used++;
	m_packEnds.erase(m_packEnds.begin(), m_packEnds.begin() + used);
	m_auScan = 0;
	m_auHasSlice = false;
	return true;
}

void MediaEngine::dropOldestPts() {
	if (!m_pendingPts.empty())
		m_pendingPts.erase(std::min_element(m_pendingPts.begin(), m_pendingPts.end()));
}

void MediaEngine::frameDecoded(int videoPixelMode, bool skipFrame) {
	// Shown in time stamp order: the frame out is the earliest unit not yet shown.
	s64 pts = -1;
	if (!m_pendingPts.empty()) {
		auto it = std::min_element(m_pendingPts.begin(), m_pendingPts.end());
		pts = *it;
		m_pendingPts.erase(it);
	}
	if (pts < 0 || m_lastPts == pts - m_firstTimeStamp) {
		m_videopts += FRAME_PTS_DEFAULT;
	} else {
		m_videopts = pts - m_firstTimeStamp;
		m_lastPts = m_videopts;
	}

	const int width = m_avc->Width(), height = m_avc->Height();
	if (!m_buffer || width != m_desWidth || height != m_desHeight) {
		delete[] m_buffer;
		m_desWidth = width;
		m_desHeight = height;
		m_buffer = new u8[(size_t)width * height * 4]();
	}
	if (!skipFrame) {
		MpegCscPlanes(m_buffer, m_desWidth, videoPixelMode,
			m_avc->Plane(0), m_avc->Stride(0), m_avc->Plane(1), m_avc->Plane(2), m_avc->Stride(1),
			0, 0, width, height);
	}
}

bool MediaEngine::stepVideo(int videoPixelMode, bool skipFrame) {
	if (!m_contextOpen || !m_avc || !m_psmf)
		return false;

	std::vector<u8> unit;
	for (;;) {
		s64 pts;
		const bool ended = streamFed();
		if (takeAccessUnit(ended, unit, &pts)) {
			if (pts >= 0)
				m_pendingPts.push_back(pts);
			if (m_avc->Decode(unit.data(), (int)unit.size())) {
				frameDecoded(videoPixelMode, skipFrame);
				return true;
			}
			// A unit refused never shows, and a frame that could not be used went: their time
			// stamps would otherwise stand in for every frame after them.
			if (m_avc->LastResult() == -1 && pts >= 0)
				m_pendingPts.pop_back();
			else if (m_avc->LastResult() == -2)
				dropOldestPts();
			continue;
		}
		if (demuxMore())
			continue;
		// Nothing more to read. At the end of the stream the frames held for reordering come out.
		while (ended) {
			if (m_avc->Drain()) {
				frameDecoded(videoPixelMode, skipFrame);
				return true;
			}
			if (m_avc->LastResult() != -2)
				break;
			dropOldestPts();
		}
		m_isVideoEnd = m_pdata->getQueueSize() == 0;
		if (m_isVideoEnd)
			m_decodingsize = 0;
		return false;
	}
}

bool MediaEngine::decodeAccessUnit(const u8 *data, int size, int videoPixelMode) {
	if (!m_avc)
		m_avc = new AvcDecoder();
	if (!m_avc->Decode(data, size))
		return false;
	const int width = m_avc->Width(), height = m_avc->Height();
	if (!m_buffer || width != m_desWidth || height != m_desHeight) {
		delete[] m_buffer;
		m_desWidth = width;
		m_desHeight = height;
		m_buffer = new u8[(size_t)width * height * 4]();
	}
	MpegCscPlanes(m_buffer, m_desWidth, videoPixelMode,
		m_avc->Plane(0), m_avc->Stride(0), m_avc->Plane(1), m_avc->Plane(2), m_avc->Stride(1),
		0, 0, width, height);
	return true;
}

// Helpers that null out alpha (which seems to be the case on the PSP.)
// Some games depend on this, for example Sword Art Online (doesn't clear A's from buffer.)
inline void writeVideoLineRGBA(void *destp, const void *srcp, int width) {
	u32_le *dest = (u32_le *)destp;
	const u32_le *src = (u32_le *)srcp;

	int count = width;

#if PPSSPP_ARCH(SSE2)
	__m128i mask = _mm_set1_epi32(0x00FFFFFF);
	while (count >= 8) {
		__m128i pixels1 = _mm_and_si128(_mm_loadu_si128((const __m128i *)src), mask);
		__m128i pixels2 = _mm_and_si128(_mm_loadu_si128((const __m128i *)src + 1), mask);
		_mm_storeu_si128((__m128i *)dest, pixels1);
		_mm_storeu_si128((__m128i *)dest + 1, pixels2);
		src += 8;
		dest += 8;
		count -= 8;
	}
#elif PPSSPP_ARCH(ARM_NEON)
	uint32x4_t mask = vdupq_n_u32(0x00FFFFFF);
	while (count >= 8) {
		uint32x4_t pixels1 = vandq_u32(vld1q_u32(src), mask);
		uint32x4_t pixels2 = vandq_u32(vld1q_u32(src + 4), mask);
		vst1q_u32(dest, pixels1);
		vst1q_u32(dest + 4, pixels2);
		src += 8;
		dest += 8;
		count -= 8;
	}
#endif
	const u32 mask32 = 0x00FFFFFF;
	DO_NOT_VECTORIZE_LOOP
	while (count--) {
		*dest++ = *src++ & mask32;
	}
}

inline void writeVideoLineABGR5650(void *destp, const void *srcp, int width) {
	memcpy(destp, srcp, width * sizeof(u16));
}

inline void writeVideoLineABGR5551(void *destp, const void *srcp, int width) {
	u16_le *dest = (u16_le *)destp;
	const u16_le *src = (u16_le *)srcp;

	const u16 mask = 0x7FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

inline void writeVideoLineABGR4444(void *destp, const void *srcp, int width) {
	u16_le *dest = (u16_le *)destp;
	const u16_le *src = (u16_le *)srcp;

	const u16 mask = 0x0FFF;
	for (int i = 0; i < width; ++i) {
		dest[i] = src[i] & mask;
	}
}

int MediaEngine::writeVideoImage(u32 bufferPtr, int frameWidth, int videoPixelMode) {
	int videoLineSize = 0;
	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		videoLineSize = frameWidth * sizeof(u32);
		break;
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		videoLineSize = frameWidth * sizeof(u16);
		break;
	}

	int videoImageSize = videoLineSize * m_desHeight;

	if (!Memory::IsValidRange(bufferPtr, videoImageSize) || frameWidth > 2048) {
		// Clearly invalid values.  Let's just not.
		ERROR_LOG(Log::ME, "Ignoring invalid video decode address %08x/%x", bufferPtr, frameWidth);
		return 0;
	}

	u8 *buffer = Memory::GetPointerWriteUnchecked(bufferPtr);

	if (!m_buffer)
		return 0;

	// lock the image size
	int height = m_desHeight;
	int width = m_desWidth;
	u8 *imgbuf = buffer;
	const u8 *data = m_buffer;

	bool swizzle = Memory::IsVRAMAddress(bufferPtr) && (bufferPtr & 0x00200000) == 0x00200000;
	if (swizzle) {
		imgbuf = new u8[videoImageSize];
	}

	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u32);
		}
		break;

	case GE_CMODE_16BIT_BGR5650:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u16);
		}
		break;

	case GE_CMODE_16BIT_ABGR5551:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u16);
		}
		break;

	case GE_CMODE_16BIT_ABGR4444:
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf + videoLineSize * y, data, width);
			data += width * sizeof(u16);
		}
		break;

	default:
		ERROR_LOG(Log::ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}

	if (swizzle) {
		const int bxc = videoLineSize / 16;
		int byc = (height + 7) / 8;
		if (byc == 0)
			byc = 1;

		DoSwizzleTex16((const u32 *)imgbuf, buffer, bxc, byc, videoLineSize);
		delete [] imgbuf;
	}

	NotifyMemInfo(MemBlockFlags::WRITE, bufferPtr, videoImageSize, "VideoDecode");

	return videoImageSize;
}

int MediaEngine::writeVideoImageWithRange(u32 bufferPtr, int frameWidth, int videoPixelMode,
	                             int xpos, int ypos, int width, int height) {
	int videoLineSize = 0;
	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		videoLineSize = frameWidth * sizeof(u32);
		break;
	case GE_CMODE_16BIT_BGR5650:
	case GE_CMODE_16BIT_ABGR5551:
	case GE_CMODE_16BIT_ABGR4444:
		videoLineSize = frameWidth * sizeof(u16);
		break;
	}
	int videoImageSize = videoLineSize * height;

	if (!Memory::IsValidRange(bufferPtr, videoImageSize) || frameWidth > 2048) {
		// Clearly invalid values.  Let's just not.
		ERROR_LOG(Log::ME, "Ignoring invalid video decode address %08x/%x", bufferPtr, frameWidth);
		return 0;
	}

	u8 *buffer = Memory::GetPointerWriteUnchecked(bufferPtr);

	if (!m_buffer)
		return 0;

	// lock the image size
	u8 *imgbuf = buffer;
	const u8 *data = m_buffer;

	bool swizzle = Memory::IsVRAMAddress(bufferPtr) && (bufferPtr & 0x00200000) == 0x00200000;
	if (swizzle) {
		imgbuf = new u8[videoImageSize];
	}

	if (width > m_desWidth - xpos)
		width = m_desWidth - xpos;
	if (height > m_desHeight - ypos)
		height = m_desHeight - ypos;

	switch (videoPixelMode) {
	case GE_CMODE_32BIT_ABGR8888:
		data += (ypos * m_desWidth + xpos) * sizeof(u32);
		for (int y = 0; y < height; y++) {
			writeVideoLineRGBA(imgbuf, data, width);
			data += m_desWidth * sizeof(u32);
			imgbuf += videoLineSize;
		}
		break;

	case GE_CMODE_16BIT_BGR5650:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5650(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += videoLineSize;
		}
		break;

	case GE_CMODE_16BIT_ABGR5551:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR5551(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += videoLineSize;
		}
		break;

	case GE_CMODE_16BIT_ABGR4444:
		data += (ypos * m_desWidth + xpos) * sizeof(u16);
		for (int y = 0; y < height; y++) {
			writeVideoLineABGR4444(imgbuf, data, width);
			data += m_desWidth * sizeof(u16);
			imgbuf += videoLineSize;
		}
		break;

	default:
		ERROR_LOG(Log::ME, "Unsupported video pixel format %d", videoPixelMode);
		break;
	}

	if (swizzle) {
		WARN_LOG_ONCE(vidswizzle, Log::ME, "Swizzling Video with range");

		const int bxc = videoLineSize / 16;
		int byc = (height + 7) / 8;
		if (byc == 0)
			byc = 1;

		DoSwizzleTex16((const u32 *)imgbuf, buffer, bxc, byc, videoLineSize);
		delete [] imgbuf;
	}
	NotifyMemInfo(MemBlockFlags::WRITE, bufferPtr, videoImageSize, "VideoDecodeRange");

	return videoImageSize;
}

u8 *MediaEngine::getFrameImage() {
	return m_buffer;
}

int MediaEngine::getRemainSize() {
	if (!m_pdata)
		return 0;
	// Free space as the game sees it: the packs read whose video is not all taken yet are
	// still held, as are two more the decoder reads ahead once it has started (pspautotests
	// video/mpeg/basic).
	const int held = (int)m_packEnds.size() + (int)std::min<s64>(2, m_psFed / PS_PACK_BYTES);
	return std::max(m_pdata->getRemainSize() - held * PS_PACK_BYTES, 0);
}

int MediaEngine::getAudioRemainSize() {
	if (!m_demux) {
		// No audio, so it can't be full, return video instead.
		return getRemainSize();
	}

	return m_demux->getRemainSize();
}

int MediaEngine::getNextAudioFrame(u8 **buf, int *headerCode1, int *headerCode2) {
	// When getting a frame, increment pts
	m_audiopts += 4180;

	// Demux now (rather than on add data) so that we select the right stream.
	m_demux->demux(m_audioStream);

	s64 pts = 0;
	int result = m_demux->getNextAudioFrame(buf, headerCode1, headerCode2, &pts);
	if (pts != 0) {
		// m_audiopts is supposed to be after the returned frame.
		m_audiopts = pts - m_firstTimeStamp + 4180;
	}
	return result;
}

int MediaEngine::getAudioSamples(u32 bufferPtr) {
	int16_t *buffer = (int16_t *)Memory::GetPointerWriteRangeOrException(bufferPtr, 8192);
	if (buffer == nullptr) {
		ERROR_LOG(Log::ME, "Ignoring bad audio decode address %08x during video playback", bufferPtr);
	}
	if (!m_demux) {
		return 0;
	}

	u8 *audioFrame = nullptr;
	int headerCode1, headerCode2;
	int frameSize = getNextAudioFrame(&audioFrame, &headerCode1, &headerCode2);
	if (frameSize == 0) {
		return 0;
	}
	int outSamples = 0;

	if (m_audioContext != nullptr) {
		if (headerCode1 == 0x24) {
			// This means mono audio - tell the decoder to expect it before the first frame.
			// Note that it will always send us back stereo audio.
			m_audioContext->SetChannels(1);
		}

		int inbytesConsumed = 0;
		if (!m_audioContext->Decode(audioFrame, frameSize, &inbytesConsumed, 2, buffer, &outSamples)) {
			ERROR_LOG(Log::ME, "Audio (%s) decode failed during video playback", GetCodecName(m_audioType));
		}
		int outBytes = outSamples * sizeof(int16_t) * 2;

		NotifyMemInfo(MemBlockFlags::WRITE, bufferPtr, outBytes, "VideoDecodeAudio");
	}

	return 0x2000;
}

bool MediaEngine::IsNoAudioData() {
	if (!m_demux) {
		return true;
	}

	// Let's double check.  Here should be a safe enough place to demux.
	m_demux->demux(m_audioStream);
	return !m_demux->hasNextAudioFrame(NULL, NULL, NULL, NULL);
}

bool MediaEngine::IsActuallyPlayingAudio() {
	return getAudioTimeStamp() >= 0;
}

s64 MediaEngine::getVideoTimeStamp() {
	return m_videopts;
}

s64 MediaEngine::getAudioTimeStamp() {
	return m_demux ? m_audiopts - 4180 : -1;
}

s64 MediaEngine::getLastTimeStamp() {
	if (!m_pdata)
		return 0;
	return m_lastTimeStamp - m_firstTimeStamp;
}
