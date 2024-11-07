#include <string>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/LuaContext.h"

// lua_writeline

/*

#define lua_writestring(s,l)   fwrite((s), sizeof(char), (l), stdout)
#define lua_writeline()        (lua_writestring("\n", 1), fflush(stdout))
#define lua_writestringerror(s,p) \
        (fprintf(stderr, (s), (p)), fflush(stderr))

		*/

extern "C" {
#include "ext/lua/lua.h"
#include "ext/lua/lauxlib.h"
#include "ext/lua/lualib.h"
}

// Sol is expensive to include so we only do it here.
#include "ext/sol/sol.hpp"

LuaContext g_lua;

void LuaContext::Init() {
	_dbg_assert_(L == nullptr);

	L = luaL_newstate();
	luaopen_base(L);
	luaopen_table(L);
	luaopen_string(L);
	luaopen_math(L);
}

void LuaContext::Shutdown() {
	lua_close(L);
	L = nullptr;
}

void LuaContext::Load(const char *code) {
	/*
	while (fgets(buff, sizeof(buff), stdin) != NULL) {
		error = luaL_loadbuffer(L, buff, strlen(buff), "line") ||
			lua_pcall(L, 0, 0, 0);
		if (error) {
			fprintf(stderr, "%s", lua_tostring(L, -1));
			lua_pop(L, 1);  // pop error message from the stack
		}
	}*/
}

void LuaContext::Execute(std::string_view cmd, std::string *output) {
	int error = luaL_loadbuffer(L, cmd.data(), cmd.length(), "line");
	if (error) {
		ERROR_LOG(Log::System, "%s", lua_tostring(L, -1));
		*output = lua_tostring(L, -1);
		lua_pop(L, 1);  /* pop error message from the stack */
		return;
	}

	lua_pcall(L, 0, 0, 0);

	/* Get the number of values which have been pushed */
	int res_count = lua_tointeger(L, -1);
	if (!lua_isnumber(L, -1))
		*output = "function `f' must return a number";
	int value = lua_tonumber(L, -1);
	/* Remove the number of values */
	lua_pop(L, 1);

	*output = StringFromFormat("%d", value);
}
