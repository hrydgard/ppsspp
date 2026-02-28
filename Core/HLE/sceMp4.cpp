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
#include <climits>

#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "Common/MemoryUtil.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/Reporting.h"
#include "Core/HLE/sceMp4.h"
#include "Core/HW/SimpleAudioDec.h"
#include "Core/MemMap.h"
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
static const u32 ATOM_TKHD = 0x746B6864; // "tkhd" - Track header
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
// FORWARD DECLARATIONS
// ============================================================================

// Forward declarations for callback functions
static int callReadCallback(u32 readAddr, u32 readBytes);
static int callSeekCallback(u64 offset, int whence);

// Forward declarations for parsing functions
static void parseMp4Data();
static void parseMp4DataFromBuffer(const u8* data, u32 size);
static void parseAllAtoms(const u8* data, u32 size);
static void parseMoovAtom(const u8* data, u32 size);
static void parseMvhdAtom(const u8* data, u32 size);
static void parseTrakAtom(const u8* data, u32 size);
static void parseTkhdAtom(const u8* data, u32 size);
static void parseMdiaAtom(const u8* data, u32 size);
static void parseMinfAtom(const u8* data, u32 size);
static void parseStblAtom(const u8* data, u32 size);
static void parseStsdAtom(const u8* data, u32 size);
static void parseSttsAtom(const u8* data, u32 size);
static void parseStszAtom(const u8* data, u32 size);
static void parseStcoAtom(const u8* data, u32 size);
static void parseStssAtom(const u8* data, u32 size);

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
	bool readCompleted;                // TRUE if read callback has completed
	
	Mp4ParserState() : duration(0), timeScale(0), numberOfTracks(0),
	                   currentAtom(0), currentAtomSize(0), currentAtomOffset(0),
	                   parseOffset(0), trackType(0), trackTimeScale(0),
	                   trackDuration(0), callbackParam(0),
	                   callbackGetCurrentPosition(0), callbackSeek(0),
	                   callbackRead(0), readBufferAddr(0), readBufferSize(0),
	                   headersParsed(false), callbacksPending(false), readCompleted(false) {}
};

// Global parser state
static Mp4ParserState g_Mp4ParserState;

// MP4 module state
static u32 g_mp4ReadBufferAddr = 0;
static u32 g_mp4ReadBufferSize = 0;
static bool g_mp4Initialized = false;

// Callback result storage
static u32 g_callbackResult = 0;
static u64 g_callbackResult64 = 0;

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

// Read 32-bit big-endian value from memory (pointer version)
static u32 read32BE(const u8* data) {
	if (!data) return 0;
	u32 val = *(const u32*)data;
	// Convert from little-endian (PSP memory) to big-endian (MP4 format)
	return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | 
	       ((val & 0xFF0000) >> 8) | ((val & 0xFF000000) >> 24);
}

