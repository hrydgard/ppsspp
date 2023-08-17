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

#include <mutex>

#include "Common/CommonTypes.h"
#include "Common/File/Path.h"
#include "Core/Loaders.h"

#ifdef _WIN32
typedef void *HANDLE;
#endif

#ifdef HAVE_LIBRETRO_VFS
#include <streams/file_stream.h>
typedef RFILE* HANDLE;
#endif

class LocalFileLoader : public FileLoader {
public:
	LocalFileLoader(const Path &filename);
	~LocalFileLoader();

	bool Exists() override;
	bool IsDirectory() override;
	s64 FileSize() override;
	Path GetPath() const override {
		return filename_;
	}
	size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override;

private:
#if !defined(_WIN32) && !defined(HAVE_LIBRETRO_VFS)
	void DetectSizeFd();
	int fd_ = -1;
#else
	HANDLE handle_ = 0;
#endif
	u64 filesize_ = 0;
	Path filename_;
	std::mutex readLock_;
	bool isOpenedByFd_ = false;
};
