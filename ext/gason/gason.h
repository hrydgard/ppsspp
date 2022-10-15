#pragma once

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

enum JsonTag {
    JSON_NUMBER = 0,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL = 0xF
};

struct JsonNode;

struct JsonValue {
    union {
        uint64_t ival_;
        double fval_;
    };
    uint8_t tag_ = JSON_NULL;

    JsonValue(double x)
        : fval_(x), tag_(JSON_NUMBER) {
    }
    JsonValue(JsonTag tag = JSON_NULL, void *payload = nullptr)
        : ival_((uintptr_t)payload), tag_(tag) {
    }
    bool isDouble() const {
        return tag_ == JSON_NUMBER;
    }
    JsonTag getTag() const {
        return JsonTag(tag_);
    }
    uint64_t getPayload() const {
        assert(!isDouble());
        return ival_;
    }
    double toNumber() const {
        assert(getTag() == JSON_NUMBER);
        return fval_;
    }
    char *toString() const {
        assert(getTag() == JSON_STRING);
        return (char *)getPayload();
    }
    JsonNode *toNode() const {
        assert(getTag() == JSON_ARRAY || getTag() == JSON_OBJECT);
        return (JsonNode *)getPayload();
    }
};

struct JsonNode {
    JsonValue value;
    JsonNode *next;
    char *key;
};

struct JsonIterator {
    JsonNode *p;

    void operator++() {
        p = p->next;
    }
    bool operator!=(const JsonIterator &x) const {
        return p != x.p;
    }
    JsonNode *operator*() const {
        return p;
    }
    JsonNode *operator->() const {
        return p;
    }
};

inline JsonIterator begin(JsonValue o) {
    return JsonIterator{o.toNode()};
}
inline JsonIterator end(JsonValue) {
    return JsonIterator{nullptr};
}

#define JSON_ERRNO_MAP(XX)                           \
    XX(OK, "ok")                                     \
    XX(BAD_NUMBER, "bad number")                     \
    XX(BAD_STRING, "bad string")                     \
    XX(BAD_IDENTIFIER, "bad identifier")             \
    XX(STACK_OVERFLOW, "stack overflow")             \
    XX(STACK_UNDERFLOW, "stack underflow")           \
    XX(MISMATCH_BRACKET, "mismatch bracket")         \
    XX(UNEXPECTED_CHARACTER, "unexpected character") \
    XX(UNQUOTED_KEY, "unquoted key")                 \
    XX(BREAKING_BAD, "breaking bad")                 \
    XX(ALLOCATION_FAILURE, "allocation failure")

enum JsonErrno {
#define XX(no, str) JSON_##no,
    JSON_ERRNO_MAP(XX)
#undef XX
};

const char *jsonStrError(int err);

class JsonAllocator {
    struct Zone {
        Zone *next;
        size_t used;
    } *head;

public:
    JsonAllocator() : head(nullptr) {};
    JsonAllocator(const JsonAllocator &) = delete;
    JsonAllocator &operator=(const JsonAllocator &) = delete;
    JsonAllocator(JsonAllocator &&x) noexcept : head(x.head) {
        x.head = nullptr;
    }
    JsonAllocator &operator=(JsonAllocator &&x) noexcept {
        head = x.head;
        x.head = nullptr;
        return *this;
    }
    ~JsonAllocator() {
        deallocate();
    }
    void *allocate(size_t size);
    void deallocate();
};

int jsonParse(char *str, char **endptr, JsonValue *value, JsonAllocator &allocator);
