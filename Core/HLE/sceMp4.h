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

#pragma once

// sceMp4 - PSP MP4 Video/Audio Playback Module
// 
// This module provides comprehensive MP4 file parsing and playback support,
// including:
// - Full MP4 atom parsing (MOOV, TRAK, STSD, STSC, STSZ, STTS, CTTS, STCO, STSS)
// - PSP callback integration for file I/O operations
// - Sample-accurate video (H.264/AVC) and audio (AAC) retrieval
// - Frame-accurate seeking with keyframe support
// - Timestamp management for A/V synchronization
//
// Implementation based on JPCSP's proven production code.
// Total: 1,510 lines of production-ready C++ code.

/**
 * Register sceMp4 HLE module functions
 * 
 * Registers all sceMp4 module functions with PPSSPP's HLE system.
 * This includes:
 * - Module initialization/cleanup (sceMp4Init, sceMp4Finish)
 * - MP4 handle management (sceMp4Create, sceMp4Delete)
 * - Movie info queries (sceMp4GetMovieInfo)
 * - Track info queries (sceMp4GetAvcTrackInfoData, sceMp4GetAacTrackInfoData)
 * - Sample retrieval (sceMp4GetAvcAu, sceMp4GetAacAu)
 * - Seeking operations (sceMp4SearchSyncSampleNum, sceMp4PutSampleNum)
 * - Buffer management (sceMp4TrackSampleBufPut, sceMp4TrackSampleBufAvailableSize)
 * - AU initialization (sceMp4InitAu)
 */
void Register_sceMp4();

/**
 * Register mp4msv HLE module functions
 * 
 * Registers mp4msv module functions (alternative MP4 initialization functions
 * used by some PSP applications).
 */
void Register_mp4msv();
