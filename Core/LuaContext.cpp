#include <string>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/LuaContext.h"

std::string g_stringBuf;

// Sol is expensive to include so we only do it here.
#include "ext/sol/sol.hpp"

LuaContext g_lua;

// Custom print function
static void log(const std::string& message) {
	INFO_LOG(Log::System, "%s", message.c_str());
	g_stringBuf = message;
}

void LuaContext::Init() {

	_dbg_assert_(lua_ == nullptr);
	lua_.reset(new sol::state());
	lua_->open_libraries(sol::lib::base);
	lua_->open_libraries(sol::lib::table);
	lua_->open_libraries(sol::lib::bit32);
	lua_->open_libraries(sol::lib::string);
	lua_->open_libraries(sol::lib::math);

	// Not sure if we can safely override print(). So making a new function.
	lua_->set("log", &log);
}

void LuaContext::Shutdown() {
	lua_.reset();
}

void LuaContext::Load(const char *code) {

}

void LuaContext::Execute(std::string_view cmd, std::string *output) {
	try {
		lua_->script(cmd);
		*output = g_stringBuf;
		g_stringBuf.clear();
	} catch (sol::error e) {
		ERROR_LOG(Log::System, "Exception: %s", e.what());
		*output = e.what();
	}
}
