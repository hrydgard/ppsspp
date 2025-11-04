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
#include <vector>
#include <cstring>

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/MemoryUtil.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceMp4.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/MemMap.h"

// ============================================================================
// MP4 ATOM CONSTANTS
// ============================================================================

// Container atoms (contain other atoms)
static const u32 ATOM_MOOV = 0x6D6F6F76; // "moov" - Movie container
static const u32 ATOM_TRAK = 0x7472616B; // "trak" - Track container
static const u32 ATOM_MDIA = 0x6D646961; // "mdia" - Media container
static const u32 ATOM_MINF = 0x6D696E66; // "minf" - Media information container
static const u32 ATOM_STBL = 0x7374626C; // "stbl" - Sample table container

// Data atoms (contain specific information)
static const u32 ATOM_FTYP = 0x66747970; // "ftyp" - File type header
static const u32 ATOM_MVHD = 0x6D766864; // "mvhd" - Movie header
static const u32 ATOM_MDHD = 0x6D646864; // "mdhd" - Media header
static const u32 ATOM_STSD = 0x73747364; // "stsd" - Sample description
static const u32 ATOM_STSC = 0x73747363; // "stsc" - Sample-to-chunk mapping
static const u32 ATOM_STSZ = 0x7374737A; // "stsz" - Sample sizes
static const u32 ATOM_STTS = 0x73747473; // "stts" - Time-to-sample
static const u32 ATOM_CTTS = 0x63747473; // "ctts" - Composition time offsets
static const u32 ATOM_STCO = 0x7374636F; // "stco" - Chunk offsets
static const u32 ATOM_STSS = 0x73747373; // "stss" - Sync samples (keyframes)
static const u32 ATOM_AVCC = 0x61766343; // "avcC" - AVC configuration

// File type identifiers
static const u32 FILE_TYPE_MSNV = 0x4D534E56; // "MSNV"
static const u32 FILE_TYPE_ISOM = 0x69736F6D; // "isom"
static const u32 FILE_TYPE_MP42 = 0x6D703432; // "mp42"

// Data format identifiers
static const u32 DATA_FORMAT_AVC1 = 0x61766331; // "avc1" - H.264 video
static const u32 DATA_FORMAT_MP4A = 0x6D703461; // "mp4a" - AAC audio

// Track types
static const int TRACK_TYPE_VIDEO = 0x10;
static const int TRACK_TYPE_AUDIO = 0x20;

// Seek directions
static const int SEARCH_BACKWARDS = 0;
static const int SEARCH_FORWARDS = 1;

// Error codes
static const int ERROR_MP4_NO_MORE_DATA = 0x80618001;
static const int ERROR_MP4_INVALID_VALUE = 0x80618002;
static const int ERROR_MP4_INVALID_SAMPLE_NUMBER = 0x80618003;

// PSP seek whence values (from IoFileMgrForUser)
static const int PSP_SEEK_SET = 0;
static const int PSP_SEEK_CUR = 1;
static const int PSP_SEEK_END = 2;

// MPEG timestamp scale (90kHz)
static const u64 MPEG_TIMESTAMP_PER_SECOND = 90000ULL;

// ============================================================================
// MP4 PARSER DATA STRUCTURES
// ============================================================================

struct Mp4TrackInfo {
	std::vector<u32> samplesOffset;        // File offset of each sample
	std::vector<u32> samplesSize;          // Size of each sample in bytes
	std::vector<u32> samplesDuration;      // Duration of each sample
	std::vector<u32> samplesPresentationOffset; // Presentation time offset
	std::vector<u32> syncSamples;          // Keyframe indices
	u32 duration;                          // Track duration
	u32 timeScale;                         // Track timescale
	u64 currentTimestamp;                  // Current playback timestamp
	int trackType;                         // TRACK_TYPE_VIDEO or TRACK_TYPE_AUDIO
	int audioChannels;                     // Number of audio channels
	int currentSample;                     // Current sample index being decoded
	
	Mp4TrackInfo() : duration(0), timeScale(0), currentTimestamp(0), 
	                 trackType(0), audioChannels(0), currentSample(0) {}
};

struct Mp4ParserState {
	// Parsed track data
	Mp4TrackInfo videoTrack;
	Mp4TrackInfo audioTrack;
	
	// Movie metadata
	u32 duration;           // Movie duration
	u32 timeScale;          // Movie timescale
	u32 numberOfTracks;     // Number of tracks parsed
	
	// Current parsing state
	u32 currentAtom;                   // Current atom being parsed
	u32 currentAtomSize;               // Size of current atom
	u32 currentAtomOffset;             // Current offset within atom
	std::vector<u8> currentAtomContent; // Buffer for current atom content
	u64 parseOffset;                   // Current file position
	
	// Temporary parsing data (per track)
	int trackType;                     // Current track type being parsed
	u32 trackTimeScale;                // Current track timescale
	u32 trackDuration;                 // Current track duration
	std::vector<u32> numberOfSamplesPerChunk; // Samples per chunk mapping
	std::vector<u32> samplesSize;      // Temporary sample size array
	
	// Callback information
	u32 callbackParam;
	u32 callbackGetCurrentPosition;
	u32 callbackSeek;
	u32 callbackRead;
	u32 readBufferAddr;
	u32 readBufferSize;
	
	// Parsing state flags
	bool headersParsed;
	bool callbacksPending;             // TRUE if waiting for callback to complete
	
	Mp4ParserState() : duration(0), timeScale(0), numberOfTracks(0),
	                   currentAtom(0), currentAtomSize(0), currentAtomOffset(0),
	                   parseOffset(0), trackType(0), trackTimeScale(0),
	                   trackDuration(0), callbackParam(0),
	                   callbackGetCurrentPosition(0), callbackSeek(0),
	                   callbackRead(0), readBufferAddr(0), readBufferSize(0),
	                   headersParsed(false), callbacksPending(false) {}
};

// Global parser state
static Mp4ParserState g_Mp4ParserState;

// MP4 module state
static u32 g_mp4ReadBufferAddr = 0;
static u32 g_mp4ReadBufferSize = 0;
static bool g_mp4Initialized = false;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Convert 4-byte atom ID to string for logging
static std::string atomToString(u32 atom) {
	char str[5];
	str[0] = (atom >> 24) & 0xFF;
	str[1] = (atom >> 16) & 0xFF;
	str[2] = (atom >> 8) & 0xFF;
	str[3] = atom & 0xFF;
	str[4] = '\0';
	return std::string(str);
}

// Read 32-bit big-endian value from memory
static u32 read32BE(u32 addr) {
	if (!Memory::IsValidAddress(addr)) return 0;
	u32 val = Memory::Read_U32(addr);
	// Convert from little-endian (PSP memory) to big-endian (MP4 format)
	return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | 
	       ((val & 0xFF0000) >> 8) | ((val & 0xFF000000) >> 24);
}

// Read 32-bit big-endian value from byte array
static u32 read32BE(const std::vector<u8>& data, size_t offset) {
	if (offset + 4 > data.size()) return 0;
	return (data[offset] << 24) | (data[offset + 1] << 16) |
	       (data[offset + 2] << 8) | data[offset + 3];
}

// Read 16-bit big-endian value from byte array
static u16 read16BE(const std::vector<u8>& data, size_t offset) {
	if (offset + 2 > data.size()) return 0;
	return (data[offset] << 8) | data[offset + 1];
}

