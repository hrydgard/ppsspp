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

#include "net/http_client.h"
#include "net/resolve.h"
#include "net/url.h"
#include "Common/CommonTypes.h"
#include "Core/Loaders.h"

class HTTPFileLoader : public FileLoader {
public:
	HTTPFileLoader(const std::string &filename);
	virtual ~HTTPFileLoader() override;

	virtual bool Exists() override;
	virtual bool ExistsFast() override;
	virtual bool IsDirectory() override;
	virtual s64 FileSize() override;
	virtual std::string Path() const override;

	virtual void Seek(s64 absolutePos) override;
	virtual size_t Read(size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(filepos_, bytes, count, data, flags);
	}
	virtual size_t Read(size_t bytes, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(filepos_, bytes, data, flags);
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, size_t count, void *data, Flags flags = Flags::NONE) override {
		return ReadAt(absolutePos, bytes * count, data, flags) / bytes;
	}
	virtual size_t ReadAt(s64 absolutePos, size_t bytes, void *data, Flags flags = Flags::NONE) override;

private:
	void Prepare();

	void Connect() {
		if (!connected_) {
			connected_ = client_.Connect();
		}
	}

	void Disconnect() {
		if (connected_) {
			client_.Disconnect();
		}
		connected_ = false;
	}

	s64 filesize_;
	s64 filepos_;
	Url url_;
	net::AutoInit netInit_;
	http::Client client_;
	std::string filename_;
	bool connected_;
	bool prepared_;
};
