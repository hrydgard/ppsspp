#include <string>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Core/LuaContext.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSDebugInterface.h"
#include "Core/MIPS/MIPSAsm.h"
#include "Core/RetroAchievements.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/OSD.h"

// Sol is expensive to include so we only do it here.
#include "ext/sol/sol.hpp"

LuaInteractiveContext g_lua;

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

static int r16(int address) {
	if (!Memory::IsValid2AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("r16: bad address %08x", address));
		return 0;
	}

	return Memory::ReadUnchecked_U16(address);
}

static int rs16(int address) {
	if (!Memory::IsValid2AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("rs16: bad address %08x", address));
		return 0;
	}

	return (s16)Memory::ReadUnchecked_U16(address);
}

static void w16(int address, int value) {
	if (!Memory::IsValid2AlignedAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("w16: bad address %08x trying to write %04x", address, value & 0xFFFF));
	}

	Memory::WriteUnchecked_U16(value, address);  // NOTE: These are backwards for historical reasons.
}

static int r8(int address) {
	if (!Memory::IsValidAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("r8: bad address %08x", address));
		return 0;
	}

	return Memory::ReadUnchecked_U8(address);
}

static int rs8(int address) {
	if (!Memory::IsValidAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("rs8: bad address %08x", address));
		return 0;
	}

	return (s16)Memory::ReadUnchecked_U8(address);
}

static void w8(int address, int value) {
	if (!Memory::IsValidAddress(address)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("w8: bad address %08x trying to write %02x", address, value & 0xFF));
	}

	Memory::WriteUnchecked_U8(value, address);  // NOTE: These are backwards for historical reasons.
}

static int gpr(int reg) {
	return currentDebugMIPS ? currentDebugMIPS->GetGPR32Value(reg) : 0;
}

static void set_gpr(int reg, int value) {
	if (currentDebugMIPS)
		currentDebugMIPS->SetGPR32Value(reg, value);
}

static double bitcast_s32_to_float(int value) {
	float fvalue;
	memcpy(&fvalue, &value, 4);
	return fvalue;
}

static int bitcast_float_to_s32(double value) {
	float fvalue = (float)value;
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

	Memory::WriteUnchecked_Float((float)value, address);  // NOTE: These are backwards for historical reasons.
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

sol::table get_strings(sol::this_state ts) {
	sol::state_view lua(ts);
	sol::table t = lua.create_table();
	t[1] = "apple";
	t[2] = "banana";
	t[3] = "cherry";
	return t;
}

void assemble(int destAddress, const char *code) {
	if (!Memory::IsValid4AlignedAddress(destAddress)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("assemble: bad address %08x", destAddress));
		return;
	}
	// TODO: Better error handling.
	if (!MIPSAsm::MipsAssembleOpcode(code, currentDebugMIPS, (u32)destAddress)) {
		std::string error = MIPSAsm::GetAssembleError();
		g_lua.Print(LogLineType::Error, StringFromFormat("Failed to assemble '%s': %s", code, error.c_str()));
	}
}

// Not yet working
void sys_call(sol::variadic_args va) {
	// First two arguments will be the name of the module and function, then the rest of the arguments will
	// be passed in through MIPS GPR registers.
	// This will be tricky for string args, we'll need to put them in some unallocated kernel space I guess.
	// Also we'll probably need to create specialized wrappers for calls that take structs like sceCtrlReadPositive etc.

	/*
	for (auto v : va) {
		if (v.is<int>()) {
			int i = v.as<int>();
		} else if (v.is<std::string>()) {
			std::string s = v.as<std::string>();
		}
	}
	*/
}

// After modifying code, this needs to be used.
void invalidate_cache(int start, int size) {
	if (!Memory::IsValidRange(start, size)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("invalidate_cache: bad range %08x + %08x", start, size));
		return;
	}

	u32 aligned = start & ~3;
	int alignedSize = (start + size - aligned + 3) & ~3;
	if (currentMIPS) {
		currentMIPS->InvalidateICache(aligned, alignedSize);
	}
}

void lua_memcpy(int dest, int src, int size) {
	if (!Memory::IsValidRange(dest, size) || !Memory::IsValidRange(src, size)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("memcpy: bad range dest %08x + %08x or src %08x + %08x", dest, size, src, size));
		return;
	}
	Memory::MemcpyUnchecked(dest, src, size);
}

void lua_memset(int dest, int byte, int size) {
	if (!Memory::IsValidRange(dest, size)) {
		g_lua.Print(LogLineType::Error, StringFromFormat("memset(%d): bad range dest %08x + %08x", (u8)byte, dest, size));
		return;
	}
	Memory::MemsetUnchecked(dest, byte, size);
}