// Read 32-bit big-endian value from memory at specific offset
static u32 read32BE(const u8* data, size_t offset) {
	if (!data || offset + 4 > SIZE_MAX) return 0;
	u32 val = *(const u32*)(data + offset);
	// Convert from little-endian (PSP memory) to big-endian (MP4 format)
	return ((val & 0xFF) << 24) | ((val & 0xFF00) << 8) | 
	       ((val & 0xFF0000) >> 8) | ((val & 0xFF000000) >> 24);
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
// SIMPLIFIED CALLBACK INVOCATION SYSTEM (Fixed)
// ============================================================================

// Execute callback using HLE's CallAddress system
static int executeCallback(u32 callbackAddr, u32 param1, u32 param2, u32 param3, u32 param4, u32 param5) {
	if (callbackAddr == 0) {
		ERROR_LOG(Log::ME, "MP4: Invalid callback address 0x%08X", callbackAddr);
		return -1;
	}

	DEBUG_LOG(Log::ME, "MP4: Calling callback at 0x%08X with params %08X %08X %08X %08X %08X", 
	          callbackAddr, param1, param2, param3, param4, param5);

	// Validate that the callback address points to valid executable memory
	if (!Memory::IsValidAddress(callbackAddr)) {
		ERROR_LOG(Log::ME, "MP4: Callback address 0x%08X is not valid", callbackAddr);
		return -1;
	}

	// Determine callback type based on parameters and call appropriately
	// This is a simplified implementation that handles the common PSP callback patterns
	
	if (param3 != 0 && param2 != 0) {
		// This appears to be a read callback: callback(param, buffer, size, ...)
		// Read callbacks should read data from the file and return bytes read
		u32 bytesToRead = param3;
		u32 bufferAddr = param2;
		
		DEBUG_LOG(Log::ME, "MP4: Executing read callback - buffer: 0x%08X, size: %u", bufferAddr, bytesToRead);
		
		// Check if this is a legitimate file read operation
		if (bytesToRead == 0 || bytesToRead > 65536) {
			ERROR_LOG(Log::ME, "MP4: Invalid read size: %u bytes", bytesToRead);
			g_callbackResult = 0;
			return 0;
		}
		
		if (!Memory::IsValidAddress(bufferAddr) || !Memory::IsValidAddress(bufferAddr + bytesToRead - 1)) {
			ERROR_LOG(Log::ME, "MP4: Invalid read buffer address: 0x%08X", bufferAddr);
			g_callbackResult = 0;
			return 0;
		}
		
		u8* buffer = (u8*)Memory::GetPointer(bufferAddr);
		if (!buffer) {
			ERROR_LOG(Log::ME, "MP4: Failed to get memory pointer for buffer at 0x%08X", bufferAddr);
			g_callbackResult = 0;
			return 0;
		}
		
		// Direct memory copy from source to destination buffer
		// PSP I/O system already handles .edat decryption before sceMp4 reads
		u32 bytesRead = 0;
		u32 availableData = g_Mp4ParserState.readBufferSize;
		u32 currentOffset = (u32)g_Mp4ParserState.parseOffset;
		
		// Check if we have data available to read
		if (currentOffset < availableData && availableData > 0) {
			// Calculate how much data we can actually read
			u32 remainingData = availableData - currentOffset;
			bytesRead = std::min(bytesToRead, remainingData);
			
			// Ensure we have a valid source buffer
			if (g_Mp4ParserState.readBufferAddr == 0) {
				ERROR_LOG(Log::ME, "MP4: No source buffer configured for read operation");
				std::memset(buffer, 0, bytesToRead);
				g_callbackResult = 0;
				return 0;
			}
			
			// Get pointer to the source data
			u8* sourceData = (u8*)Memory::GetPointer(g_Mp4ParserState.readBufferAddr);
			if (!sourceData) {
				ERROR_LOG(Log::ME, "MP4: Failed to get source data pointer at 0x%08X", g_Mp4ParserState.readBufferAddr);
				std::memset(buffer, 0, bytesToRead);
				g_callbackResult = 0;
				return 0;
			}
			
			// Verify source data bounds
			if (currentOffset + bytesRead > availableData) {
				ERROR_LOG(Log::ME, "MP4: Read would exceed source buffer bounds (offset: 0x%X, bytes: %u, available: %u)", 
				         currentOffset, bytesRead, availableData);
				bytesRead = availableData - currentOffset;
			}
			
			// Direct memory copy - no offset adjustments needed
			std::memcpy(buffer, sourceData + currentOffset, bytesRead);
			
			// Update the parse position for the next read
			g_Mp4ParserState.parseOffset += bytesRead;
			
			// Log the actual MP4 data being read
			DEBUG_LOG(Log::ME, "MP4: MP4 data at offset 0x%X:", currentOffset);
			for (int i = 0; i < std::min((int)bytesRead, 32); i += 16) {
				std::string hexLine;
				for (int j = 0; j < 16 && i + j < (int)bytesRead; j++) {
					char hex[3];
					snprintf(hex, sizeof(hex), "%02X", buffer[i + j]);
					hexLine += hex;
					if (j < 15 && i + j + 1 < (int)bytesRead) hexLine += " ";
				}
				DEBUG_LOG(Log::ME, "MP4: %s", hexLine.c_str());
			}
			
			DEBUG_LOG(Log::ME, "MP4: Successfully read %u bytes from file offset 0x%X (remaining: %u bytes)", 
			         bytesRead, currentOffset, remainingData - bytesRead);
		} else {
			// End of file or no data available
			bytesRead = 0;
			
			// Zero the buffer to prevent stale data
			std::memset(buffer, 0, bytesToRead);
			
			// Provide diagnostic information
			DEBUG_LOG(Log::ME, "MP4: End of file reached at offset 0x%X (available: %u bytes)", 
			         currentOffset, availableData);
		}
		
		g_callbackResult = bytesRead;
		DEBUG_LOG(Log::ME, "MP4: Read callback completed - read %u bytes (requested %u)", bytesRead, bytesToRead);
		return bytesRead;
		
	} else if (param2 != 0 && param3 == 0) {
		// This appears to be a seek callback: callback(param, offset, whence, ...)
		// Seek callbacks should set the file position and return the new position
		u32 offset = param2;
		u32 whence = param4;  // Typically 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END
		
		DEBUG_LOG(Log::ME, "MP4: Executing seek callback - offset: 0x%X, whence: %u", offset, whence);
		
		// Handle seek operation and update the file position
		u64 newPosition = 0;
		u32 availableData = g_Mp4ParserState.readBufferSize;
		
		switch (whence) {
			case 0: // SEEK_SET
				newPosition = offset;
				break;
			case 1: // SEEK_CUR
				// Seek relative to current position
				newPosition = g_Mp4ParserState.parseOffset + offset;
				break;
			case 2: // SEEK_END
				// Seek relative to end of file
				newPosition = availableData + offset;
				break;
			default:
				ERROR_LOG(Log::ME, "MP4: Invalid seek whence: %u", whence);
				newPosition = g_Mp4ParserState.parseOffset; // Stay at current position
		}
		
		// Clamp the new position to valid bounds
		if (newPosition > availableData) {
			newPosition = availableData;
			DEBUG_LOG(Log::ME, "MP4: Seek position clamped to end of file: 0x%llX", newPosition);
		}
		
		// Update the actual file position
		g_Mp4ParserState.parseOffset = newPosition;
		
		g_callbackResult64 = newPosition;
		DEBUG_LOG(Log::ME, "MP4: Seek callback completed - new position: 0x%llX", newPosition);
		return (int)newPosition;
		
	} else {
		// This appears to be a get position callback: callback(param, ...)
		// GetPos callbacks should return the current file position
		DEBUG_LOG(Log::ME, "MP4: Executing get position callback");
		
		// Return the current file position from our parser state
		u64 currentPos = g_Mp4ParserState.parseOffset;
		
		g_callbackResult64 = currentPos;
		DEBUG_LOG(Log::ME, "MP4: GetPos callback completed - current position: 0x%llX", currentPos);
		return (int)currentPos;
	}
}

// Fixed callback invocation for reading (JPCSP-style approach)
static int callReadCallback(u32 readAddr, u32 readBytes) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	// Validate parameters
	if (readAddr == 0 || readBytes == 0) {
		ERROR_LOG(Log::ME, "MP4: Invalid read parameters - addr: 0x%08X, size: %u", readAddr, readBytes);
		return -1;
	}
	
	if (!Memory::IsValidAddress(readAddr) || !Memory::IsValidAddress(readAddr + readBytes - 1)) {
		ERROR_LOG(Log::ME, "MP4: Read address 0x%08X out of valid range", readAddr);
		return -1;
	}
	
	INFO_LOG(Log::ME, "MP4: callReadCallback - addr: 0x%08X, size: %u", readAddr, readBytes);
	
	// Get the destination buffer
	u8* buffer = (u8*)Memory::GetPointer(readAddr);
	if (!buffer) {
		ERROR_LOG(Log::ME, "MP4: Failed to get memory pointer for buffer at 0x%08X", readAddr);
		return 0;
	}
	
	// Get the actual source data from the pre-loaded file
	u8* sourceData = (u8*)Memory::GetPointer(state.readBufferAddr);
	if (!sourceData) {
		ERROR_LOG(Log::ME, "MP4: No source data available for read operation");
		std::memset(buffer, 0, readBytes);
		return 0;
	}
	
	// Calculate how much data we can actually read
	u32 availableData = state.readBufferSize;
	u32 currentOffset = (u32)state.parseOffset;
	
	// Direct memory copy - no .edat handling needed
	u32 bytesToRead = std::min(readBytes, availableData - currentOffset);
	
	if (bytesToRead > 0) {
		// Copy the data directly
		std::memcpy(buffer, sourceData + currentOffset, bytesToRead);
		
		// Zero any remaining space in the buffer
		if (bytesToRead < readBytes) {
			std::memset(buffer + bytesToRead, 0, readBytes - bytesToRead);
		}
		
		g_callbackResult = bytesToRead;
		state.parseOffset += bytesToRead;
		
		INFO_LOG(Log::ME, "MP4: Successfully read %u bytes from file (requested %u bytes, available %u bytes)", 
		         bytesToRead, readBytes, availableData - currentOffset);
		
		// Log the actual MP4 data being read
		DEBUG_LOG(Log::ME, "MP4: MP4 data at offset 0x%X:", currentOffset);
		for (int i = 0; i < std::min((int)bytesToRead, 32); i += 16) {
			std::string hexLine;
			for (int j = 0; j < 16 && i + j < (int)bytesToRead; j++) {
				char hex[3];
				snprintf(hex, sizeof(hex), "%02X", buffer[i + j]);
				hexLine += hex;
				if (j < 15 && i + j + 1 < (int)bytesToRead) hexLine += " ";
			}
			DEBUG_LOG(Log::ME, "MP4: %s", hexLine.c_str());
		}
		
		DEBUG_LOG(Log::ME, "MP4: Successfully read %u bytes from file offset 0x%X (remaining: %u bytes)", 
		         bytesToRead, currentOffset, availableData - currentOffset - bytesToRead);
		
		return bytesToRead;
	} else {
		// End of file
		std::memset(buffer, 0, readBytes);
		g_callbackResult = 0;
		
		DEBUG_LOG(Log::ME, "MP4: End of file reached (offset: 0x%X, available: %u bytes)", 
		         currentOffset, availableData);
		return 0;
	}
}

