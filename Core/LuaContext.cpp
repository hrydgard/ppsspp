#include <string>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/LuaContext.h"
#include "Core/MemMap.h"

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
	if (Memory::IsValid4AlignedAddress(address)) {
		return Memory::Read_U32(address);
	} else {
		g_lua.Print(LogLineType::Error, StringFromFormat("r32: bad address %08x", address));
		return 0;
	}
}

static void w32(int address, int value) {
	if (Memory::IsValid4AlignedAddress(address)) {
		Memory::Write_U32(value, address);  // NOTE: These are backwards for historical reasons.
	} else {
		g_lua.Print(LogLineType::Error, StringFromFormat("w32: bad address %08x trying to write %08x", address, value));
	}
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

	lua_->set("r32", &r32);
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
	} catch (sol::error e) {
		ERROR_LOG(Log::System, "Lua exception: %s", e.what());
		lines_.push_back(LuaLogLine{ LogLineType::Error, std::string(e.what()) });
	}
}
