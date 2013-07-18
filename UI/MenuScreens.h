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

#include <string>
#include <vector>
#include <map>

#include "ui/screen.h"
#include "ui/ui.h"
#include "file/file_util.h"

class LogoScreen : public Screen
{
public:
	LogoScreen(const std::string &bootFilename)
		: bootFilename_(bootFilename), frames_(0) {}
	void key(const KeyInput &key);
	void update(InputState &input);
	void render();
	void sendMessage(const char *message, const char *value);

private:
	void Next();
	std::string bootFilename_;
	int frames_;
};

class MenuScreen : public Screen
{
public:
	MenuScreen();
	void update(InputState &input);
	void render();
	void sendMessage(const char *message, const char *value);
	void dialogFinished(const Screen *dialog, DialogResult result);

private:
	int frames_;
	bool showAtracShortcut_;
};

// Dialog box, meant to be pushed
class PauseScreen : public Screen
{
public:
	void update(InputState &input);
	void render();
	virtual void sendMessage(const char *msg, const char *value);

	struct Message
	{
		Message(const char *m, const char *v)
			: msg(m), value(v) {}

		const char *msg;
		const char *value;
	};

	virtual void *dialogData()
	{
		return m_data;
	}

	PauseScreen() : m_data(NULL) {}

private:
	Message* m_data;
};


class SettingsScreen : public Screen
{
public:
	void update(InputState &input);
	void render();
};


class DeveloperScreen : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class AudioScreen : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class GraphicsScreenP1 : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class GraphicsScreenP2 : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class GraphicsScreenP3 : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class SystemScreen : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class LanguageScreen : public Screen
{
public:
	LanguageScreen();
	void update(InputState &input);
	void render();
private:
	std::vector<FileInfo> langs_;
	std::map<std::string, std::pair<std::string, int>> langValuesMapping;
};

class ControlsScreen : public Screen
{
public:
	void update(InputState &input);
	void render();
};

class KeyMappingScreen : public Screen
{
public:
	KeyMappingScreen() : currentMap_(0) {}
	void update(InputState &input);
	void render();
private:
	int currentMap_;
};

// Dialog box, meant to be pushed
class KeyMappingNewKeyDialog : public Screen
{
public:
	KeyMappingNewKeyDialog(int btn, int currentMap) {
		pspBtn = btn;
		last_kb_deviceid = 0;
		last_kb_key = 0;
		last_axis_deviceid = 0;
		last_axis_id = -1;
		currentMap_ = currentMap;
	}
	void update(InputState &input);
	void render();
	void key(const KeyInput &key);
	void axis(const AxisInput &axis);

private:
	int pspBtn;
	int last_kb_deviceid;
	int last_kb_key;
	int last_axis_deviceid;
	int last_axis_id;
	int last_axis_direction;
	int currentMap_;
};

struct FileSelectScreenOptions {
	const char* filter;  // Enforced extension filter. Case insensitive, extensions separated by ":".
	bool allowChooseDirectory;
	int folderIcon;
	std::map<std::string, int> iconMapping;
};


class FileSelectScreen : public Screen
{
public:
	FileSelectScreen(const FileSelectScreenOptions &options);
	void update(InputState &input);
	void render();

	// Override these to for example write the current directory to a config file.
	virtual void onSelectFile() {}
	virtual void onCancel() {}
	void key(const KeyInput &key);

private:
	void updateListing();

	FileSelectScreenOptions options_;
	UIList list_;
	std::string currentDirectory_;
	std::vector<FileInfo> listing_;
};


class CreditsScreen : public Screen
{
public:
	CreditsScreen() : frames_(0) {}
	void update(InputState &input);
	void render();
private:
	int frames_;
};

void DrawWatermark();