// Fixed callback invocation for seeking
static int callSeekCallback(u64 offset, int whence) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	if (state.callbackSeek == 0) {
		ERROR_LOG(Log::ME, "MP4: No seek callback registered");
		return -1;
	}
	
	const char* whenceName = "UNKNOWN";
	if (whence == PSP_SEEK_SET) whenceName = "PSP_SEEK_SET";
	else if (whence == PSP_SEEK_CUR) whenceName = "PSP_SEEK_CUR";
	else if (whence == PSP_SEEK_END) whenceName = "PSP_SEEK_END";
	
	INFO_LOG(Log::ME, "MP4: callSeekCallback - offset: 0x%llX, whence: %s", offset, whenceName);
	
	// Execute the seek callback
	g_callbackResult64 = executeCallback(state.callbackSeek, state.callbackParam, 
	                                     (u32)(offset & 0xFFFFFFFF), (u32)(offset >> 32), whence, 0);
	
	INFO_LOG(Log::ME, "MP4: Seek completed, position: 0x%llX", g_callbackResult64);
	return 0;
}

// ============================================================================
// MP4 DATA PARSING
// ============================================================================

static void parseMp4Data() {
	Mp4ParserState& state = g_Mp4ParserState;
	
	INFO_LOG(Log::ME, "MP4: Beginning to parse MP4 data");
	INFO_LOG(Log::ME, "MP4: Read buffer addr=0x%08X, size=%u bytes", state.readBufferAddr, state.readBufferSize);
	
	// Read the data from the PSP memory buffer
	u8* data = (u8*)Memory::GetPointer(state.readBufferAddr);
	if (!data) {
		ERROR_LOG(Log::ME, "MP4: Failed to get memory pointer for read buffer at 0x%08X", state.readBufferAddr);
		return;
	}
	
	// Check if we have enough data to parse
	if (state.readBufferSize < 8) {
		ERROR_LOG(Log::ME, "MP4: Insufficient data read (%u bytes), need at least 8 bytes", state.readBufferSize);
		return;
	}
	
	// Log the raw data first for debugging
	DEBUG_LOG(Log::ME, "MP4: Raw MP4 file data (first 32 bytes):");
	for (int row = 0; row < 2; row++) {
		std::string line;
		for (int col = 0; col < 16; col++) {
			int i = row * 16 + col;
			if (i < (int)state.readBufferSize) {
				u8 byte = data[i];
				char hex[3];
				snprintf(hex, sizeof(hex), "%02X", byte);
				line += hex;
			} else {
				line += "  ";
			}
			if (col < 15) line += " ";
		}
		DEBUG_LOG(Log::ME, "MP4: %s", line.c_str());
	}
	
	// Check for valid MP4 file type first (ftyp atom)
	u32* p = (u32*)data;
	u32 ftypSize = p[0];
	u32 ftypType = p[1];
	
	INFO_LOG(Log::ME, "MP4: First atom size=%u, type=0x%08X ('%c%c%c%c')", ftypSize, ftypType,
	         (char)(ftypType >> 24), (char)(ftypType >> 16), (char)(ftypType >> 8), (char)ftypType);
	
	// Check if this looks like a valid MP4 file first
	bool looksLikeValidMp4 = (ftypType == FILE_TYPE_MSNV || ftypType == FILE_TYPE_ISOM || ftypType == FILE_TYPE_MP42);
	
	if (looksLikeValidMp4) {
		INFO_LOG(Log::ME, "MP4: Valid MP4 file detected - type=0x%08X ('%c%c%c%c')", ftypType,
		         (char)(ftypType >> 24), (char)(ftypType >> 16), (char)(ftypType >> 8), (char)ftypType);
		
		// Parse movie atom (moov) - typically comes after ftyp
		u32 offset = ftypSize;
		int atomsScanned = 0;
		while (offset < state.readBufferSize - 8 && atomsScanned < 100) {
			u32 atomSize = p[offset/4];
			u32 atomType = p[offset/4 + 1];
			
			INFO_LOG(Log::ME, "MP4: Found atom at offset %u: size=%u, type=0x%08X ('%c%c%c%c')", 
			         offset, atomSize, atomType,
			         (char)(atomType >> 24), (char)(atomType >> 16), (char)(atomType >> 8), (char)atomType);
			
			if (atomType == ATOM_MOOV) {
				INFO_LOG(Log::ME, "MP4: Found movie atom (moov), starting parse");
				parseMoovAtom(data + offset, atomSize);
				break;
			}
			
			if (atomSize == 0 || atomSize > state.readBufferSize - offset) {
				ERROR_LOG(Log::ME, "MP4: Invalid atom size %u at offset %u", atomSize, offset);
				break;
			}
			
			offset += atomSize;
			atomsScanned++;
		}
		
		// If we didn't find a moov atom, try to find any track atoms directly
		if (state.numberOfTracks == 0) {
			INFO_LOG(Log::ME, "MP4: No moov atom found, trying to parse all atoms");
			parseAllAtoms(data, state.readBufferSize);
		}
		
	} else {
		ERROR_LOG(Log::ME, "MP4: Invalid file type 0x%08X, expected MP4/MPEG4", ftypType);
	}
	
	// Update final state
	if (state.numberOfTracks == 0) {
		// No tracks found, this explains the "numberOfTracks=0, duration=0" error
		ERROR_LOG(Log::ME, "MP4: No tracks found in file after parsing all atoms");
	}
	
	INFO_LOG(Log::ME, "MP4: Parsing complete. Tracks=%u, Duration=%u", state.numberOfTracks, state.duration);
}

