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

#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

class RetryingFileLoader : public FileLoader {
public:
	RetryingFileLoader(FileLoader *backend);
	virtual ~RetryingFileLoader() override;

	virtual bool Exists() override;
	virtual bool IsDirectory() override;
	virtual s64 FileSize() override;
	virtual std::string Path() const override;

	virtual void Seek(s64 absolutePos) override;
	virtual size_t Read(size_t bytes, size_t count, void *data) override {
		return ReadAt(filepos_, bytes, count, data);
	}
	virtual size_t Read(size_t bytes, void *data) override {
		return ReadAt(filepos_, bytes, data);
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data) override {
		return ReadAt(absolutePos, bytes * count, data) / bytes;
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data) override;

private:
	enum {
		MAX_RETRIES = 3,
	};

	s64 filepos_;
	FileLoader *backend_;
};
