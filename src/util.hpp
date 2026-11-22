// util.hpp — shared Windows helpers: time formatting, file I/O, version-info
// company lookup, exe-path extraction, RAII handle wrappers.
#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <type_traits>

#include "json.hpp"

namespace cm {

// RAII wrappers (CloseHandle / RegCloseKey / CloseServiceHandle).
struct HandleDeleter {
    void operator()(HANDLE h) const {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

struct FindHandleDeleter {
    void operator()(HANDLE h) const {
        if (h && h != INVALID_HANDLE_VALUE) FindClose(h);
    }
};
using UniqueFindHandle = std::unique_ptr<void, FindHandleDeleter>;

struct RegKeyDeleter {
    void operator()(HKEY h) const {
        if (h) RegCloseKey(h);
    }
};
using UniqueRegKey = std::unique_ptr<std::remove_pointer<HKEY>::type, RegKeyDeleter>;

struct ServiceHandleDeleter {
    void operator()(SC_HANDLE h) const {
        if (h) CloseServiceHandle(h);
    }
};
using UniqueServiceHandle = std::unique_ptr<std::remove_pointer<SC_HANDLE>::type, ServiceHandleDeleter>;

// Time formatting (all UTC, ISO-8601 "YYYY-MM-DDTHH:MM:SSZ").
std::wstring FormatFileTimeUtc(FILETIME ft);
std::wstring FormatSystemTimeUtc(const SYSTEMTIME& st);
std::wstring NowUtcIso();
// WMI datetime "yyyyMMddHHmmss.mmmmmm+zzz" -> ISO-8601 with offset.
std::wstring FormatWmiDateTime(const std::wstring& wmi);

// File I/O (UTF-8 bytes). ReadFileBytes returns false on failure.
bool ReadFileBytes(const std::wstring& path, std::string& out);
bool WriteFileBytes(const std::wstring& path, const std::string& data);

// FormatMessageW for a Win32 error code.
std::wstring WinErrorString(DWORD err);

// Extract the executable path from a command line / service binary path
// (handles quoted paths and trailing arguments).
std::wstring ExtractExePath(const std::wstring& cmdline);

// CompanyName from the file version resource ("" if unavailable).
std::wstring GetFileCompanyName(const std::wstring& exePath);

// Case-insensitive substring test.
bool ContainsCi(const std::wstring& haystack, const std::wstring& needle);

}  // namespace cm