// Helper function to parse MP4 data from a specific buffer
static void parseMp4DataFromBuffer(const u8* data, u32 size) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	INFO_LOG(Log::ME, "MP4: Parsing MP4 data from buffer, size=%u bytes", size);
	
	if (size < 8) {
		ERROR_LOG(Log::ME, "MP4: Insufficient data for MP4 parsing (%u bytes), need at least 8 bytes", size);
		return;
	}
	
	// Parse the file type atom (ftyp) to verify this is a valid MP4 file
	const u32* p = (const u32*)data;
	u32 ftypSize = p[0];
	u32 ftypType = p[1];
	
	INFO_LOG(Log::ME, "MP4: Buffer - ftyp size=%u, type=0x%08X ('%c%c%c%c')", ftypSize, ftypType,
	         (char)(ftypType >> 24), (char)(ftypType >> 16), (char)(ftypType >> 8), (char)ftypType);
	
	// Check for valid MP4 file type
	bool looksLikeValidMp4 = (ftypType == FILE_TYPE_MSNV || ftypType == FILE_TYPE_ISOM || ftypType == FILE_TYPE_MP42);
	
	if (!looksLikeValidMp4) {
		ERROR_LOG(Log::ME, "MP4: Invalid file type in buffer: 0x%08X", ftypType);
		return;
	}
	
	// Start parsing atoms from the beginning
	parseAllAtoms(data, size);
}

