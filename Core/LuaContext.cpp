#include <string>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/LuaContext.h"
#include "Core/MemMap.h"
#include "Core/RetroAchievements.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"

// Sol is expensive to include so we only do it here.
#include "ext/sol/sol.hpp"

LuaContext g_lua;

static bool IsProbablyExpression(std::string_view input) {
	// Heuristic: If it's a single-line statement without assignment or keywords, assume it's an expression.
	return !(input.find("=") != std::string_view::npos ||
		input.find("function") != std::string_view::npos ||
		input.find("do") != std::string_view::npos ||
		input.find("end") != std::string_view::npos ||
		input.find("return") != std::string_view::npos ||
		input.find("local") != std::string_view::npos);
}

// Custom print function
static void print(const std::string& message) {
	g_lua.Print(message);
}

// TODO: Should these also echo to the console?
static void debug(const std::string &message) {
	DEBUG_LOG(Log::System, "%s", message.c_str());
}

static void info(const std::string &message) {
	INFO_LOG(Log::System, "%s", message.c_str());
}

static void warn(const std::string &message) {
	WARN_LOG(Log::System, "%s", message.c_str());
}

static void error(const std::string &message) {
	ERROR_LOG(Log::System, "%s", message.c_str());
}

// TODO: We should probably disallow or at least discourage raw read/writes and instead
// only support read/writes that refer to the name of a memory region.
static int r32(int address) {
	if (!Memory::IsValid4AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("r32: bad address %08x", address));
		return 0;
	}

	return Memory::ReadUnchecked_U32(address);
}

static void w32(int address, int value) {
	if (!Memory::IsValid4AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("w32: bad address %08x trying to write %08x", address, value));
	}

	Memory::WriteUnchecked_U32(value, address);  // NOTE: These are backwards for historical reasons.
}

static double bitcast_s32_to_float(int value) {
	float fvalue;
	memcpy(&fvalue, &value, 4);
	return fvalue;
}

static int bitcast_float_to_s32(double value) {
	float fvalue = value;
	int ivalue;
	memcpy(&ivalue, &value, 4);
	return ivalue;
}

// TODO: We should probably disallow or at least discourage raw read/writes and instead
// only support read/writes that refer to the name of a memory region.
static double rf(int address) {
	if (!Memory::IsValid4AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("rf: bad address %08x", address));
		return 0;
	}

	return Memory::ReadUnchecked_Float(address);
}

static void wf(int address, double value) {
	float fvalue = (float)value;
	if (!Memory::IsValid4AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("w32: bad address %08x trying to write %08x", address, value));
	}

	Memory::WriteUnchecked_Float(value, address);  // NOTE: These are backwards for historical reasons.
}

static int scan32(int address, int size, int value) {
	if (Memory::IsValidRange(address, size)) {
		g_lua.Print(LogLineType::Error, "bad range");
		return 0;
	}

	for (int i = 0; i < size; i += 4) {
		if (Memory::ReadUnchecked_U32(address + i) == value) {
			return address + i;
		}
	}

	return 0;
}

static void stop() {
	System_PostUIMessage(UIMessage::REQUEST_GAME_STOP);
}

static void reset() {
	System_PostUIMessage(UIMessage::REQUEST_GAME_RESET);
}

void LuaContext::Init() {
	_dbg_assert_(lua_ == nullptr);
	lua_.reset(new sol::state());
	lua_->open_libraries(sol::lib::base);
	lua_->open_libraries(sol::lib::table);
	lua_->open_libraries(sol::lib::bit32);
	lua_->open_libraries(sol::lib::string);
	lua_->open_libraries(sol::lib::math);

	extern const char *PPSSPP_GIT_VERSION;
	lua_->set("ver", PPSSPP_GIT_VERSION);

	lua_->set("print", &print);
	lua_->set("debug", &debug);
	lua_->set("info", &info);
	lua_->set("warn", &warn);
	lua_->set("error", &error);

	// lua_->set("list_modules", &list_modules);

	// Memory accessors
	lua_->set("r32", &r32);
	lua_->set("w32", &w32);
	lua_->set("wf", &wf);
	lua_->set("rf", &rf);

	lua_->set("bitcast_s32_to_float", &bitcast_s32_to_float);
	lua_->set("bitcast_float_to_s32", &bitcast_float_to_s32);

	lua_->set("scan32", &scan32);

	lua_->set("stop", &stop);
	lua_->set("reset", &reset);
}

void LuaContext::Shutdown() {
	lua_.reset();
}

const char *SolTypeToString(sol::type type) {
	switch (type) {
	case sol::type::boolean: return "boolean";
	default: return "other";
	}
}

void LuaContext::Print(LogLineType type, std::string_view text) {
	lines_.push_back(LuaLogLine{ type, std::string(text)});
}

void LuaContext::ExecuteConsoleCommand(std::string_view cmd) {
	if (Achievements::HardcoreModeActive()) {
		Print(LogLineType::Error, "RetroAchievemnts hardcore mode is active, lua console disabled.");
		return;
	}

	// TODO: Also rewrite expressions like:
	// print "hello"
	// to
	// print("hello") ?
	try {
		std::string command;
		if (IsProbablyExpression(cmd)) {
			command = "return ";
			command += cmd;
		} else {
			command = cmd;
		}
		auto result = lua_->script(command);
		if (result.valid()) {
			for (const sol::stack_proxy &item : result) {
				switch (item.get_type()) {
				case sol::type::number:
				{
					int num = item.get<int>();
					lines_.push_back(LuaLogLine{ LogLineType::Integer, StringFromFormat("%08x (%d)", num, num), item.get<int>()});
					break;
				}
				case sol::type::string:
				{
					// TODO: Linebreak multi-line strings.
					lines_.push_back(LuaLogLine{ LogLineType::String, item.get<std::string>() });
					break;
				}
				default:
					break;
				}
			}
		} else {
			sol::error err = result;
			lines_.push_back(LuaLogLine{ LogLineType::Error, std::string(err.what()) });
		}
	} catch (const sol::error& e) {
		ERROR_LOG(Log::System, "Lua exception: %s", e.what());
		lines_.push_back(LuaLogLine{ LogLineType::Error, std::string(e.what()) });
	}
}
