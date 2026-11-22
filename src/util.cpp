// util.cpp
#include "util.hpp"

#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <vector>

#include <winver.h>

namespace cm {

namespace {

std::wstring FormatSystemTimeParts(const SYSTEMTIME& st) {
    wchar_t buf[64];
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

}  // namespace

std::wstring FormatSystemTimeUtc(const SYSTEMTIME& st) {
    return FormatSystemTimeParts(st);
}

std::wstring FormatFileTimeUtc(FILETIME ft) {
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) return L"";
    return FormatSystemTimeParts(st);
}

std::wstring NowUtcIso() {
    SYSTEMTIME st{};
    GetSystemTime(&st);
    return FormatSystemTimeParts(st);
}

std::wstring FormatWmiDateTime(const std::wstring& wmi) {
    // Expected: "20240101120000.000000+000" (at least 14 digits).
    if (wmi.size() < 14) return wmi;
    for (size_t i = 0; i < 14; ++i) {
        if (wmi[i] < L'0' || wmi[i] > L'9') return wmi;
    }
    std::wstring out;
    out.reserve(32);
    out += wmi.substr(0, 4) + L"-" + wmi.substr(4, 2) + L"-" + wmi.substr(6, 2) +
           L"T" + wmi.substr(8, 2) + L":" + wmi.substr(10, 2) + L":" + wmi.substr(12, 2);
    // Append offset if present ("+000" / "-300" = minutes).
    if (wmi.size() >= 22) {
        wchar_t sign = wmi[21];
        if (sign == L'+' || sign == L'-') {
            std::wstring off = wmi.substr(22, 3);
            bool digits = off.size() == 3;
            for (wchar_t c : off) if (c < L'0' || c > L'9') digits = false;
            if (digits) {
                int mins = std::stoi(off);
                wchar_t buf[16];
                swprintf_s(buf, L"%c%02d:%02d", sign, mins / 60, mins % 60);
                out += buf;
                return out;
            }
        }
    }
    out += L"Z";
    return out;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    std::filesystem::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

std::wstring WinErrorString(DWORD err) {
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, err, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring out;
    if (n && msg) {
        out.assign(msg, n);
        // Trim trailing CRLF.
        while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n')) out.pop_back();
    } else {
        wchar_t buf[64];
        swprintf_s(buf, L"Win32 error %lu", static_cast<unsigned long>(err));
        out = buf;
    }
    if (msg) LocalFree(msg);
    return out;
}

std::wstring ExtractExePath(const std::wstring& cmdline) {
    std::wstring s = cmdline;
    // Trim leading whitespace.
    size_t start = s.find_first_not_of(L" \t");
    if (start == std::wstring::npos) return L"";
    s = s.substr(start);
    if (s.empty()) return L"";

    if (s[0] == L'"') {
        size_t end = s.find(L'"', 1);
        if (end != std::wstring::npos) return s.substr(1, end - 1);
        return s.substr(1);
    }
    // Unquoted: find ".exe" (case-insensitive) and cut there.
    std::wstring lower = s;
    for (wchar_t& c : lower) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    size_t exe = lower.find(L".exe");
    if (exe != std::wstring::npos) return s.substr(0, exe + 4);
    // Fallback: first whitespace-delimited token.
    size_t sp = s.find_first_of(L" \t");
    if (sp != std::wstring::npos) return s.substr(0, sp);
    return s;
}

std::wstring GetFileCompanyName(const std::wstring& exePath) {
    if (exePath.empty()) return L"";
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), &handle);
    if (size == 0) return L"";
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(exePath.c_str(), 0, size, buf.data())) return L"";

    // Translation table -> language + codepage.
    struct LangCp { WORD lang; WORD cp; };
    LangCp* trans = nullptr;
    UINT transLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<LPVOID*>(&trans), &transLen) || transLen < 4) {
        return L"";
    }
    // Try each translation until CompanyName is found.
    size_t count = transLen / sizeof(LangCp);
    for (size_t i = 0; i < count; ++i) {
        wchar_t sub[128];
        swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\CompanyName",
                   trans[i].lang, trans[i].cp);
        LPWSTR val = nullptr;
        UINT valLen = 0;
        if (VerQueryValueW(buf.data(), sub, reinterpret_cast<LPVOID*>(&val), &valLen) && val && valLen > 0) {
            return std::wstring(val, valLen - 1);  // exclude NUL
        }
    }
    return L"";
}

bool ContainsCi(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto lower = [](wchar_t c) -> wchar_t {
        if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
        return c;
    };
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (lower(haystack[i + j]) != lower(needle[j])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

}  // namespace cm