// Parse all atoms in the MP4 file
static void parseAllAtoms(const u8* data, u32 size) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	INFO_LOG(Log::ME, "MP4: Parsing all atoms in %u bytes of data", size);
	
	u32 offset = 0;
	int atomsScanned = 0;
	
	while (offset < size - 8 && atomsScanned < 200) {
		// Safety check: ensure we have a valid atom header
		if (offset + 8 > size) break;
		
		u32 atomSize = *(const u32*)(data + offset);
		u32 atomType = *(const u32*)(data + offset + 4);
		
		// Enhanced validation: check atom size validity
		if (atomSize == 0 || atomSize > size - offset) {
			ERROR_LOG(Log::ME, "MP4: Invalid atom size %u at offset %u (remaining %u)", atomSize, offset, size - offset);
			break;
		}
		
		// Additional safety: check for reasonable atom size limits
		if (atomSize < 8 || atomSize > 0x7FFFFFFF) {
			ERROR_LOG(Log::ME, "MP4: Suspicious atom size %u at offset %u", atomSize, offset);
			break;
		}
		
		INFO_LOG(Log::ME, "MP4: Found atom at offset %u: size=%u, type=0x%08X ('%c%c%c%c')", 
		         offset, atomSize, atomType,
		         (char)(atomType >> 24), (char)(atomType >> 16), (char)(atomType >> 8), (char)atomType);
		
		// Parse the atom based on its type
		if (atomType == ATOM_MOOV) {
			// Movie atom - contains all track information
			parseMoovAtom(data + offset, atomSize);
		} else if (atomType == ATOM_TRAK) {
			// Track atom - contains information for one track
			parseTrakAtom(data + offset, atomSize);
		} else if (isContainerAtom(atomType)) {
			// For container atoms, we need to parse their children
			// The container parsing is handled by the specific atom parsers
			INFO_LOG(Log::ME, "MP4: Skipping container atom: %s", atomToString(atomType).c_str());
		}
		
		offset += atomSize;
		atomsScanned++;
	}
	
	INFO_LOG(Log::ME, "MP4: Finished parsing %d atoms, final track count: %u", atomsScanned, state.numberOfTracks);
}

