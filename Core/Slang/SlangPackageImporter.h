// Copyright (c) 2026- PPSSPP Project.

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
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include "Common/File/Path.h"

namespace http { class Request; }

// Extract a slang-shaders zip at 'zipPath' into 'destRoot' (default GetSlangShaderDir()),
// using a temp dir + atomic swap. Only shader-asset files that pass ResolveSafeZipEntryPath
// are written. On success, writes destRoot/manifest.json (sourceUrl, timestamp, fileCount).
// Returns false and leaves any prior install untouched on any failure; *error is set.
// 'sourceUrl' and 'unixTimestamp' are recorded in the manifest (pass "" / 0 if unknown).
// Synchronous; Task 7 calls this from a worker thread.
bool ExtractSlangPackage(const Path &zipPath, const Path &destRoot,
                         const std::string &sourceUrl, int64_t unixTimestamp,
                         std::string *error);

enum class SlangImportState { IDLE, DOWNLOADING, EXTRACTING, DONE, FAILED };

class SlangPackageImporter {
public:
	~SlangPackageImporter();
	// Begins download+import of 'url' (default g_Config.sSlangBuildbotUrl if empty).
	// Returns false if an import is already in progress.
	bool Start(const std::string &url);
	// Pump each frame from the UI thread (drives download completion + thread join).
	void Update();
	SlangImportState GetState() const { return state_; }
	float GetProgress() const;                 // 0..1 across download (extraction is coarse)
	std::string GetError() const { return error_; }
	bool Busy() const { return state_ == SlangImportState::DOWNLOADING || state_ == SlangImportState::EXTRACTING; }
private:
	SlangImportState state_ = SlangImportState::IDLE;
	std::shared_ptr<http::Request> download_;
	std::thread extractThread_;
	std::atomic<bool> extractDone_{false};
	std::atomic<bool> extractOk_{false};
	std::string error_;
	std::string sourceUrl_;
	Path zipPath_;
};

// Process-wide instance (mirrors g_GameManager).
extern SlangPackageImporter g_SlangImporter;
