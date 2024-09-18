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