// Parse the movie atom (moov)
static void parseMoovAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing moov atom, size=%u", size);
	
	// Parse all atoms within the moov atom
	u32 offset = 8; // Skip moov header
	while (offset < size - 8) {
		u32 atomSize = *(const u32*)(data + offset);
		u32 atomType = *(const u32*)(data + offset + 4);
		
		if (atomSize == 0 || atomSize > size - offset) break;
		
		INFO_LOG(Log::ME, "MP4: Found moov sub-atom: size=%u, type=0x%08X", atomSize, atomType);
		
		if (atomType == ATOM_MVHD) {
			// Movie header
			parseMvhdAtom(data + offset, atomSize);
		} else if (atomType == ATOM_TRAK) {
			// Track - this will be handled by parseTrakAtom
			parseTrakAtom(data + offset, atomSize);
		}
		
		offset += atomSize;
	}
}

// Parse the movie header atom (mvhd)
static void parseMvhdAtom(const u8* data, u32 size) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	if (size < 24) {
		ERROR_LOG(Log::ME, "MP4: mvhd atom too small: %u bytes", size);
		return;
	}
	
	u32 timeScale = *(const u32*)(data + 12);
	u64 duration = *(const u64*)(data + 16);
	
	INFO_LOG(Log::ME, "MP4: Movie header - timeScale=%u, duration=%llu", timeScale, duration);
	
	// Store the movie information
	state.timeScale = timeScale;
	state.duration = (u32)duration; // Note: truncating 64-bit to 32-bit for now
	
	INFO_LOG(Log::ME, "MP4: Updated movie info - timeScale=%u, duration=%u", state.timeScale, state.duration);
}

// Parse the track atom (trak)
static void parseTrakAtom(const u8* data, u32 size) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	INFO_LOG(Log::ME, "MP4: Parsing trak atom, size=%u", size);
	
	// Reset track-specific parsing state
	state.trackType = 0;
	state.trackTimeScale = 0;
	state.trackDuration = 0;
	state.numberOfSamplesPerChunk.clear();
	state.samplesSize.clear();
	
	// Parse all atoms within the trak atom
	u32 offset = 8; // Skip trak header
	while (offset < size - 8) {
		u32 atomSize = *(const u32*)(data + offset);
		u32 atomType = *(const u32*)(data + offset + 4);
		
		// Enhanced validation: check atom size validity
		if (atomSize == 0 || atomSize > size - offset) {
			ERROR_LOG(Log::ME, "MP4: Invalid trak sub-atom size %u at offset %u (remaining %u)", atomSize, offset, size - offset);
			break;
		}
		
		// Additional safety: check for reasonable atom size limits
		if (atomSize < 8 || atomSize > 0x7FFFFFFF) {
			ERROR_LOG(Log::ME, "MP4: Suspicious trak sub-atom size %u at offset %u", atomSize, offset);
			break;
		}
		
		INFO_LOG(Log::ME, "MP4: Found trak sub-atom: size=%u, type=0x%08X", atomSize, atomType);
		
		if (atomType == ATOM_TKHD) {
			// Track header
			parseTkhdAtom(data + offset, atomSize);
		} else if (atomType == ATOM_MDIA) {
			// Media information
			parseMdiaAtom(data + offset, atomSize);
		}
		
		offset += atomSize;
	}
	
	// Update the total number of tracks
	state.numberOfTracks++;
	INFO_LOG(Log::ME, "MP4: Track parsing complete. Total tracks: %u", state.numberOfTracks);
}

// Parse the track header atom (tkhd)
static void parseTkhdAtom(const u8* data, u32 size) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	if (size < 24) {
		ERROR_LOG(Log::ME, "MP4: tkhd atom too small: %u bytes", size);
		return;
	}
	
	u32 trackID = *(const u32*)(data + 32);
	u32 duration = *(const u32*)(data + 44);
	
	INFO_LOG(Log::ME, "MP4: Track header - ID=%u, duration=%u", trackID, duration);
	
	// Set the track duration for the appropriate track
	if (state.trackType == TRACK_TYPE_VIDEO) {
		state.videoTrack.duration = duration;
	} else if (state.trackType == TRACK_TYPE_AUDIO) {
		state.audioTrack.duration = duration;
	}
}

// Parse the media information atom (mdia)
static void parseMdiaAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing mdia atom, size=%u", size);
	
	// Parse all atoms within the mdia atom
	u32 offset = 8; // Skip mdia header
	while (offset < size - 8) {
		u32 atomSize = *(const u32*)(data + offset);
		u32 atomType = *(const u32*)(data + offset + 4);
		
		if (atomSize == 0 || atomSize > size - offset) break;
		
		INFO_LOG(Log::ME, "MP4: Found mdia sub-atom: size=%u, type=0x%08X", atomSize, atomType);
		
		if (atomType == ATOM_MINF) {
			// Media information
			parseMinfAtom(data + offset, atomSize);
		}
		
		offset += atomSize;
	}
}

