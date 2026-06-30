#pragma once

#include <string_view>
#include <string>
#include <memory>

#include "ext/sol/forward.hpp"

struct lua_State;

enum class LogLineType {
	Cmd,
	String,
	Integer,
	Float,
	Error,
	External,
	Url,
};

// A bit richer than regular log lines, so we can display them in color, and allow various UI tricks.
// All have a string, but some may also have a number or other value.
struct LuaLogLine {
	LogLineType type;
	std::string line;
	int number;
	double fnumber;
};

class LuaContext {
public:
	virtual ~LuaContext() = default;

	void Init();
	void Shutdown();
	void RunCode(const std::string &code);

	virtual void Print(LogLineType type, std::string_view text);
	void PrintF(LogLineType type, const char *fmt, ...);

protected:
	void SetupContext(sol::state &lua);
	std::unique_ptr<sol::state> lua_;
};

class LuaInteractiveContext : public LuaContext {
public:
	// For the console.
	void ExecuteConsoleCommand(std::string_view cmd);
	std::vector<std::string> AutoComplete(std::string_view cmd) const;

	const std::vector<LuaLogLine> GetLines() const {
		return lines_;
	}
	void Clear() { lines_.clear(); }

	void Print(LogLineType type, std::string_view text);
	void Print(std::string_view text) {
		Print(LogLineType::External, text);
	}

private:
	std::vector<std::string> GetGlobals() const;
	std::vector<LuaLogLine> lines_;
};

extern LuaInteractiveContext g_lua;
