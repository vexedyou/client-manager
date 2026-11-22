// config.hpp — minimal JSON config parser (subset: objects, arrays, strings,
// numbers, booleans, null). Throws std::runtime_error on parse failure.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"

namespace cm {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    JsonValue parse() {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (pos_ != text_.size()) fail("trailing characters after JSON value");
        return v;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& msg) const {
        throw std::runtime_error("config JSON parse error at offset " +
                                 std::to_string(pos_) + ": " + msg);
    }

    void skipWs() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    char take() {
        if (pos_ >= text_.size()) fail("unexpected end of input");
        return text_[pos_++];
    }

    void expect(char c) {
        if (take() != c) fail(std::string("expected '") + c + "'");
    }

    JsonValue parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return JsonValue::stringUtf8(parseString());
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { parseLiteral("null"); return JsonValue::nullValue(); }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail("unexpected character");
    }

    JsonValue parseObject() {
        expect('{');
        JsonValue obj = JsonValue::object();
        skipWs();
        if (peek() == '}') { ++pos_; return obj; }
        while (true) {
            skipWs();
            if (peek() != '"') fail("expected object key string");
            std::string key = parseString();
            skipWs();
            expect(':');
            JsonValue val = parseValue();
            obj.set(key, std::move(val));
            skipWs();
            char c = take();
            if (c == '}') break;
            if (c != ',') fail("expected ',' or '}' in object");
        }
        return obj;
    }

    JsonValue parseArray() {
        expect('[');
        JsonValue arr = JsonValue::array();
        skipWs();
        if (peek() == ']') { ++pos_; return arr; }
        while (true) {
            JsonValue val = parseValue();
            arr.push(std::move(val));
            skipWs();
            char c = take();
            if (c == ']') break;
            if (c != ',') fail("expected ',' or ']' in array");
        }
        return arr;
    }

    uint32_t parseHex4() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = take();
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return v;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) fail("unterminated string");
            char c = take();
            if (c == '"') break;
            if (c == '\\') {
                char e = take();
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        uint32_t cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // Expect a low surrogate.
                            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                                pos_ += 2;
                                uint32_t lo = parseHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    fail("bad low surrogate");
                                }
                            } else {
                                fail("unpaired high surrogate");
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("bad escape");
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string");
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JsonValue parseBool() {
        if (text_.compare(pos_, 4, "true") == 0) { pos_ += 4; return JsonValue::boolean(true); }
        if (text_.compare(pos_, 5, "false") == 0) { pos_ += 5; return JsonValue::boolean(false); }
        fail("bad literal");
    }

    void parseLiteral(const char* lit) {
        size_t n = std::string(lit).size();
        if (text_.compare(pos_, n, lit) != 0) fail("bad literal");
        pos_ += n;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        bool isDouble = false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            isDouble = true;
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            isDouble = true;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        std::string tok = text_.substr(start, pos_ - start);
        if (tok.empty() || tok == "-") fail("bad number");
        try {
            if (isDouble) return JsonValue::number(std::stod(tok));
            // Integers: prefer signed, fall back to unsigned on overflow.
            try {
                return JsonValue::integer(std::stoll(tok));
            } catch (...) {
                return JsonValue::unsignedInteger(std::stoull(tok));
            }
        } catch (...) {
            fail("bad number");
        }
    }
};

inline JsonValue ParseJson(const std::string& text) {
    JsonParser p(text);
    return p.parse();
}

// Config helpers -----------------------------------------------------------

struct Config {
    std::vector<std::wstring> fsPaths;
    bool fsRecursive = false;
    bool includeSysInfo = true;
    bool includeProcList = true;
    bool includeNetConn = true;
    bool includeSvcList = true;
    bool includePersistCheck = true;
    bool includeSoftwareList = true;
    bool includeFsList = true;
};

inline Config ParseConfig(const JsonValue& root) {
    Config cfg;
    if (!root.isObject()) return cfg;

    auto getPaths = [&](const char* key) {
        const JsonValue* v = root.find(key);
        if (v && v->isArray()) {
            for (const JsonValue& e : v->asArray()) {
                if (e.isString()) cfg.fsPaths.push_back(Utf8ToWide(e.asString()));
            }
        }
    };
    getPaths("fs_paths");
    if (cfg.fsPaths.empty()) getPaths("paths");

    auto getBool = [&](const char* key, bool& dst) {
        const JsonValue* v = root.find(key);
        if (v && v->isBool()) dst = v->asBool();
    };
    getBool("fs_recursive", cfg.fsRecursive);
    getBool("recursive", cfg.fsRecursive);

    const JsonValue* inc = root.find("include");
    if (inc && inc->isObject()) {
        auto ib = [&](const char* key, bool& dst) {
            const JsonValue* v = inc->find(key);
            if (v && v->isBool()) dst = v->asBool();
        };
        ib("sys_info", cfg.includeSysInfo);
        ib("proc_list", cfg.includeProcList);
        ib("net_conn", cfg.includeNetConn);
        ib("svc_list", cfg.includeSvcList);
        ib("persist_check", cfg.includePersistCheck);
        ib("software_list", cfg.includeSoftwareList);
        ib("fs_list", cfg.includeFsList);
    }
    return cfg;
}

}  // namespace cm