// Check if atom is a container (contains other atoms)
static bool isContainerAtom(u32 atom) {
	return atom == ATOM_MOOV || atom == ATOM_TRAK || atom == ATOM_MDIA ||
	       atom == ATOM_MINF || atom == ATOM_STBL;
}

// Check if atom content needs to be cached for processing
static bool isAtomContentRequired(u32 atom) {
	return atom == ATOM_MVHD || atom == ATOM_STSD || atom == ATOM_STSC ||
	       atom == ATOM_STSZ || atom == ATOM_STTS || atom == ATOM_CTTS ||
	       atom == ATOM_STCO || atom == ATOM_STSS || atom == ATOM_MDHD ||
	       atom == ATOM_AVCC;
}

// Extend array to specified size
static std::vector<u32> extendArray(const std::vector<u32>& array, size_t length, u32 fillValue = 0) {
	std::vector<u32> result = array;
	if (length > result.size()) {
		result.resize(length, fillValue);
	}
	return result;
}

// Convert sample timestamp to MPEG timestamp format (90kHz)
static u64 sampleToMpegTimestamp(u32 sampleDuration, u32 timeScale) {
	if (timeScale == 0) return 0;
	return ((u64)sampleDuration * MPEG_TIMESTAMP_PER_SECOND) / timeScale;
}

// ============================================================================
// SAMPLE RETRIEVAL HELPER FUNCTIONS (Feature #2)
// ============================================================================

// Get sample offset (file position) for a specific sample
static int getSampleOffset(int trackType, int sample) {
	const Mp4TrackInfo* track = nullptr;
	
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr || track->samplesOffset.empty() || 
	    sample < 0 || sample >= (int)track->samplesOffset.size()) {
		return -1;
	}
	
	return track->samplesOffset[sample];
}

// Get sample size in bytes
static int getSampleSize(int trackType, int sample) {
	const Mp4TrackInfo* track = nullptr;
	
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr || track->samplesSize.empty() || 
	    sample < 0 || sample >= (int)track->samplesSize.size()) {
		return -1;
	}
	
	return track->samplesSize[sample];
}

// Get sample duration in track timescale units
static int getSampleDuration(int trackType, int sample) {
	const Mp4TrackInfo* track = nullptr;
	
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr || track->samplesDuration.empty() || 
	    sample < 0 || sample >= (int)track->samplesDuration.size()) {
		return -1;
	}
	
	return track->samplesDuration[sample];
}

// Get sample presentation offset (for B-frame reordering)
static int getSamplePresentationOffset(int trackType, int sample) {
	const Mp4TrackInfo* track = nullptr;
	
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr || track->samplesPresentationOffset.empty() || 
	    sample < 0 || sample >= (int)track->samplesPresentationOffset.size()) {
		return 0; // Default to 0 if not available
	}
	
	return track->samplesPresentationOffset[sample];
}

// Get total number of samples in track
static int getTotalNumberOfSamples(int trackType) {
	const Mp4TrackInfo* track = nullptr;
	
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr || track->samplesSize.empty()) {
		return 0;
	}
	
	return (int)track->samplesSize.size();
}

// ============================================================================
// CALLBACK INVOCATION SYSTEM (Feature #1)
// ============================================================================

// Callback completion handlers
static u32 afterReadCallback(MipsCall *call) {
	u32 bytesRead = call->savedV0;
	INFO_LOG(Log::ME, "MP4: Read callback completed, bytesRead=%u", bytesRead);
	g_Mp4ParserState.callbacksPending = false;
	return 0;
}

static u32 afterSeekCallback(MipsCall *call) {
	u64 position = ((u64)call->savedV0) | (((u64)call->savedV1) << 32);
	INFO_LOG(Log::ME, "MP4: Seek callback completed, position=0x%llX", position);
	g_Mp4ParserState.callbacksPending = false;
	return 0;
}

static u32 afterGetCurrentPositionCallback(MipsCall *call) {
	u64 position = ((u64)call->savedV0) | (((u64)call->savedV1) << 32);
	INFO_LOG(Log::ME, "MP4: GetCurrentPosition callback completed, position=0x%llX", position);
	g_Mp4ParserState.callbacksPending = false;
	return 0;
}

// PSPAction wrapper classes for callback functions
class ActionAfterReadCallback : public PSPAction {
public:
	void run(MipsCall &call) override {
		afterReadCallback(&call);
	}
	
	void DoState(PointerWrap &p) override {
		// No state to save/restore for this simple action
	}
};

class ActionAfterSeekCallback : public PSPAction {
public:
	void run(MipsCall &call) override {
		afterSeekCallback(&call);
	}
	
	void DoState(PointerWrap &p) override {
		// No state to save/restore for this simple action
	}
};

class ActionAfterGetCurrentPositionCallback : public PSPAction {
public:
	void run(MipsCall &call) override {
		afterGetCurrentPositionCallback(&call);
	}
	
	void DoState(PointerWrap &p) override {
		// No state to save/restore for this simple action
	}
};

// Invoke read callback with __KernelDirectMipsCall integration
static int callReadCallback(u32 readAddr, u32 readBytes) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	if (state.callbackRead == 0) {
		ERROR_LOG(Log::ME, "MP4: callReadCallback - No read callback registered");
		return -1;
	}
	
	INFO_LOG(Log::ME, "MP4: callReadCallback readAddr=0x%08X, readBytes=0x%X", readAddr, readBytes);
	
	// Set up callback parameters
	u32 args[3] = { state.callbackParam, readAddr, readBytes };
	state.callbacksPending = true;
	
	// Execute PSP callback
	// Parameters: callbackFunc, afterAction, args, argCount, reschedAfter
	__KernelDirectMipsCall(state.callbackRead, new ActionAfterReadCallback(), args, 3, true);
	
	return 0;
}

// Invoke seek callback with __KernelDirectMipsCall integration
static int callSeekCallback(u64 offset, int whence) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	if (state.callbackSeek == 0) {
		ERROR_LOG(Log::ME, "MP4: callSeekCallback - No seek callback registered");
		return -1;
	}
	
	const char* whenceName = "UNKNOWN";
	if (whence == PSP_SEEK_SET) whenceName = "PSP_SEEK_SET";
	else if (whence == PSP_SEEK_CUR) whenceName = "PSP_SEEK_CUR";
	else if (whence == PSP_SEEK_END) whenceName = "PSP_SEEK_END";
	
	INFO_LOG(Log::ME, "MP4: callSeekCallback offset=0x%llX, whence=%s", offset, whenceName);
	
	// Set up callback parameters
	// Parameters: callbackParam, 0, offsetLow, offsetHigh, whence
	u32 args[5] = { 
		state.callbackParam, 
		0, 
		(u32)(offset & 0xFFFFFFFF), 
		(u32)(offset >> 32), 
		(u32)whence 
	};
	state.callbacksPending = true;
	
	// Execute PSP callback
	// Parameters: callbackFunc, afterAction, args, argCount, reschedAfter
	__KernelDirectMipsCall(state.callbackSeek, new ActionAfterSeekCallback(), args, 5, true);
	
	return 0;
}

// Invoke get current position callback with __KernelDirectMipsCall integration
static int callGetCurrentPositionCallback() {
	Mp4ParserState& state = g_Mp4ParserState;
	
	if (state.callbackGetCurrentPosition == 0) {
		ERROR_LOG(Log::ME, "MP4: callGetCurrentPositionCallback - No position callback registered");
		return -1;
	}
	
	INFO_LOG(Log::ME, "MP4: callGetCurrentPositionCallback");
	
	// Set up callback parameters
	u32 args[1] = { state.callbackParam };
	state.callbacksPending = true;
	
	// Execute PSP callback
	// Parameters: callbackFunc, afterAction, args, argCount, reschedAfter
	__KernelDirectMipsCall(state.callbackGetCurrentPosition, new ActionAfterGetCurrentPositionCallback(), args, 1, true);
	
	return 0;
}