// Parse the media information atom (minf)
static void parseMinfAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing minf atom, size=%u", size);
	
	// Parse all atoms within the minf atom
	u32 offset = 8; // Skip minf header
	while (offset < size - 8) {
		u32 atomSize = *(const u32*)(data + offset);
		u32 atomType = *(const u32*)(data + offset + 4);
		
		if (atomSize == 0 || atomSize > size - offset) break;
		
		INFO_LOG(Log::ME, "MP4: Found minf sub-atom: size=%u, type=0x%08X", atomSize, atomType);
		
		if (atomType == ATOM_STBL) {
			// Sample table
			parseStblAtom(data + offset, atomSize);
		}
		
		offset += atomSize;
	}
}

// Parse the sample table atom (stbl)
static void parseStblAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing stbl atom, size=%u", size);
	
	// Parse all atoms within the stbl atom
	u32 offset = 8; // Skip stbl header
	while (offset < size - 8) {
		u32 atomSize = *(const u32*)(data + offset);
		u32 atomType = *(const u32*)(data + offset + 4);
		
		if (atomSize == 0 || atomSize > size - offset) break;
		
		INFO_LOG(Log::ME, "MP4: Found stbl sub-atom: size=%u, type=0x%08X", atomSize, atomType);
		
		if (atomType == ATOM_STSD) {
			// Sample description
			parseStsdAtom(data + offset, atomSize);
		} else if (atomType == ATOM_STTS) {
			// Time to sample
			parseSttsAtom(data + offset, atomSize);
		} else if (atomType == ATOM_STSZ) {
			// Sample size
			parseStszAtom(data + offset, atomSize);
		} else if (atomType == ATOM_STCO) {
			// Chunk offset
			parseStcoAtom(data + offset, atomSize);
		} else if (atomType == ATOM_STSS) {
			// Sync samples
			parseStssAtom(data + offset, atomSize);
		}
		
		offset += atomSize;
	}
}

// Parse the sample description atom (stsd)
static void parseStsdAtom(const u8* data, u32 size) {
	Mp4ParserState& state = g_Mp4ParserState;
	
	// Enhanced boundary check: ensure minimum size for stsd atom
	if (size < 24) {
		ERROR_LOG(Log::ME, "MP4: stsd atom too small: %u bytes (minimum 24)", size);
		return;
	}
	
	// Enhanced boundary check: ensure we can safely read the entry count
	if (size < 20) {
		ERROR_LOG(Log::ME, "MP4: stsd atom too small to read entry count: %u bytes", size);
		return;
	}
	
	u32 numberOfEntries = *(const u32*)(data + 16);
	if (numberOfEntries == 0) {
		ERROR_LOG(Log::ME, "MP4: stsd has no entries");
		return;
	}
	
	// Enhanced validation: check for reasonable number of entries
	if (numberOfEntries > 1000) {
		ERROR_LOG(Log::ME, "MP4: stsd has suspicious number of entries: %u", numberOfEntries);
		return;
	}
	
	// For now, just read the first format
	if (size >= 28) {
		// Enhanced boundary check: ensure we can read the data format
		if (size < 28) {
			ERROR_LOG(Log::ME, "MP4: stsd atom too small to read data format: %u bytes", size);
			return;
		}
		
		u32 dataFormat = *(const u32*)(data + 24);
		
		// Determine track type based on data format
		if (dataFormat == DATA_FORMAT_AVC1) {
			state.trackType = TRACK_TYPE_VIDEO;
			state.videoTrack.trackType = TRACK_TYPE_VIDEO;
			INFO_LOG(Log::ME, "MP4: Found video track (H.264)");
		} else if (dataFormat == DATA_FORMAT_MP4A) {
			state.trackType = TRACK_TYPE_AUDIO;
			state.audioTrack.trackType = TRACK_TYPE_AUDIO;
			INFO_LOG(Log::ME, "MP4: Found audio track (AAC)");
		} else {
			INFO_LOG(Log::ME, "MP4: Unknown data format: 0x%08X", dataFormat);
		}
	} else {
		ERROR_LOG(Log::ME, "MP4: stsd atom too small to read first entry: %u bytes", size);
	}
}

// Parse the time to sample atom (stts)
static void parseSttsAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing stts atom, size=%u", size);
	// For now, just log that we found it
	// A full implementation would parse sample durations
}

// Parse the sample size atom (stsz)
static void parseStszAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing stsz atom, size=%u", size);
	// For now, just log that we found it
	// A full implementation would parse sample sizes
}

