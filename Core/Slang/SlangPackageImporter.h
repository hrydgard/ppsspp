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
#include <functional>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include "Common/File/Path.h"

namespace http { class Request; }

// Called during extraction as each file is written: (filesWritten, totalCandidateFiles).
// May be invoked from a worker thread. Optional.
using SlangExtractProgressCallback = std::function<void(int, int)>;

// Extract a slang-shaders zip at 'zipPath' directly into 'destRoot' (default GetSlangShaderDir()).
// Only shader-asset files that pass ResolveSafeZipEntryPath are written. destRoot is cleared first
// and extraction happens in place (no temp-dir+rename swap: directory rename is unsupported on
// Android scoped storage and its copy fallback is unusably slow / fails). On success, writes
// destRoot/manifest.json (sourceUrl, timestamp, fileCount) last. Returns false + *error on failure,
// removing the partial destRoot. 'sourceUrl'/'unixTimestamp' are recorded in the manifest ("" / 0
// if unknown). Optional 'progress' is called per written file. Synchronous; run from a worker thread.
bool ExtractSlangPackage(const Path &zipPath, const Path &destRoot,
                         const std::string &sourceUrl, int64_t unixTimestamp,
                         std::string *error,
                         const SlangExtractProgressCallback &progress = nullptr);

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
	float GetProgress() const;                 // 0..1 across download + extraction
	std::string GetError() const { return error_; }
	bool Busy() const { return state_ == SlangImportState::DOWNLOADING || state_ == SlangImportState::EXTRACTING; }
private:
	SlangImportState state_ = SlangImportState::IDLE;
	std::shared_ptr<http::Request> download_;
	std::thread extractThread_;
	std::atomic<bool> extractDone_{false};
	std::atomic<bool> extractOk_{false};
	std::atomic<float> extractProgress_{0.0f};  // 0..1 fraction of files written (worker updates, UI reads)
	std::string error_;
	std::string threadError_;   // written by the extract worker only; copied to error_ by Update() on the UI thread
	std::string sourceUrl_;
	Path zipPath_;
};

// Process-wide instance (mirrors g_GameManager).
extern SlangPackageImporter g_SlangImporter;
