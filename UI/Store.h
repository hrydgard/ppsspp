// Copyright (c) 2013- PPSSPP Project.

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

#include "base/functional.h"
#include "ui/ui_screen.h"
#include "ui/viewgroup.h"
#include "net/http_client.h"

#include "UI/MiscScreens.h"

// Game screen: Allows you to start a game, delete saves, delete the game,
// set game specific settings, etc.
// Uses GameInfoCache heavily to implement the functionality.

struct json_value;

enum EntryType {
	ENTRY_PBPZIP,
	ENTRY_ISO,
};

struct StoreCategory {
	std::string name;
};

struct StoreEntry {
	EntryType type;
	std::string name;
	std::string description;
	std::string author;
	std::string iconURL;
	std::string file;  // This is the folder name of the installed one too, and hence a "unique-ish" identifier.
	std::string category;
	std::string downloadURL;  // Only set for games that are not hosted on store.ppsspp.org
	bool hidden;
	u64 size;
};

struct StoreFilter {
	std::string categoryId;
};

class StoreScreen : public UIDialogScreenWithBackground {
public:
	StoreScreen();
	~StoreScreen();

	virtual void update(InputState &input);
	virtual std::string tag() const { return "store"; }

protected:
	virtual void CreateViews();
	UI::EventReturn OnGameSelected(UI::EventParams &e);
	UI::EventReturn OnRetry(UI::EventParams &e);
	UI::EventReturn OnGameLaunch(UI::EventParams &e);

private:
	void SetFilter(const StoreFilter &filter);
	void ParseListing(std::string json);
	std::vector<StoreEntry> FilterEntries();

	std::string GetStoreJsonURL(std::string storePath) const;
	std::string GetTranslatedString(const json_value *json, std::string key, const char *fallback = 0) const;

	std::shared_ptr<http::Download> listing_;
	std::shared_ptr<http::Download> image_;

	// TODO: Replace with a PathBrowser or similar. Though that one only supports
	// local filesystems at the moment.
	std::string storePath_;

	bool loading_;
	bool connectionError_;

	std::map<std::string, StoreCategory> categories_;

	// We download the whole store in one JSON request. Not super scalable but works fine
	// for now. entries_ contains all the products in the store.
	std::vector<StoreEntry> entries_;

	StoreFilter filter_;
	std::string lang_;

	UI::ViewGroup *productPanel_;
};