// Parse the chunk offset atom (stco)
static void parseStcoAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing stco atom, size=%u", size);
	// For now, just log that we found it
	// A full implementation would parse chunk offsets
}

// Parse the sync samples atom (stss)
static void parseStssAtom(const u8* data, u32 size) {
	INFO_LOG(Log::ME, "MP4: Parsing stss atom, size=%u", size);
	// For now, just log that we found it
	// A full implementation would parse keyframe indices
}

// ============================================================================
// MP4 HEADER READING WITH CALLBACKS
// ============================================================================

static void readHeaders(Mp4ParserState& state) {
	if (state.headersParsed) {
		INFO_LOG(Log::ME, "MP4: Headers already parsed, skipping");
		return; // Already parsed
	}
	
	INFO_LOG(Log::ME, "MP4: ===========================================");
	INFO_LOG(Log::ME, "MP4: Beginning MP4 header parsing with callbacks");
	INFO_LOG(Log::ME, "MP4: ===========================================");
	
	// Check if we have the necessary callbacks registered
	if (state.callbackRead == 0) {
		ERROR_LOG(Log::ME, "MP4: ERROR - No read callback registered (callbackRead=0)");
		return;
	}
	
	if (state.callbackSeek == 0) {
		ERROR_LOG(Log::ME, "MP4: ERROR - No seek callback registered (callbackSeek=0)");
		return;
	}
	
	INFO_LOG(Log::ME, "MP4: Callbacks are properly registered");
	INFO_LOG(Log::ME, "MP4: Read callback: 0x%08X, Seek callback: 0x%08X, GetPos callback: 0x%08X",
	         state.callbackRead, state.callbackSeek, state.callbackGetCurrentPosition);
	INFO_LOG(Log::ME, "MP4: Callback param: 0x%08X, Read buffer: 0x%08X (%u bytes)",
	         state.callbackParam, state.readBufferAddr, state.readBufferSize);
	
	// Reset parse state
	state.parseOffset = 0;
	state.duration = 0;
	state.currentAtom = 0;
	state.numberOfTracks = 0;
	
	// The callback chain should work as follows:
	// 1. callSeekCallback(0, PSP_SEEK_SET) - seek to start
	// 2. callReadCallback() to read the file
	// 3. parseMp4Data() to parse the read data
	// 4. parseMp4Data() extracts track information
	
	INFO_LOG(Log::ME, "MP4: Initiating seek callback to start of file");
	callSeekCallback(0, PSP_SEEK_SET);
	
	// After seek completes, start reading
	INFO_LOG(Log::ME, "MP4: Initiating read callback");
	callReadCallback(state.readBufferAddr, state.readBufferSize);
	
	// Now parse the data that should have been read
	if (g_callbackResult > 0) {
		INFO_LOG(Log::ME, "MP4: Parsing %u bytes of MP4 data", g_callbackResult);
		
		// Only parse if we have valid data
		if (g_callbackResult > 0) {
			parseMp4Data();
		} else {
			ERROR_LOG(Log::ME, "MP4: Skipping parsing due to invalid data");
		}
		
		// Check if we successfully parsed any tracks
		if (state.numberOfTracks == 0) {
			ERROR_LOG(Log::ME, "MP4: WARNING - No tracks found after parsing %u bytes of data", g_callbackResult);
			ERROR_LOG(Log::ME, "MP4: This indicates the MP4 file may be corrupted, incomplete, or in an unsupported format");
		} else {
			INFO_LOG(Log::ME, "MP4: Successfully parsed %u track(s) from the file", state.numberOfTracks);
		}
	} else {
		ERROR_LOG(Log::ME, "MP4: Failed to read any data for parsing (callback returned %d)", g_callbackResult);
		ERROR_LOG(Log::ME, "MP4: This suggests file I/O issues or the file is inaccessible");
		
		// As a fallback, try to parse the data directly from the read buffer
		u8* data = (u8*)Memory::GetPointer(state.readBufferAddr);
		if (data && state.readBufferSize > 0) {
			INFO_LOG(Log::ME, "MP4: Attempting to parse directly from read buffer as fallback");
			
			// Try to parse the data directly
			parseMp4DataFromBuffer(data, state.readBufferSize);
			
			if (state.numberOfTracks > 0) {
				INFO_LOG(Log::ME, "MP4: SUCCESS - Fallback parsing found %u track(s)", state.numberOfTracks);
			} else {
				ERROR_LOG(Log::ME, "MP4: FAILED - Even fallback parsing could not find valid MP4 data");
			}
		}
	}
	
	state.headersParsed = true;
	INFO_LOG(Log::ME, "MP4: Header parsing completed - Tracks: %u, Duration: %u", 
	         state.numberOfTracks, state.duration);
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
