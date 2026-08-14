// Minimal-state JSON writer. Consumes almost no memory
// apart from the string being built-up, which could easily be replaced
// with a file stream (although I've chosen not to do that just yet).
//
// Writes nicely 2-space indented output with correct comma-placement
// in arrays and dictionaries.
//
// Does not deal with encodings in any way.
//
// Zero dependencies apart from stdlib (if you remove the vhjson usage.)

#pragma once

#include <string>
#include <vector>
#include <sstream>

struct JsonNode;

namespace json {

class JsonWriter {
public:
	JsonWriter(int flags = NORMAL);
	~JsonWriter();
	void begin();
	void beginArray();
	void beginRaw();
	void end();
	void pushDict();
	void pushDict(const std::string &name);
	void pushArray();
	void pushArray(const std::string &name);
	void pop();
	void writeBool(bool value);
	void writeBool(const std::string &name, bool value);
	void writeInt(int value);
	void writeInt(const std::string &name, int value);
	void writeUint(uint32_t value);
	void writeUint(const std::string &name, uint32_t value);
	void writeFloat(double value);
	void writeFloat(const std::string &name, double value);
	void writeString(const std::string &value);
	void writeString(const std::string &name, const std::string &value);
	void writeRaw(const std::string &value);
	void writeRaw(const std::string &name, const std::string &value);
	void writeNull();
	void writeNull(const std::string &name);

	std::string str() const {
		return str_.str();
	}

	std::string flush() {
		std::string result = str_.str();
		str_.str("");
		return result;
	}

	// RAII helpers: push in the constructor, pop in the destructor. Prefer these over manually
	// paired pushDict()/pushArray() + pop() calls - a forgotten or early-returned pop() silently
	// produces malformed JSON (extra or missing closing brace/bracket) rather than a compile or
	// even runtime error, and that gets easy to miss once nesting goes a few levels deep (e.g.
	// an array of per-item dicts, each with their own nested array).
	class DictScope {
	public:
		explicit DictScope(JsonWriter &w) : w_(w) { w_.pushDict(); }
		DictScope(JsonWriter &w, const std::string &name) : w_(w) { w_.pushDict(name); }
		~DictScope() { w_.pop(); }
		DictScope(const DictScope &) = delete;
		DictScope &operator=(const DictScope &) = delete;
	private:
		JsonWriter &w_;
	};
	class ArrayScope {
	public:
		explicit ArrayScope(JsonWriter &w) : w_(w) { w_.pushArray(); }
		ArrayScope(JsonWriter &w, const std::string &name) : w_(w) { w_.pushArray(name); }
		~ArrayScope() { w_.pop(); }
		ArrayScope(const ArrayScope &) = delete;
		ArrayScope &operator=(const ArrayScope &) = delete;
	private:
		JsonWriter &w_;
	};

	enum {
		NORMAL = 0,
		PRETTY = 1,
	};

private:
	const char *indent(int n) const;
	const char *comma() const;
	const char *arrayComma() const;
	const char *indent() const;
	const char *arrayIndent() const;
	void writeEscapedString(const std::string &s);

	enum BlockType {
		ARRAY,
		DICT,
		RAW,
	};
	struct StackEntry {
		StackEntry(BlockType t) : type(t), first(true) {}
		BlockType type;
		bool first;
	};
	std::vector<StackEntry> stack_;
	std::ostringstream str_;
	bool pretty_;
};

std::string json_stringify(const JsonNode *json);

}  // namespace json