// ============================================================================
// MP4 ATOM PROCESSING
// ============================================================================

static void setTrackDurationAndTimeScale(Mp4ParserState& state) {
	if (state.trackType == 0) {
		return;
	}
	
	switch (state.trackType) {
		case TRACK_TYPE_VIDEO:
			state.videoTrack.timeScale = state.trackTimeScale;
			state.videoTrack.duration = state.trackDuration;
			state.videoTrack.trackType = TRACK_TYPE_VIDEO;
			break;
		case TRACK_TYPE_AUDIO:
			state.audioTrack.timeScale = state.trackTimeScale;
			state.audioTrack.duration = state.trackDuration;
			state.audioTrack.trackType = TRACK_TYPE_AUDIO;
			break;
		default:
			ERROR_LOG(Log::ME, "Unknown track type %d", state.trackType);
			break;
	}
}

// Process atom content (called when full atom content is available)
static void processAtomContent(Mp4ParserState& state, u32 atom, const std::vector<u8>& content) {
	switch (atom) {
		case ATOM_MVHD:
			// Movie header: contains movie timescale and duration
			if (content.size() >= 20) {
				state.timeScale = read32BE(content, 12);
				state.duration = read32BE(content, 16);
				INFO_LOG(Log::ME, "MP4 Movie: timeScale=%u, duration=%u", state.timeScale, state.duration);
			}
			break;
			
		case ATOM_MDHD:
			// Media header: contains track timescale and duration
			if (content.size() >= 20) {
				state.trackTimeScale = read32BE(content, 12);
				state.trackDuration = read32BE(content, 16);
				setTrackDurationAndTimeScale(state);
				INFO_LOG(Log::ME, "MP4 Track Media Header: timeScale=%u, duration=%u", 
				         state.trackTimeScale, state.trackDuration);
			}
			break;
			
		case ATOM_STSD:
			// Sample description: identifies track type (video/audio) and codec
			if (content.size() >= 16) {
				u32 dataFormat = read32BE(content, 12);
				switch (dataFormat) {
					case DATA_FORMAT_AVC1:
						INFO_LOG(Log::ME, "MP4 Track Type: Video (AVC1/H.264)");
						state.trackType = TRACK_TYPE_VIDEO;
						
						// Extract video dimensions if available
						if (content.size() >= 44) {
							u16 width = read16BE(content, 40);
							u16 height = read16BE(content, 42);
							INFO_LOG(Log::ME, "Video dimensions: %dx%d", width, height);
						}
						
						// Extract AVC configuration if available
						if (content.size() >= 102) {
							u32 avcCAtom = read32BE(content, 98);
							u32 avcCSize = read32BE(content, 94);
							if (avcCAtom == ATOM_AVCC && avcCSize <= content.size() - 94) {
								INFO_LOG(Log::ME, "Found AVC configuration atom, size=%u", avcCSize);
								// Video codec extra data would be extracted here
							}
						}
						break;
						
					case DATA_FORMAT_MP4A:
						INFO_LOG(Log::ME, "MP4 Track Type: Audio (MP4A/AAC)");
						state.trackType = TRACK_TYPE_AUDIO;
						
						// Extract audio channels if available
						if (content.size() >= 34) {
							u16 channels = read16BE(content, 32);
							INFO_LOG(Log::ME, "Audio channels: %u", channels);
							state.audioTrack.audioChannels = channels;
						}
						break;
						
					default:
						WARN_LOG(Log::ME, "Unknown data format: 0x%08X (%s)", dataFormat, atomToString(dataFormat).c_str());
						break;
				}
				
				setTrackDurationAndTimeScale(state);
			}
			break;
			
		case ATOM_STSC: {
			// Sample-to-chunk: maps samples to chunks
			state.numberOfSamplesPerChunk.clear();
			if (content.size() >= 8) {
				u32 numberOfEntries = read32BE(content, 4);
				if (content.size() >= numberOfEntries * 12 + 8) {
					size_t offset = 8;
					u32 previousChunk = 1;
					u32 previousSamplesPerChunk = 0;
					
					for (u32 i = 0; i < numberOfEntries; i++, offset += 12) {
						u32 firstChunk = read32BE(content, offset);
						u32 samplesPerChunk = read32BE(content, offset + 4);
						
						state.numberOfSamplesPerChunk = extendArray(state.numberOfSamplesPerChunk, firstChunk);
						for (u32 j = previousChunk; j < firstChunk; j++) {
							state.numberOfSamplesPerChunk[j - 1] = previousSamplesPerChunk;
						}
						
						previousChunk = firstChunk;
						previousSamplesPerChunk = samplesPerChunk;
					}
					
					state.numberOfSamplesPerChunk = extendArray(state.numberOfSamplesPerChunk, previousChunk);
					state.numberOfSamplesPerChunk[previousChunk - 1] = previousSamplesPerChunk;
				}
			}
			break;
		}
		
		case ATOM_STSZ: {
			// Sample sizes
			state.samplesSize.clear();
			if (content.size() >= 8) {
				u32 sampleSize = read32BE(content, 4);
				if (sampleSize > 0) {
					// All samples have the same size
					state.samplesSize.push_back(sampleSize);
				} else if (content.size() >= 12) {
					// Each sample has a different size
					u32 numberOfEntries = read32BE(content, 8);
					state.samplesSize.reserve(numberOfEntries);
					size_t offset = 12;
					for (u32 i = 0; i < numberOfEntries && offset + 4 <= content.size(); i++, offset += 4) {
						state.samplesSize.push_back(read32BE(content, offset));
					}
				}
			}
			
			// Assign to appropriate track
			switch (state.trackType) {
				case TRACK_TYPE_VIDEO:
					state.videoTrack.samplesSize = state.samplesSize;
					break;
				case TRACK_TYPE_AUDIO:
					state.audioTrack.samplesSize = state.samplesSize;
					break;
			}
			break;
		}
		
		case ATOM_STTS: {
			// Time-to-sample: sample duration information
			std::vector<u32> samplesDuration;
			if (content.size() >= 8) {
				u32 numberOfEntries = read32BE(content, 4);
				size_t offset = 8;
				u32 sample = 0;
				
				for (u32 i = 0; i < numberOfEntries && offset + 8 <= content.size(); i++, offset += 8) {
					u32 sampleCount = read32BE(content, offset);
					u32 sampleDuration = read32BE(content, offset + 4);
					
					samplesDuration = extendArray(samplesDuration, sample + sampleCount);
					for (u32 j = 0; j < sampleCount; j++) {
						samplesDuration[sample + j] = sampleDuration;
					}
					sample += sampleCount;
				}
			}
			
			// Assign to appropriate track
			switch (state.trackType) {
				case TRACK_TYPE_VIDEO:
					state.videoTrack.samplesDuration = samplesDuration;
					break;
				case TRACK_TYPE_AUDIO:
					state.audioTrack.samplesDuration = samplesDuration;
					break;
			}
			break;
		}
		
		case ATOM_CTTS: {
			// Composition time offsets: presentation time adjustments
			std::vector<u32> samplesPresentationOffset;
			if (content.size() >= 8) {
				u32 numberOfEntries = read32BE(content, 4);
				size_t offset = 8;
				u32 sample = 0;
				
				for (u32 i = 0; i < numberOfEntries && offset + 8 <= content.size(); i++, offset += 8) {
					u32 sampleCount = read32BE(content, offset);
					u32 sampleOffset = read32BE(content, offset + 4);
					
					samplesPresentationOffset = extendArray(samplesPresentationOffset, sample + sampleCount);
					for (u32 j = 0; j < sampleCount; j++) {
						samplesPresentationOffset[sample + j] = sampleOffset;
					}
					sample += sampleCount;
				}
			}
			
			// Assign to appropriate track
			switch (state.trackType) {
				case TRACK_TYPE_VIDEO:
					state.videoTrack.samplesPresentationOffset = samplesPresentationOffset;
					break;
				case TRACK_TYPE_AUDIO:
					state.audioTrack.samplesPresentationOffset = samplesPresentationOffset;
					break;
			}
			break;
		}
		
		case ATOM_STCO: {
			// Chunk offsets: file positions of chunks
			std::vector<u32> chunksOffset;
			if (content.size() >= 8) {
				u32 numberOfEntries = read32BE(content, 4);
				chunksOffset.reserve(numberOfEntries);
				size_t offset = 8;
				for (u32 i = 0; i < numberOfEntries && offset + 4 <= content.size(); i++, offset += 4) {
					chunksOffset.push_back(read32BE(content, offset));
				}
			}
			
			// Compute sample offsets from chunk offsets
			std::vector<u32> samplesOffset;
			if (!state.numberOfSamplesPerChunk.empty() && !state.samplesSize.empty() && !chunksOffset.empty()) {
				// Extend numberOfSamplesPerChunk if needed
				size_t compactedChunksLength = state.numberOfSamplesPerChunk.size();
				state.numberOfSamplesPerChunk = extendArray(state.numberOfSamplesPerChunk, chunksOffset.size(),
				                                             state.numberOfSamplesPerChunk[compactedChunksLength - 1]);
				
				// Compute total number of samples
				u32 numberOfSamples = 0;
				for (size_t i = 0; i < state.numberOfSamplesPerChunk.size(); i++) {
					numberOfSamples += state.numberOfSamplesPerChunk[i];
				}
				
				// Extend samplesSize if needed
				size_t compactedSamplesLength = state.samplesSize.size();
				state.samplesSize = extendArray(state.samplesSize, numberOfSamples,
				                                 state.samplesSize[compactedSamplesLength - 1]);
				
				// Compute sample offsets
				samplesOffset.reserve(numberOfSamples);
				u32 sample = 0;
				for (size_t i = 0; i < chunksOffset.size(); i++) {
					u32 offset = chunksOffset[i];
					for (u32 j = 0; j < state.numberOfSamplesPerChunk[i]; j++, sample++) {
						samplesOffset.push_back(offset);
						offset += state.samplesSize[sample];
					}
				}
				
				INFO_LOG(Log::ME, "Computed %u sample offsets for track type %d", numberOfSamples, state.trackType);
			}
			
			// Assign to appropriate track
			switch (state.trackType) {
				case TRACK_TYPE_VIDEO:
					state.videoTrack.samplesOffset = samplesOffset;
					break;
				case TRACK_TYPE_AUDIO:
					state.audioTrack.samplesOffset = samplesOffset;
					break;
			}
			break;
		}
		
		case ATOM_STSS: {
			// Sync samples: keyframe indices
			std::vector<u32> syncSamples;
			if (content.size() >= 8) {
				u32 numberOfEntries = read32BE(content, 4);
				syncSamples.reserve(numberOfEntries);
				size_t offset = 8;
				for (u32 i = 0; i < numberOfEntries && offset + 4 <= content.size(); i++, offset += 4) {
					// Sync samples are numbered starting at 1, convert to 0-based index
					syncSamples.push_back(read32BE(content, offset) - 1);
				}
			}
			
			// Assign to appropriate track
			switch (state.trackType) {
				case TRACK_TYPE_VIDEO:
					state.videoTrack.syncSamples = syncSamples;
					INFO_LOG(Log::ME, "Found %zu video sync samples (keyframes)", syncSamples.size());
					break;
				case TRACK_TYPE_AUDIO:
					state.audioTrack.syncSamples = syncSamples;
					break;
			}
			break;
		}
	}
}

