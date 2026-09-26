#pragma once
/*
 * config.hpp - minimal JSON parser for config.json, no external deps.
 *
 * This is deliberately small — a variant-style JsonValue (null, bool,
 * number, string, array, object) plus a recursive-descent parser. It's
 * not meant as a general-purpose JSON library; it's just enough to read
 * the launcher's config.json (a handful of nested objects with numbers,
 * bools, strings, and flat arrays of numbers for colors).
 *
 * Usage:
 *   auto result = parse_json_file("config.json");
 *   if (result.ok) {
 *       int fps = result.value["fire"]["fps"].as_int(15);
 *   }
 */

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>
#include <string>
#include <vector>

enum class JsonType { Null, Bool, Number, String, Array, Object };

struct JsonValue {
    JsonType type = JsonType::Null;
    bool                                boolValue = false;
    double                              numValue  = 0.0;
    std::string                         strValue;
    std::vector<JsonValue>              arrValue;
    std::map<std::string, JsonValue>    objValue;

    // Object member access — returns a Null JsonValue if the key is
    // absent, so chaining ["a"]["b"].as_int(default) never throws.
    const JsonValue &operator[](const std::string &key) const {
        static const JsonValue null_value{};
        if (type != JsonType::Object) return null_value;
        auto it = objValue.find(key);
        return (it != objValue.end()) ? it->second : null_value;
    }

    bool has(const std::string &key) const {
        return type == JsonType::Object && objValue.count(key) > 0;
    }

    bool is_null() const { return type == JsonType::Null; }

    int as_int(int fallback) const {
        return (type == JsonType::Number) ? (int)numValue : fallback;
    }
    float as_float(float fallback) const {
        return (type == JsonType::Number) ? (float)numValue : fallback;
    }
    bool as_bool(bool fallback) const {
        return (type == JsonType::Bool) ? boolValue : fallback;
    }
    std::string as_string(const std::string &fallback) const {
        return (type == JsonType::String) ? strValue : fallback;
    }

    // For a 3-element [r,g,b] array; returns fallback if missing/malformed.
    struct RGBTriple { int r, g, b; };
    RGBTriple as_rgb(RGBTriple fallback) const {
        if (type != JsonType::Array || arrValue.size() != 3) return fallback;
        return {
            arrValue[0].as_int(fallback.r),
            arrValue[1].as_int(fallback.g),
            arrValue[2].as_int(fallback.b),
        };
    }
};

struct JsonParseResult {
    bool      ok = false;
    JsonValue value;
    std::string error;
};

namespace json_detail {

class Parser {
public:
    explicit Parser(const std::string &text) : s(text), i(0), n(text.size()) {}

    JsonValue parse(std::string &error) {
        skip_ws();
        JsonValue v = parse_value(error);
        if (!error.empty()) return v;
        skip_ws();
        if (i != n) error = "trailing data after JSON value";
        return v;
    }

private:
    const std::string &s;
    size_t i, n;

    void skip_ws() {
        while (i < n && std::isspace((unsigned char)s[i])) i++;
    }

    char peek() { return i < n ? s[i] : '\0'; }

    bool consume(char c) {
        if (peek() == c) { i++; return true; }
        return false;
    }

    JsonValue parse_value(std::string &error) {
        skip_ws();
        char c = peek();
        if (c == '{') return parse_object(error);
        if (c == '[') return parse_array(error);
        if (c == '"') return parse_string(error);
        if (c == 't' || c == 'f') return parse_bool(error);
        if (c == 'n') return parse_null(error);
        if (c == '-' || std::isdigit((unsigned char)c)) return parse_number(error);
        error = "unexpected character at position " + std::to_string(i);
        return JsonValue{};
    }

    JsonValue parse_object(std::string &error) {
        JsonValue v; v.type = JsonType::Object;
        i++;   // consume '{'
        skip_ws();
        if (consume('}')) return v;
        while (true) {
            skip_ws();
            if (peek() != '"') { error = "expected string key at position " + std::to_string(i); return v; }
            JsonValue key = parse_string(error);
            if (!error.empty()) return v;
            skip_ws();
            if (!consume(':')) { error = "expected ':' at position " + std::to_string(i); return v; }
            JsonValue val = parse_value(error);
            if (!error.empty()) return v;
            v.objValue[key.strValue] = val;
            skip_ws();
            if (consume(',')) continue;
            if (consume('}')) break;
            error = "expected ',' or '}' at position " + std::to_string(i);
            return v;
        }
        return v;
    }

    JsonValue parse_array(std::string &error) {
        JsonValue v; v.type = JsonType::Array;
        i++;   // consume '['
        skip_ws();
        if (consume(']')) return v;
        while (true) {
            JsonValue val = parse_value(error);
            if (!error.empty()) return v;
            v.arrValue.push_back(val);
            skip_ws();
            if (consume(',')) continue;
            if (consume(']')) break;
            error = "expected ',' or ']' at position " + std::to_string(i);
            return v;
        }
        return v;
    }

    JsonValue parse_string(std::string &error) {
        JsonValue v; v.type = JsonType::String;
        i++;   // consume opening '"'
        std::string out;
        while (i < n && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < n) {
                char esc = s[i++];
                switch (esc) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '"': out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/';  break;
                    default: out += esc; break;   // \uXXXX etc. not needed for this config
                }
            } else {
                out += c;
            }
        }
        if (i >= n) { error = "unterminated string"; return v; }
        i++;   // consume closing '"'
        v.strValue = out;
        return v;
    }

    JsonValue parse_bool(std::string &error) {
        JsonValue v; v.type = JsonType::Bool;
        if (s.compare(i, 4, "true") == 0) { v.boolValue = true; i += 4; return v; }
        if (s.compare(i, 5, "false") == 0) { v.boolValue = false; i += 5; return v; }
        error = "invalid literal at position " + std::to_string(i);
        return v;
    }

    JsonValue parse_null(std::string &error) {
        JsonValue v; v.type = JsonType::Null;
        if (s.compare(i, 4, "null") == 0) { i += 4; return v; }
        error = "invalid literal at position " + std::to_string(i);
        return v;
    }

    JsonValue parse_number(std::string &error) {
        JsonValue v; v.type = JsonType::Number;
        size_t start = i;
        if (peek() == '-') i++;
        while (i < n && std::isdigit((unsigned char)s[i])) i++;
        if (peek() == '.') {
            i++;
            while (i < n && std::isdigit((unsigned char)s[i])) i++;
        }
        if (peek() == 'e' || peek() == 'E') {
            i++;
            if (peek() == '+' || peek() == '-') i++;
            while (i < n && std::isdigit((unsigned char)s[i])) i++;
        }
        std::string numStr = s.substr(start, i - start);
        char *end;
        v.numValue = std::strtod(numStr.c_str(), &end);
        if (end == numStr.c_str()) error = "invalid number at position " + std::to_string(start);
        return v;
    }
};

}   // namespace json_detail

inline JsonParseResult parse_json_file(const std::string &path) {
    JsonParseResult result;

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        result.error = "could not open file";
        return result;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > 1 << 20) {
        fclose(f);
        result.error = "file too large or unreadable";
        return result;
    }
    std::string text(size, '\0');
    size_t read = fread(&text[0], 1, size, f);
    fclose(f);
    text.resize(read);

    json_detail::Parser parser(text);
    std::string error;
    JsonValue value = parser.parse(error);
    if (!error.empty()) {
        result.error = error;
        return result;
    }
    result.ok    = true;
    result.value = value;
    return result;
}
