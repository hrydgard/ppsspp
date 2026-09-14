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

// This is about the log *channels themselves* (their level/enabled state) - the actual log
// message stream is a separate, passive broadcast, see LogBroadcaster.cpp's "log" event (which
// keeps its existing numeric 'level' - only this newer API uses level names, for readability).

#include "Common/Log/LogManager.h"
#include "Core/Debugger/WebSocket/LogConfigSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

static const char *LogLevelToString(LogLevel level) {
	switch (level) {
	case LogLevel::LNOTICE: return "notice";
	case LogLevel::LERROR: return "error";
	case LogLevel::LWARNING: return "warning";
	case LogLevel::LINFO: return "info";
	case LogLevel::LDEBUG: return "debug";
	case LogLevel::LVERBOSE: return "verbose";
	default: return "unknown";
	}
}

static bool LogLevelFromString(const std::string &s, LogLevel *out) {
	if (s == "notice") *out = LogLevel::LNOTICE;
	else if (s == "error") *out = LogLevel::LERROR;
	else if (s == "warning") *out = LogLevel::LWARNING;
	else if (s == "info") *out = LogLevel::LINFO;
	else if (s == "debug") *out = LogLevel::LDEBUG;
	else if (s == "verbose") *out = LogLevel::LVERBOSE;
	else return false;
	return true;
}

DebuggerSubscriber *WebSocketLogConfigInit(DebuggerEventHandlerMap &map) {
	map["log.channels.list"] = &WebSocketLogChannelsList;
	map["log.channel.set"] = &WebSocketLogChannelSet;
	return nullptr;
}

// List all log channels and their current level/enabled state (log.channels.list)
//
// No parameters.
//
// Response (same event name):
//  - channels: array of objects, each with properties:
//     - name: string channel name - pass this back as 'channel' to log.channel.set.
//     - level: string severity level, one of 'notice', 'error', 'warning', 'info', 'debug',
//       'verbose' (in that order, least to most noisy.) A message is only actually logged for
//       this channel if its own level is at or above this severity, and the channel is enabled.
//     - enabled: boolean, whether this channel logs anything at all right now.
void WebSocketLogChannelsList(DebuggerRequest &req) {
	JsonWriter &json = req.Respond();
	json.pushArray("channels");
	for (int i = 0; i < LogManager::GetNumChannels(); ++i) {
		Log type = (Log)i;
		const LogChannel *chan = g_logManager.GetLogChannel(type);
		json.pushDict();
		json.writeString("name", LogManager::GetLogTypeName(type));
		json.writeString("level", LogLevelToString(chan->level));
		json.writeBool("enabled", chan->enabled);
		json.pop();
	}
	json.pop();
}

// Change a log channel's level and/or enabled state (log.channel.set)
//
// Parameters:
//  - channel: string, channel name (as returned by log.channels.list - case sensitive.)
//  - level: optional string, one of 'notice', 'error', 'warning', 'info', 'debug', 'verbose' -
//    see log.channels.list for what this means.
//  - enabled: optional boolean, whether this channel should log at all.
//
// At least one of level/enabled must be given.
//
// Response (same event name):
//  - name: channel name, repeated back.
//  - level: the channel's resulting level (string, see log.channels.list.)
//  - enabled: the channel's resulting enabled state.
void WebSocketLogChannelSet(DebuggerRequest &req) {
	std::string channelName;
	if (!req.ParamString("channel", &channelName))
		return;

	bool hasLevel = req.HasParam("level");
	LogLevel level = LogLevel::LNOTICE;
	if (hasLevel) {
		std::string levelStr;
		if (!req.ParamString("level", &levelStr, DebuggerParamType::OPTIONAL))
			return;
		if (!LogLevelFromString(levelStr, &level))
			return req.Fail("Invalid 'level', must be notice, error, warning, info, debug, or verbose");
	}
	bool hasEnabled = req.HasParam("enabled");
	bool enabled = false;
	if (hasEnabled && !req.ParamBool("enabled", &enabled, DebuggerParamType::OPTIONAL))
		return;

	if (!hasLevel && !hasEnabled)
		return req.Fail("Must specify 'level' and/or 'enabled'");

	for (int i = 0; i < LogManager::GetNumChannels(); ++i) {
		Log type = (Log)i;
		if (channelName != LogManager::GetLogTypeName(type))
			continue;

		// These are meant as temporary, session-only diagnostic tweaks - make sure they never
		// get written back over the user's actual saved log settings.
		g_logManager.NotifyChannelsChangedByDebugger();
		if (hasLevel)
			g_logManager.SetLogLevel(type, level);
		if (hasEnabled)
			g_logManager.SetEnabled(type, enabled);

		const LogChannel *chan = g_logManager.GetLogChannel(type);
		JsonWriter &json = req.Respond();
		json.writeString("name", channelName);
		json.writeString("level", LogLevelToString(chan->level));
		json.writeBool("enabled", chan->enabled);
		return;
	}

	req.Fail("No log channel found with that name");
}