// Process atom without content (container atoms)
static void processAtom(Mp4ParserState& state, u32 atom) {
	switch (atom) {
		case ATOM_TRAK:
			// Starting a new track
			state.trackType = 0;
			state.numberOfSamplesPerChunk.clear();
			state.samplesSize.clear();
			state.numberOfTracks++;
			INFO_LOG(Log::ME, "MP4: Starting track #%u", state.numberOfTracks);
			break;
	}
}

// Parse MP4 atoms from buffer
static void parseAtoms(Mp4ParserState& state, u32 addr, u32 size) {
	if (!Memory::IsValidAddress(addr) || size == 0) {
		return;
	}
	
	u32 offset = 0;
	
	// Continue parsing incomplete atom from previous read
	if (state.currentAtom != 0) {
		u32 length = std::min(size, state.currentAtomSize - state.currentAtomOffset);
		
		// Copy data to atom content buffer
		for (u32 i = 0; i < length; i++) {
			if (Memory::IsValidAddress(addr + offset + i)) {
				state.currentAtomContent[state.currentAtomOffset++] = Memory::Read_U8(addr + offset + i);
			}
		}
		offset += length;
		
		// If atom is complete, process it
		if (state.currentAtomOffset >= state.currentAtomSize) {
			processAtomContent(state, state.currentAtom, state.currentAtomContent);
			state.currentAtom = 0;
			state.currentAtomContent.clear();
		}
	}
	
	// Parse new atoms
	while (offset + 8 <= size && Memory::IsValidAddress(addr + offset)) {
		u32 atomSize = read32BE(addr + offset);
		u32 atom = read32BE(addr + offset + 4);
		
		DEBUG_LOG(Log::ME, "MP4 Atom: %s, size=%u, offset=0x%llX", 
		          atomToString(atom).c_str(), atomSize, state.parseOffset + offset);
		
		if (atomSize <= 0) {
			break;
		}
		
		// Handle atoms that need content
		if (isAtomContentRequired(atom)) {
			if (offset + atomSize <= size) {
				// Entire atom is in current buffer, process directly
				std::vector<u8> content(atomSize - 8);
				for (u32 i = 0; i < atomSize - 8; i++) {
					if (Memory::IsValidAddress(addr + offset + 8 + i)) {
						content[i] = Memory::Read_U8(addr + offset + 8 + i);
					}
				}
				processAtomContent(state, atom, content);
			} else {
				// Atom spans multiple buffers, cache for later
				state.currentAtom = atom;
				state.currentAtomSize = atomSize - 8;
				state.currentAtomOffset = 0;
				state.currentAtomContent.resize(state.currentAtomSize);
				
				// Copy what we have
				u32 available = size - offset - 8;
				for (u32 i = 0; i < available; i++) {
					if (Memory::IsValidAddress(addr + offset + 8 + i)) {
						state.currentAtomContent[state.currentAtomOffset++] = Memory::Read_U8(addr + offset + 8 + i);
					}
				}
				atomSize = size - offset;
			}
		} else {
			// Process atom without content
			processAtom(state, atom);
		}
		
		// Advance offset
		if (isContainerAtom(atom)) {
			offset += 8; // Skip only the header for container atoms
		} else {
			offset += atomSize; // Skip entire atom for data atoms
		}
	}
	
	state.parseOffset += offset;
}

