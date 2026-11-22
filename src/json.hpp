// json.hpp — minimal JSON value + writer (header-only, no external deps).
// Strings are stored as UTF-8. Wide strings are converted manually so this
// header stays portable and does not depend on windows.h.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace cm {

// Convert UTF-16 (std::wstring) to UTF-8.
inline std::string WideToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size() * 2);
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        // Handle surrogate pairs.
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            uint32_t lo = static_cast<uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
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
    return out;
}

// Convert UTF-8 to UTF-16 (std::wstring). Invalid sequences become U+FFFD.
inline std::wstring Utf8ToWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) { cp = c & 0x07; len = 4; }
        else { cp = 0xFFFD; len = 1; }
        if (len > 1) {
            bool ok = true;
            for (size_t k = 1; k < len; ++k) {
                unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (!ok) { cp = 0xFFFD; len = 1; }
        }
        i += len;
        if (cp < 0x10000) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

class JsonValue {
public:
    enum class Type { Null, Bool, Int, UInt, Double, String, Array, Object };

    JsonValue() = default;

    static JsonValue nullValue() { return JsonValue(); }
    static JsonValue boolean(bool b) { JsonValue v; v.type_ = Type::Bool; v.bool_ = b; return v; }
    static JsonValue integer(int64_t i) { JsonValue v; v.type_ = Type::Int; v.int_ = i; return v; }
    static JsonValue unsignedInteger(uint64_t u) { JsonValue v; v.type_ = Type::UInt; v.uint_ = u; return v; }
    static JsonValue number(double d) { JsonValue v; v.type_ = Type::Double; v.double_ = d; return v; }
    static JsonValue stringUtf8(const std::string& s) { JsonValue v; v.type_ = Type::String; v.string_ = s; return v; }
    static JsonValue stringWide(const std::wstring& w) { return stringUtf8(WideToUtf8(w)); }
    static JsonValue array() { JsonValue v; v.type_ = Type::Array; return v; }
    static JsonValue object() { JsonValue v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isInt() const { return type_ == Type::Int; }
    bool isUInt() const { return type_ == Type::UInt; }
    bool isDouble() const { return type_ == Type::Double; }
    bool isNumber() const { return type_ == Type::Int || type_ == Type::UInt || type_ == Type::Double; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const { return bool_; }
    int64_t asInt() const {
        if (type_ == Type::Int) return int_;
        if (type_ == Type::UInt) return static_cast<int64_t>(uint_);
        if (type_ == Type::Double) return static_cast<int64_t>(double_);
        return 0;
    }
    uint64_t asUInt() const {
        if (type_ == Type::UInt) return uint_;
        if (type_ == Type::Int) return static_cast<uint64_t>(int_);
        if (type_ == Type::Double) return static_cast<uint64_t>(double_);
        return 0;
    }
    double asDouble() const {
        if (type_ == Type::Double) return double_;
        if (type_ == Type::Int) return static_cast<double>(int_);
        if (type_ == Type::UInt) return static_cast<double>(uint_);
        return 0.0;
    }
    const std::string& asString() const { return string_; }
    const std::vector<JsonValue>& asArray() const { return array_; }
    const std::vector<std::pair<std::string, JsonValue>>& asObject() const { return object_; }

    // Object lookup (nullptr if absent / not an object).
    const JsonValue* find(const std::string& key) const {
        if (type_ != Type::Object) return nullptr;
        for (const auto& kv : object_) {
            if (kv.first == key) return &kv.second;
        }
        return nullptr;
    }

    void push(JsonValue v) { array_.push_back(std::move(v)); }
    void set(const std::string& key, JsonValue v) { object_.emplace_back(key, std::move(v)); }

    std::string dump(int indent = 2) const {
        std::string out;
        dumpTo(out, 0, indent);
        out.push_back('\n');
        return out;
    }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    int64_t int_ = 0;
    uint64_t uint_ = 0;
    double double_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<std::pair<std::string, JsonValue>> object_;

    static void appendEscaped(std::string& out, const std::string& s) {
        out.push_back('"');
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        const char* hex = "0123456789abcdef";
                        out += "\\u00";
                        out.push_back(hex[(c >> 4) & 0xF]);
                        out.push_back(hex[c & 0xF]);
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
        out.push_back('"');
    }

    void dumpTo(std::string& out, int level, int indent) const {
        switch (type_) {
            case Type::Null: out += "null"; break;
            case Type::Bool: out += bool_ ? "true" : "false"; break;
            case Type::Int: out += std::to_string(int_); break;
            case Type::UInt: out += std::to_string(uint_); break;
            case Type::Double: {
                // Use a fixed, locale-independent rendering.
                char buf[64];
                // %g keeps integers compact; JSON allows this form.
                snprintf(buf, sizeof(buf), "%.17g", double_);
                out += buf;
                break;
            }
            case Type::String: appendEscaped(out, string_); break;
            case Type::Array: {
                if (array_.empty()) { out += "[]"; break; }
                out += "[\n";
                for (size_t i = 0; i < array_.size(); ++i) {
                    out.append(static_cast<size_t>(level + 1) * static_cast<size_t>(indent), ' ');
                    array_[i].dumpTo(out, level + 1, indent);
                    if (i + 1 < array_.size()) out.push_back(',');
                    out.push_back('\n');
                }
                out.append(static_cast<size_t>(level) * static_cast<size_t>(indent), ' ');
                out.push_back(']');
                break;
            }
            case Type::Object: {
                if (object_.empty()) { out += "{}"; break; }
                out += "{\n";
                for (size_t i = 0; i < object_.size(); ++i) {
                    out.append(static_cast<size_t>(level + 1) * static_cast<size_t>(indent), ' ');
                    appendEscaped(out, object_[i].first);
                    out += ": ";
                    object_[i].second.dumpTo(out, level + 1, indent);
                    if (i + 1 < object_.size()) out.push_back(',');
                    out.push_back('\n');
                }
                out.append(static_cast<size_t>(level) * static_cast<size_t>(indent), ' ');
                out.push_back('}');
                break;
            }
        }
    }
};

}  // namespace cm