void InitializeLuaContextForPPSSPP(sol::state &lua) {
	// Add stuff we want to add.
	lua.open_libraries(sol::lib::base);
	lua.open_libraries(sol::lib::table);
	lua.open_libraries(sol::lib::bit32);
	lua.open_libraries(sol::lib::string);
	lua.open_libraries(sol::lib::math);

	// Remove some stuff we don't want to expose.
	// TODO: The sandbox environmeent method would be better.
	sol::table globals = lua.globals();
	globals["dofile"] = sol::lua_nil;
	globals["load"] = sol::lua_nil;
	globals["loadstring"] = sol::lua_nil;
	globals["loadfile"] = sol::lua_nil;
	globals["collectgarbage"] = sol::lua_nil;
	globals["rawequal"] = sol::lua_nil;
	globals["rawset"] = sol::lua_nil;
	globals["rawget"] = sol::lua_nil;
	globals["setmetatable"] = sol::lua_nil;
	globals["debug"] = sol::lua_nil;
	globals["require"] = sol::lua_nil;

	extern const char *PPSSPP_GIT_VERSION;
	lua.set("ver", PPSSPP_GIT_VERSION);

	lua.set_function("print", &print);
	lua.set_function("debug", &debug);
	lua.set_function("info", &info);
	lua.set_function("warn", &warn);
	lua.set_function("error", &error);

	// lua.set("module_list", &module_list);

	// Memory accessors
	lua.set_function("r8", &r8);
	lua.set_function("rs8", &rs8);  // read signed 8-bit
	lua.set_function("w8", &w8);
	lua.set_function("r16", &r16);
	lua.set_function("rs16", &r16);  // read signed 16-bit
	lua.set_function("w16", &w16);
	lua.set_function("r32", &r32);
	lua.set_function("w32", &w32);
	lua.set_function("wf", &wf);
	lua.set_function("rf", &rf);

	// Memory scans
	lua.set_function("scan32", &scan32);

	// Register accessors
	lua.set_function("gpr", &gpr);
	lua.set_function("set_gpr", &set_gpr);

	// Data conversion utilities, bitcasts
	lua.set_function("bitcast_s32_to_float", &bitcast_s32_to_float);
	lua.set_function("bitcast_float_to_s32", &bitcast_float_to_s32);

	// System control (TODO: Should this be privileged to certain types of script?)
	lua.set_function("stop", &stop);
	lua.set_function("reset", &reset);

	// MIPS instruction utilities
	lua.set_function("asm", &assemble);

	// Test functions
	lua.set_function("get_strings", &get_strings);

	// not yet working
	lua.set_function("sys_call", &sys_call);

	// Memory tools
	lua.set_function("invalidate_cache", &invalidate_cache);
	lua.set_function("memcpy", &lua_memcpy);
	lua.set_function("memset", &lua_memset);

	// UI interactions
	lua.set_function("notify_info", [](const char *str, const char *id) {
		g_OSD.Show(OSDType::MESSAGE_INFO, str, 0.0f, strlen(id) == 0 ? nullptr : id);
	});
	lua.set_function("notify_warn", [](const char *str, const char *id) {
		g_OSD.Show(OSDType::MESSAGE_WARNING, str, 0.0f, strlen(id) == 0 ? nullptr : id);
	});
	lua.set_function("notify_error", [](const char *str, const char *id) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, str, 0.0f, strlen(id) == 0 ? nullptr : id);
	});
	lua.set_function("notify_success", [](const char *str, const char *id) {
		g_OSD.Show(OSDType::MESSAGE_SUCCESS, str, 0.0f, strlen(id) == 0 ? nullptr : id);
	});

	// Missing functions after studying Thirteen's plugins
	// sceIoDevctl("kemulator:", EMULATOR_DEVCTL__TOGGLE_FASTFORWARD, (void*)1, 0, NULL, 0);
	// Easy-to-use scan functions for hex patterns
	//

	// Functionality required to replace cwcheats
	// https://datacrystal.tcrf.net/wiki/CwCheat
	// https://github.com/raing3/psp-cheat-documentation/blob/master/cheat-devices/cwcheat.md
	// Multi-write (though can easily be replaced with loops, but cleaner to have a specific way)
	// Pointer commands and boolean commands are easily replaced with simple logic and r32/w32 etc.
	// Conditional and Multiple Skip types can be replaced with simple if statements and loops.
	// Button-peek functions are needed.
	// We don't properly support the "Pause" code type, I don't think?
	// Icache invalidation will not be automatic, unlike in cwcheats.

	// Initialize useful constants.
	lua["btn"] = lua.create_table_with(
		"SELECT", 0x00000001,
		"START", 0x00000008,
		"UP", 0x00000010,
		"RIGHT", 0x00000020,
		"DOWN", 0x00000040,
		"LEFT", 0x00000080,
		"LTRIGGER", 0x00000100,
		"RTRIGGER", 0x00000200,
		"TRIANGLE", 0x00001000,
		"CIRCLE", 0x00002000,
		"CROSS", 0x00004000,
		"SQUARE", 0x00008000,
		"HOME", 0x00010000,
		"HOLD", 0x00020000,
		"WLAN_UP", 0x00040000,
		"REMOTE", 0x00080000,
		"VOLUP", 0x00100000,
		"VOLDOWN", 0x00200000,
		"SCREEN", 0x00400000,
		"NOTE", 0x00800000,
		"DISC", 0x01000000,
		"MS", 0x02000000
	);

	// Load a function to dump values to console in a readable way.
	lua.script(R"(
function dump(value, indent, visited)
    indent = indent or ""
    visited = visited or {}

    local t = type(value)

    if t == "table" then
        if visited[value] then
            print(indent .. "{ -- already seen }")
            return
        end
        visited[value] = true

        print(indent .. "{")
        local nextIndent = indent .. "  "
        for k, v in pairs(value) do
            if type(v) == "table" then
                print(nextIndent .. tostring(k) .. " =")
                dump(v, nextIndent, visited)
            else
                print(nextIndent .. tostring(k) .. " = " .. tostring(v))
            end
        end
        print(indent .. "}")

    else
        print(indent .. tostring(value))
    end
end
)");
}