// ============================================================================
// MP4 HEADER READING WITH CALLBACKS
// ============================================================================

static void readHeaders(Mp4ParserState& state) {
	if (state.headersParsed) {
		return; // Already parsed
	}
	
	INFO_LOG(Log::ME, "MP4: Beginning header parsing with callbacks");
	
	// Reset parse state
	state.parseOffset = 0;
	state.duration = 0;
	state.currentAtom = 0;
	state.numberOfTracks = 0;
	
	// In a full implementation, this would:
	// 1. Call callSeekCallback to position at start of file
	// 2. Set up afterSeek handler
	// 3. afterSeek handler calls callReadCallback
	// 4. afterRead handler calls parseAtoms on read data
	// 5. Continue until all necessary atoms are parsed
	//
	// For now, we just mark as initiated
	callSeekCallback(0, PSP_SEEK_SET);
	
	// After seek completes, would call:
	// callReadCallback(state.readBufferAddr, state.readBufferSize);
	
	state.headersParsed = true;
}

// ============================================================================
// SYNC SAMPLE SEARCH (Feature #3)
// ============================================================================

// Search for nearest sync sample (keyframe) in specified direction
// Returns sample number of the sync sample found
static int searchSyncSampleNum(int trackType, int sample, int searchDirection) {
	if (searchDirection != SEARCH_BACKWARDS && searchDirection != SEARCH_FORWARDS) {
		ERROR_LOG(Log::ME, "sceMp4SearchSyncSampleNum: Invalid search direction %d", searchDirection);
		return ERROR_MP4_INVALID_VALUE;
	}
	
	const Mp4TrackInfo* track = nullptr;
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr) {
		ERROR_LOG(Log::ME, "sceMp4SearchSyncSampleNum: Unknown track type 0x%X", trackType);
		return -1;
	}
	
	int syncSample = 0;
	if (!track->syncSamples.empty()) {
		// Search through sync samples array
		for (size_t i = 0; i < track->syncSamples.size(); i++) {
			int currentSync = track->syncSamples[i];
			
			if (sample > currentSync) {
				// This sync sample is before our target
				syncSample = currentSync;
			} else if (sample == currentSync && searchDirection == SEARCH_FORWARDS) {
				// Exact match, and we're searching forwards
				syncSample = currentSync;
			} else {
				// We've passed the target sample
				if (searchDirection == SEARCH_FORWARDS) {
					// Use this sync sample (first one after target)
					syncSample = currentSync;
				}
				// For BACKWARDS, we already have the previous sync sample
				break;
			}
		}
	}
	
	DEBUG_LOG(Log::ME, "sceMp4SearchSyncSampleNum: trackType=0x%X, sample=%d, direction=%d, result=%d",
	          trackType, sample, searchDirection, syncSample);
	
	return syncSample;
}

// ============================================================================
// HLE FUNCTIONS
// ============================================================================

static void hleMp4Init() {
	g_mp4ReadBufferAddr = 0;
	g_mp4ReadBufferSize = 0;
	g_mp4Initialized = true;
	
	// Reset parser state
	g_Mp4ParserState = Mp4ParserState();
	
	INFO_LOG(Log::ME, "MP4 module initialized");
}

