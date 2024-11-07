#pragma once

#include <string_view>
#include <string>
#include <memory>

#include "ext/sol/forward.hpp"

struct lua_State;

class LuaContext {
public:
	void Init();
	void Shutdown();
	void Load(const char *code);

	// For the console.
	void Execute(std::string_view cmd, std::string *output);

private:
	std::unique_ptr<sol::state> lua_;

	// Naming it L is a common convention.
	lua_State *L = nullptr;
};

extern LuaContext g_lua;