void LuaContext::Init() {
	_dbg_assert_(lua_ == nullptr);
	lua_.reset(new sol::state());

	InitializeLuaContextForPPSSPP(*lua_);
}

void LuaContext::Shutdown() {
	lua_.reset();
}

static const char *SolTypeToString(sol::type type) {
	switch (type) {
	case sol::type::none:        return "none";
	case sol::type::lua_nil:     return "nil";
	case sol::type::string:      return "string";
	case sol::type::number:      return "number";
	case sol::type::thread:      return "thread";
	case sol::type::boolean:     return "boolean";
	case sol::type::function:    return "function";
	case sol::type::userdata:    return "userdata";
	case sol::type::lightuserdata: return "lightuserdata";
	case sol::type::table:       return "table";
	case sol::type::poly:        return "poly"; // special variant type
	default:                     return "other";
	}
}

void LuaContext::Print(LogLineType type, std::string_view text) {
	INFO_LOG(Log::System, "%.*s", (int)text.size(), text.data());
}

void LuaInteractiveContext::Print(LogLineType type, std::string_view text) {
	lines_.push_back(LuaLogLine{ type, std::string(text)});
}

std::vector<std::string> LuaInteractiveContext::AutoComplete(std::string_view cmd) const {
	auto globals = GetGlobals();
	std::vector<std::string> candidates;
	for (auto &g : globals) {
		if (startsWithNoCase(g, cmd)) {
			candidates.push_back(g);
		}
	}
	return candidates;
}

std::vector<std::string> LuaInteractiveContext::GetGlobals() const {
	std::vector<std::string> globalStrings;
	sol::table globals = lua_->globals();

	for (auto& kv : globals) {
		sol::object key = kv.first;
		sol::object value = kv.second;

		if (value.is<sol::function>()) {
			globalStrings.push_back(key.as<std::string>());
		}
	}
	return globalStrings;
}

void LuaInteractiveContext::ExecuteConsoleCommand(std::string_view cmd) {
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
					double num = item.get<double>();
					// Check if it's an integer value
					if (std::floor(num) == num && num >= static_cast<double>(std::numeric_limits<int>::min()) &&
						num <= static_cast<double>(std::numeric_limits<int>::max())) {

						int int_val = static_cast<int>(num);
						lines_.push_back(LuaLogLine{
							LogLineType::Integer,
							StringFromFormat("%08x (%d)", int_val, int_val),
							int_val
							});
					} else {
						lines_.push_back(LuaLogLine{
							LogLineType::Float,
							StringFromFormat("%f", num),
							0,
							num
							});
					}
					break;
				}
				case sol::type::string:
				{
					// TODO: Linebreak multi-line strings.
					lines_.push_back(LuaLogLine{ LogLineType::String, item.get<std::string>() });
					break;
				}
				default:
				{
					// More complex object, let's call our lua function 'dump'.
					lua_->script("return dump(" + std::string(cmd) + ")");
					//	lines_.push_back(LuaLogLine{LogLineType::String, "(other result)"});
					break;
				}
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
