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

#include <string>
#include <vector>
#include <sstream>

class JsonWriter {
public:
	JsonWriter(int flags = NORMAL);
	~JsonWriter();
	void begin();
	void beginArray();
	void beginRaw();
	void end();
	void pushDict();
	void pushDict(const char *name);
	void pushArray();
	void pushArray(const char *name);
	void pop();
	void writeBool(bool value);
	void writeBool(const char *name, bool value);
	void writeInt(int value);
	void writeInt(const char *name, int value);
	void writeFloat(double value);
	void writeFloat(const char *name, double value);
	void writeString(const char *value);
	void writeString(const char *name, const char *value);
	void writeString(const std::string &value) {
		writeString(value.c_str());
	}
	void writeString(const char *name, const std::string &value) {
		writeString(name, value.c_str());
	}
	void writeString(const std::string &name, const std::string &value) {
		writeString(name.c_str(), value.c_str());
	}
	void writeRaw(const char *value);
	void writeRaw(const char *name, const char *value);
	void writeRaw(const std::string &value) {
		writeRaw(value.c_str());
	}
	void writeRaw(const char *name, const std::string &value) {
		writeRaw(name, value.c_str());
	}
	void writeRaw(const std::string &name, const std::string &value) {
		writeRaw(name.c_str(), value.c_str());
	}

	std::string str() const {
		return str_.str();
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
	void writeEscapedString(const char *s);

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

struct JsonNode;
std::string json_stringify(const JsonNode *json);