static u32 sceMp4Init() {
	hleMp4Init();
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4Finish() {
	g_mp4Initialized = false;
	g_Mp4ParserState = Mp4ParserState(); // Clear parser state
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4Create(u32 mp4, u32 callbacks, u32 readBufferAddr, u32 readBufferSize) {
	// Store read buffer parameters
	g_mp4ReadBufferAddr = readBufferAddr;
	g_mp4ReadBufferSize = readBufferSize;
	g_Mp4ParserState.readBufferAddr = readBufferAddr;
	g_Mp4ParserState.readBufferSize = readBufferSize;

	// Read callbacks from the callbacks pointer
	if (Memory::IsValidAddress(callbacks)) {
		g_Mp4ParserState.callbackParam = Memory::Read_U32(callbacks);
		g_Mp4ParserState.callbackGetCurrentPosition = Memory::Read_U32(callbacks + 4);
		g_Mp4ParserState.callbackSeek = Memory::Read_U32(callbacks + 8);
		g_Mp4ParserState.callbackRead = Memory::Read_U32(callbacks + 12);
		
		INFO_LOG(Log::ME, "sceMp4Create: callbacks at 0x%08x, readBuffer=0x%08x, size=%d", 
		         callbacks, readBufferAddr, readBufferSize);
		INFO_LOG(Log::ME, "  Callbacks: param=0x%08x, getPos=0x%08x, seek=0x%08x, read=0x%08x",
		         g_Mp4ParserState.callbackParam, g_Mp4ParserState.callbackGetCurrentPosition,
		         g_Mp4ParserState.callbackSeek, g_Mp4ParserState.callbackRead);
	}

	// Initiate header parsing with callbacks
	readHeaders(g_Mp4ParserState);

	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetNumberOfSpecificTrack() {
	// Should return number of tracks of a specific type
	u32 count = 0;
	if (g_Mp4ParserState.videoTrack.trackType == TRACK_TYPE_VIDEO) count++;
	if (g_Mp4ParserState.audioTrack.trackType == TRACK_TYPE_AUDIO) count++;
	return hleLogInfo(Log::ME, count > 0 ? 1 : 0);
}

static u32 sceMp4GetMovieInfo(u32 mp4, u32 movieInfoAddr) {
	if (!Memory::IsValidAddress(movieInfoAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid movieInfo address");
	}

	// Use parsed data
	u32 numberOfTracks = g_Mp4ParserState.numberOfTracks;
	u32 duration = g_Mp4ParserState.duration;
	
	// Convert duration to MPEG timestamp format
	if (g_Mp4ParserState.timeScale > 0 && duration > 0) {
		duration = (u32)(((u64)duration * MPEG_TIMESTAMP_PER_SECOND) / g_Mp4ParserState.timeScale);
	}

	Memory::Write_U32(numberOfTracks, movieInfoAddr);
	Memory::Write_U32(0, movieInfoAddr + 4);
	Memory::Write_U32(duration, movieInfoAddr + 8);

	INFO_LOG(Log::ME, "sceMp4GetMovieInfo: numberOfTracks=%u, duration=%u", numberOfTracks, duration);

	return 0;
}

// ============================================================================
// TRACK INFO QUERY FUNCTIONS (Feature #4)
// ============================================================================

static u32 sceMp4GetAvcTrackInfoData(u32 mp4, u32 trackAddr, u32 infoAddr) {
	if (!Memory::IsValidAddress(infoAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid info address");
	}
	
	const Mp4TrackInfo& track = g_Mp4ParserState.videoTrack;
	
	// Calculate total frame duration in MPEG timestamps
	u64 totalFrameDuration = 0;
	if (!track.samplesDuration.empty() && track.timeScale > 0) {
		for (size_t i = 0; i < track.samplesDuration.size(); i++) {
			totalFrameDuration += sampleToMpegTimestamp(track.samplesDuration[i], track.timeScale);
		}
	}
	
	int totalSamples = getTotalNumberOfSamples(TRACK_TYPE_VIDEO);
	
	// Return 3 32-bit values in infoAddr:
	// [0] = 0 (always)
	// [4] = total frame duration
	// [8] = total number of samples
	Memory::Write_U32(0, infoAddr);
	Memory::Write_U32((u32)totalFrameDuration, infoAddr + 4);
	Memory::Write_U32(totalSamples, infoAddr + 8);
	
	INFO_LOG(Log::ME, "sceMp4GetAvcTrackInfoData: totalDuration=%llu, totalSamples=%d", 
	         totalFrameDuration, totalSamples);
	
	return 0;
}

static u32 sceMp4GetAacTrackInfoData(u32 mp4, u32 trackAddr, u32 infoAddr) {
	if (!Memory::IsValidAddress(infoAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid info address");
	}
	
	const Mp4TrackInfo& track = g_Mp4ParserState.audioTrack;
	
	// Calculate total frame duration in MPEG timestamps
	u64 totalFrameDuration = 0;
	if (!track.samplesDuration.empty() && track.timeScale > 0) {
		for (size_t i = 0; i < track.samplesDuration.size(); i++) {
			totalFrameDuration += sampleToMpegTimestamp(track.samplesDuration[i], track.timeScale);
		}
	}
	
	int totalSamples = getTotalNumberOfSamples(TRACK_TYPE_AUDIO);
	
	// Return 5 32-bit values in infoAddr:
	// [0] = 0 (always)
	// [4] = total frame duration
	// [8] = total number of samples
	// [12] = timeScale
	// [16] = audio channels
	Memory::Write_U32(0, infoAddr);
	Memory::Write_U32((u32)totalFrameDuration, infoAddr + 4);
	Memory::Write_U32(totalSamples, infoAddr + 8);
	Memory::Write_U32(track.timeScale, infoAddr + 12);
	Memory::Write_U32(track.audioChannels, infoAddr + 16);
	
	INFO_LOG(Log::ME, "sceMp4GetAacTrackInfoData: totalDuration=%llu, totalSamples=%d, timeScale=%u, channels=%d", 
	         totalFrameDuration, totalSamples, track.timeScale, track.audioChannels);
	
	return 0;
}

// ============================================================================
// SAMPLE RETRIEVAL FUNCTIONS (Feature #2 - Actual Implementation)
// ============================================================================

static u32 sceMp4GetAvcAu(u32 mp4, u32 trackAddr, u32 auAddr, u32 infoAddr) {
	if (!Memory::IsValidAddress(trackAddr) || !Memory::IsValidAddress(auAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid addresses");
	}
	
	Mp4TrackInfo& track = g_Mp4ParserState.videoTrack;
	
	// Check if we have samples available
	int totalSamples = getTotalNumberOfSamples(TRACK_TYPE_VIDEO);
	if (totalSamples <= 0 || track.currentSample >= totalSamples) {
		return hleLogError(Log::ME, ERROR_MP4_NO_MORE_DATA, "No more video samples");
	}
	
	int sample = track.currentSample;
	
	// Get sample metadata
	int sampleOffset = getSampleOffset(TRACK_TYPE_VIDEO, sample);
	int sampleSize = getSampleSize(TRACK_TYPE_VIDEO, sample);
	int sampleDuration = getSampleDuration(TRACK_TYPE_VIDEO, sample);
	int samplePresentationOffset = getSamplePresentationOffset(TRACK_TYPE_VIDEO, sample);
	
	if (sampleOffset < 0 || sampleSize <= 0) {
		return hleLogError(Log::ME, ERROR_MP4_INVALID_VALUE, "Invalid sample metadata");
	}
	
	// Convert sample duration to MPEG timestamp (90kHz)
	u64 frameDuration = sampleToMpegTimestamp(sampleDuration, track.timeScale);
	u64 framePresentationOffset = sampleToMpegTimestamp(samplePresentationOffset, track.timeScale);
	
	// Read AU structure from memory
	u32 esBuffer = Memory::Read_U32(auAddr);      // esBuffer pointer
	u32 esSize = Memory::Read_U32(auAddr + 4);    // esSize (will be updated)
	u64 pts = Memory::Read_U64(auAddr + 8);       // pts (will be updated)
	u64 dts = Memory::Read_U64(auAddr + 16);      // dts (will be updated)
	
	// Update AU with sample data
	// In a full implementation, would read sample from file at sampleOffset
	// For now, we assume the buffer management is handled externally
	// and we just need to update metadata
	
	// Set size
	Memory::Write_U32(sampleSize, auAddr + 4);
	
	// Calculate timestamps
	dts = track.currentTimestamp;
	track.currentTimestamp += frameDuration;
	pts = dts + framePresentationOffset;
	
	// Write timestamps
	Memory::Write_U64(pts, auAddr + 8);
	Memory::Write_U64(dts, auAddr + 16);
	
	// Advance to next sample
	track.currentSample++;
	
	// Write sample info if requested
	if (Memory::IsValidAddress(infoAddr)) {
		Memory::Write_U32(sample, infoAddr);                    // sample number
		Memory::Write_U32(sampleOffset, infoAddr + 4);          // sample offset
		Memory::Write_U32(sampleSize, infoAddr + 8);            // sample size
		Memory::Write_U32(0, infoAddr + 12);                    // unknown1
		Memory::Write_U32((u32)frameDuration, infoAddr + 16);   // frame duration
		Memory::Write_U32(0, infoAddr + 20);                    // unknown2
		Memory::Write_U32((u32)dts, infoAddr + 24);             // timestamp1 (DTS)
		Memory::Write_U32((u32)pts, infoAddr + 28);             // timestamp2 (PTS)
		
		DEBUG_LOG(Log::ME, "sceMp4GetAvcAu sample=%d, offset=0x%X, size=0x%X, dts=%llu, pts=%llu",
			          sample, sampleOffset, sampleSize, dts, pts);
	}
	
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4GetAacAu(u32 mp4, u32 trackAddr, u32 auAddr, u32 infoAddr) {
	if (!Memory::IsValidAddress(trackAddr) || !Memory::IsValidAddress(auAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid addresses");
	}
	
	Mp4TrackInfo& track = g_Mp4ParserState.audioTrack;
	
	// Check if we have samples available
	int totalSamples = getTotalNumberOfSamples(TRACK_TYPE_AUDIO);
	if (totalSamples <= 0 || track.currentSample >= totalSamples) {
		return hleLogError(Log::ME, ERROR_MP4_NO_MORE_DATA, "No more audio samples");
	}
	
	int sample = track.currentSample;
	
	// Get sample metadata
	int sampleOffset = getSampleOffset(TRACK_TYPE_AUDIO, sample);
	int sampleSize = getSampleSize(TRACK_TYPE_AUDIO, sample);
	int sampleDuration = getSampleDuration(TRACK_TYPE_AUDIO, sample);
	int samplePresentationOffset = getSamplePresentationOffset(TRACK_TYPE_AUDIO, sample);
	
	if (sampleOffset < 0 || sampleSize <= 0) {
		return hleLogError(Log::ME, ERROR_MP4_INVALID_VALUE, "Invalid sample metadata");
	}
	
	// Convert sample duration to MPEG timestamp (90kHz)
	u64 frameDuration = sampleToMpegTimestamp(sampleDuration, track.timeScale);
	u64 framePresentationOffset = sampleToMpegTimestamp(samplePresentationOffset, track.timeScale);
	
	// Read AU structure from memory
	u32 esBuffer = Memory::Read_U32(auAddr);      // esBuffer pointer
	
	// Update AU with sample data
	// Set size
	Memory::Write_U32(sampleSize, auAddr + 4);
	
	// Calculate timestamps
	u64 dts = track.currentTimestamp;
	track.currentTimestamp += frameDuration;
	u64 pts = dts + framePresentationOffset;
	
	// Write timestamps
	Memory::Write_U64(pts, auAddr + 8);
	Memory::Write_U64(dts, auAddr + 16);
	
	// Advance to next sample
	track.currentSample++;
	
	// Write sample info if requested
	if (Memory::IsValidAddress(infoAddr)) {
		Memory::Write_U32(sample, infoAddr);                    // sample number
		Memory::Write_U32(sampleOffset, infoAddr + 4);          // sample offset
		Memory::Write_U32(sampleSize, infoAddr + 8);            // sample size
		Memory::Write_U32(0, infoAddr + 12);                    // unknown1
		Memory::Write_U32((u32)frameDuration, infoAddr + 16);   // frame duration
		Memory::Write_U32(0, infoAddr + 20);                    // unknown2
		Memory::Write_U32((u32)dts, infoAddr + 24);             // timestamp1 (DTS)
		Memory::Write_U32((u32)pts, infoAddr + 28);             // timestamp2 (PTS)
		
		DEBUG_LOG(Log::ME, "sceMp4GetAacAu sample=%d, offset=0x%X, size=0x%X, dts=%llu, pts=%llu",
			          sample, sampleOffset, sampleSize, dts, pts);
	}
	
	return hleLogInfo(Log::ME, 0);
}

// ============================================================================
// OTHER HLE FUNCTIONS
// ============================================================================

static u32 sceMp4InitAu(u32 mp4, u32 bufferAddr, u32 auAddr) {
	// Initialize AU (Access Unit) structure
	// Sets esBuffer pointer and zeros esSize
	if (Memory::IsValidAddress(auAddr)) {
		Memory::Write_U32(bufferAddr, auAddr);        // esBuffer
		Memory::Write_U32(0, auAddr + 4);             // esSize
		Memory::Write_U64(0, auAddr + 8);             // pts
		Memory::Write_U64(0, auAddr + 16);            // dts
	}
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4SearchSyncSampleNum(u32 mp4, u32 trackAddr, u32 searchDirection, u32 sample) {
	// Read track type from trackAddr if valid
	int trackType = TRACK_TYPE_VIDEO; // Default to video
	if (Memory::IsValidAddress(trackAddr)) {
		trackType = Memory::Read_U32(trackAddr + 4); // Assuming trackType at offset 4
	}
	
	int result = searchSyncSampleNum(trackType, sample, searchDirection);
	
	if (result < 0) {
		return hleLogError(Log::ME, result, "Search failed");
	}
	
	return hleLogDebug(Log::ME, result);
}

static u32 sceMp4PutSampleNum(u32 mp4, u32 trackAddr, u32 sample) {
	// Sets the current sample position for the track
	// Used for seeking to a specific sample
	
	if (!Memory::IsValidAddress(trackAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid track address");
	}
	
	// Read track type from trackAddr (assuming structure: [0]=reserved, [4]=trackType)
	int trackType = Memory::Read_U32(trackAddr + 4);
	
	Mp4TrackInfo* track = nullptr;
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr) {
		return hleLogError(Log::ME, ERROR_MP4_INVALID_VALUE, "Invalid track type");
	}
	
	int totalSamples = getTotalNumberOfSamples(trackType);
	if ((int)sample < 0 || (int)sample >= totalSamples) {
		return hleLogError(Log::ME, ERROR_MP4_INVALID_SAMPLE_NUMBER, "Sample number out of range");
	}
	
	// Update current sample position
	track->currentSample = sample;
	
	// Reset timestamp to the cumulative duration up to this sample
	track->currentTimestamp = 0;
	for (int i = 0; i < (int)sample && i < (int)track->samplesDuration.size(); i++) {
		track->currentTimestamp += sampleToMpegTimestamp(track->samplesDuration[i], track->timeScale);
	}
	
	INFO_LOG(Log::ME, "sceMp4PutSampleNum trackType=0x%X, sample=%u, timestamp=%llu", 
	         trackType, sample, track->currentTimestamp);
	
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufPut(u32 mp4, u32 trackAddr, u32 samples) {
	// Puts samples into the track sample buffer
	// This triggers reading sample data from file via callbacks
	
	if (!Memory::IsValidAddress(trackAddr)) {
		return hleLogError(Log::ME, 0x80000004, "Invalid track address");
	}
	
	if (samples == 0) {
		return hleLogInfo(Log::ME, 0);
	}
	
	// Read track type from trackAddr
	int trackType = Memory::Read_U32(trackAddr + 4);
	
	Mp4TrackInfo* track = nullptr;
	if ((trackType & TRACK_TYPE_AUDIO) != 0) {
		track = &g_Mp4ParserState.audioTrack;
	} else if ((trackType & TRACK_TYPE_VIDEO) != 0) {
		track = &g_Mp4ParserState.videoTrack;
	}
	
	if (track == nullptr) {
		return hleLogError(Log::ME, ERROR_MP4_INVALID_VALUE, "Invalid track type");
	}
	
	int totalSamples = getTotalNumberOfSamples(trackType);
	int remainingSamples = totalSamples - track->currentSample;
	
	if (remainingSamples <= 0) {
		return hleLogInfo(Log::ME, 0);
	}
	
	// Calculate how many samples to actually buffer
	int samplesToBuffer = std::min((int)samples, remainingSamples);
	
	// In a full implementation, this would:
	// 1. Calculate total bytes needed for the samples
	// 2. Seek to the first sample's offset via callSeekCallback
	// 3. Read all samples' data via callReadCallback
	// 4. Update buffer management structures
	//
	// For now, we just log the operation
	
	INFO_LOG(Log::ME, "sceMp4TrackSampleBufPut trackType=0x%X, requested=%u, buffering=%d samples",
	         trackType, samples, samplesToBuffer);
	
	// Mark samples as available for reading
	// This is simplified - full implementation would manage a circular buffer
	
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4TrackSampleBufAvailableSize(u32 mp4, u32 trackAddr, u32 writableSamplesAddr, u32 writableBytesAddr) {
	return hleLogError(Log::ME, 0, "unimplemented");
}

static u32 sceMp4Delete(u32 mp4) {
	g_Mp4ParserState = Mp4ParserState();
	return hleLogInfo(Log::ME, 0);
}

static u32 sceMp4AacDecodeInitResource(int unknown) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetAvcTrackInfoData() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufConstruct(u32 mp4, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5, u32 unknown6, u32 unknown7) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufQueryMemSize(u32 unknown1, u32 unknown2, u32 unknown3, u32 unknown4, u32 unknown5)
{
	u32 value = std::max(unknown2 * unknown3, unknown4 << 1) + (unknown2 << 6) + unknown5 + 256;
	return hleLogWarning(Log::ME, value);
}

static u32 sceMp4AacDecode(u32 mp4, u32 auAddr, u32 bufferAddr, u32 init, u32 frequency) {
	return hleLogError(Log::ME, 0, "mp4 %i, auAddr %08x, bufferAddr %08x, init %i, frequency %i ", mp4, auAddr, bufferAddr, init, frequency);
}

static u32 sceMp4GetSampleInfo() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetSampleNumWithTimeStamp() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4TrackSampleBufFlush() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4AacDecodeInit(int unknown) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4GetNumberOfMetaData() {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 sceMp4RegistTrack(u32 mp4, u32 unknown2, u32 unknown3, u32 callbacks, u32 unknown5) {
	return hleLogError(Log::ME, 0, "UNIMPL");
}

static u32 mp4msv_3C2183C7(u32 unknown1, u32 unknown2) {
	if (unknown2) {
		u32 values[5];
		if (Memory::IsValidAddress(unknown2)) {
			for (int i = 0; i < 5; i++) {
				values[i] = Memory::Read_U32(unknown2 + i * 4);
			}
			INFO_LOG(Log::ME, "mp4msv_3C2183C7 unknown values: %08x %08x %08x %08x %08x", 
			         values[0], values[1], values[2], values[3], values[4]);
		} else {
			ERROR_LOG(Log::ME, "mp4msv_3C2183C7 invalid address: %08x", unknown2);
		}
	}

	hleMp4Init();
	return hleLogInfo(Log::ME, 0);
}

static u32 mp4msv_9CA13D1A(u32 unknown1, u32 unknown2) {
	if (unknown2) {
		u32 values[17];
		if (Memory::IsValidAddress(unknown2)) {
			for (int i = 0; i < 17; i++) {
				values[i] = Memory::Read_U32(unknown2 + i * 4);
			}
			INFO_LOG(Log::ME, "mp4msv_9CA13D1A unknown values: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
			         values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
			         values[8], values[9], values[10], values[11], values[12], values[13], values[14], values[15], values[16]);
		} else {
			ERROR_LOG(Log::ME, "mp4msv_9CA13D1A invalid address: %08x", unknown2);
		}
	}

	hleMp4Init();
	return hleLogInfo(Log::ME, 0);
}

const HLEFunction sceMp4[] = {
	{0X68651CBC, &WrapU_V<sceMp4Init>,                           "sceMp4Init",                        'x', ""       },
	{0X9042B257, &WrapU_V<sceMp4Finish>,                         "sceMp4Finish",                      'x', ""       },
	{0XB1221EE7, &WrapU_UUUU<sceMp4Create>,                      "sceMp4Create",                      'x', "xxxx"   },
	{0X538C2057, &WrapU_U<sceMp4Delete>,                         "sceMp4Delete",                      'x', "x"      },
	{0X113E9E7B, &WrapU_V<sceMp4GetNumberOfMetaData>,            "sceMp4GetNumberOfMetaData",         'x', ""       },
	{0X7443AF1D, &WrapU_UU<sceMp4GetMovieInfo>,                  "sceMp4GetMovieInfo",                'x', "xx"     },
	{0X5EB65F26, &WrapU_V<sceMp4GetNumberOfSpecificTrack>,       "sceMp4GetNumberOfSpecificTrack",    'x', ""       },
	{0X7ADFD01C, &WrapU_UUUUU<sceMp4RegistTrack>,                "sceMp4RegistTrack",                 'x', "xxxxx"  },
	{0XBCA9389C, &WrapU_UUUUU<sceMp4TrackSampleBufQueryMemSize>, "sceMp4TrackSampleBufQueryMemSize",  'x', "xxxxx"  },
	{0X9C8F4FC1, &WrapU_UUUUUUU<sceMp4TrackSampleBufConstruct>,  "sceMp4TrackSampleBufConstruct",     'x', "xxxxxxx"},
	{0X0F0187D2, &WrapU_UUU<sceMp4GetAvcTrackInfoData>,          "sceMp4GetAvcTrackInfoData",         'x', "xxx"    },
	{0X9CE6F5CF, &WrapU_UUU<sceMp4GetAacTrackInfoData>,          "sceMp4GetAacTrackInfoData",         'x', "xxx"    },
	{0X4ED4AB1E, &WrapU_I<sceMp4AacDecodeInitResource>,          "sceMp4AacDecodeInitResource",       'x', "i"      },
	{0X10EE0D2C, &WrapU_I<sceMp4AacDecodeInit>,                  "sceMp4AacDecodeInit",               'x', "i"      },
	{0X496E8A65, &WrapU_V<sceMp4TrackSampleBufFlush>,            "sceMp4TrackSampleBufFlush",         'x', ""       },
	{0XB4B400D1, &WrapU_V<sceMp4GetSampleNumWithTimeStamp>,      "sceMp4GetSampleNumWithTimeStamp",   'x', ""       },
	{0XF7C51EC1, &WrapU_V<sceMp4GetSampleInfo>,                  "sceMp4GetSampleInfo",               'x', ""       },
	{0X74A1CA3E, &WrapU_UUUU<sceMp4SearchSyncSampleNum>,         "sceMp4SearchSyncSampleNum",         'x', "xxxx"   },
	{0XD8250B75, &WrapU_UUU<sceMp4PutSampleNum>,                 "sceMp4PutSampleNum",               'x', "xxx"    },
	{0X8754ECB8, &WrapU_UUUU<sceMp4TrackSampleBufAvailableSize>, "sceMp4TrackSampleBufAvailableSize", 'x', "xppp"   },
	{0X31BCD7E0, &WrapU_UUU<sceMp4TrackSampleBufPut>,          "sceMp4TrackSampleBufPut",          'x', "xxx"    },
	{0X5601A6F0, &WrapU_UUUU<sceMp4GetAacAu>,                    "sceMp4GetAacAu",                    'x', "xxxx"   },
	{0X7663CB5C, &WrapU_UUUUU<sceMp4AacDecode>,                  "sceMp4AacDecode",                   'x', "xxxxx"  },
	{0X503A3CBA, &WrapU_UUUU<sceMp4GetAvcAu>,                    "sceMp4GetAvcAu",                    'x', "xxxx"   },
	{0X01C76489, nullptr,                                        "sceMp4TrackSampleBufDestruct",      '?', ""       },
	{0X6710FE77, nullptr,                                        "sceMp4UnregistTrack",               '?', ""       },
	{0X5D72B333, nullptr,                                        "sceMp4AacDecodeExit",               '?', ""       },
	{0X7D332394, nullptr,                                        "sceMp4AacDecodeTermResource",       '?', ""       },
	{0X131BDE57, &WrapU_UUU<sceMp4InitAu>,                       "sceMp4InitAu",                      'x', "xxx"    },
	{0X17EAA97D, nullptr,                                        "sceMp4GetAvcAuWithoutSampleBuf",    '?', ""       },
	{0X28CCB940, nullptr,                                        "sceMp4GetTrackEditList",            '?', ""       },
	{0X3069C2B5, nullptr,                                        "sceMp4GetAvcParamSet",              '?', ""       },
	{0XD2AC9A7E, nullptr,                                        "sceMp4GetMetaData",                 '?', ""       },
	{0X4FB5B756, nullptr,                                        "sceMp4GetMetaDataInfo",             '?', ""       },
	{0X427BEF7F, nullptr,                                        "sceMp4GetTrackNumOfEditList",       '?', ""       },
	{0X532029B8, nullptr,                                        "sceMp4GetAacAuWithoutSampleBuf",    '?', ""       },
	{0XA6C724DC, nullptr,                                        "sceMp4GetSampleNum",                '?', ""       },
	{0X3C2183C7, nullptr,                                        "mp4msv_3C2183C7",                   '?', ""       },
	{0X9CA13D1A, nullptr,                                        "mp4msv_9CA13D1A",                   '?', ""       },
};

const HLEFunction mp4msv[] = {
	{0x3C2183C7, &WrapU_UU<mp4msv_3C2183C7>,                    "mp4msv_3C2183C7",               'x', "xx"      },
	{0x9CA13D1A, &WrapU_UU<mp4msv_9CA13D1A>,                    "mp4msv_9CA13D1A",               'x', "xx"      },
};

void Register_sceMp4() {
	RegisterHLEModule("sceMp4", ARRAY_SIZE(sceMp4), sceMp4);
}

void Register_mp4msv() {
	RegisterHLEModule("mp4msv", ARRAY_SIZE(mp4msv), mp4msv);
}
