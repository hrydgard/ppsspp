#pragma once

#include <string_view>
#include <string>

struct lua_State;

class LuaContext {
public:
	void Init();
	void Shutdown();
	void Load(const char *code);

	// For the console.
	void Execute(std::string_view cmd, std::string *output);

private:
	// Naming it L is a common convention.
	lua_State *L = nullptr;
};

extern LuaContext g_lua;
