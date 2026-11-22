// fs.cpp — fs.list: FindFirstFileW directory enumeration.
#include "fs.hpp"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <vector>

#include "util.hpp"

namespace cm {

namespace {

// Prepend \\?\ for long paths (MAX_PATH+). Relative paths are left alone.
std::wstring LongPath(const std::wstring& p) {
    if (p.size() <= 240 || p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    if (p.size() >= 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/'))
        return L"\\\\?\\" + p;
    return p;
}

JsonValue MakeEntry(const std::wstring& fullPath, const std::wstring& name,
                    ULONGLONG size, FILETIME mtime, bool isDir) {
    JsonValue e = JsonValue::object();
    e.set("name", JsonValue::stringWide(name));
    e.set("path", JsonValue::stringWide(fullPath));
    e.set("size", JsonValue::unsignedInteger(size));
    e.set("mtime_utc", JsonValue::stringWide(FormatFileTimeUtc(mtime)));
    e.set("is_dir", JsonValue::boolean(isDir));
    return e;
}

DWORD EnumerateDir(const std::wstring& dir, bool recursive,
                   std::vector<JsonValue>& out) {
    std::wstring pattern = LongPath(dir) + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
    }
    UniqueFindHandle guard(h);
    DWORD firstError = ERROR_SUCCESS;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        std::wstring full = dir + L"\\" + fd.cFileName;
        out.push_back(MakeEntry(full, std::wstring(fd.cFileName), size, fd.ftLastWriteTime, isDir));

        // Recurse, but never follow reparse points (junctions/symlinks).
        if (recursive && isDir &&
            !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            DWORD error = EnumerateDir(full, recursive, out);
            if (firstError == ERROR_SUCCESS) firstError = error;
        }
    } while (FindNextFileW(h, &fd));
    DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES && firstError == ERROR_SUCCESS) firstError = error;
    return firstError;
}

}  // namespace

JsonValue CollectFsList(const std::vector<std::wstring>& paths, bool recursive) {
    JsonValue list = JsonValue::array();

    for (const std::wstring& rawPath : paths) {
        JsonValue root = JsonValue::object();
        root.set("path", JsonValue::stringWide(rawPath));

        DWORD attrs = GetFileAttributesW(LongPath(rawPath).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            root.set("exists", JsonValue::boolean(false));
            root.set("error", JsonValue::stringWide(WinErrorString(GetLastError())));
            root.set("entries", JsonValue::array());
            list.push(std::move(root));
            continue;
        }

        bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        root.set("exists", JsonValue::boolean(true));
        root.set("is_dir", JsonValue::boolean(isDir));

        std::vector<JsonValue> entries;
        if (isDir) {
            DWORD error = EnumerateDir(rawPath, recursive, entries);
            if (error != ERROR_SUCCESS)
                root.set("error", JsonValue::stringWide(WinErrorString(error)));
        } else {
            // Single file: report it as its own entry.
            HANDLE h = CreateFileW(LongPath(rawPath).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            ULONGLONG size = 0;
            FILETIME mtime{};
            if (h && h != INVALID_HANDLE_VALUE) {
                UniqueHandle guard(h);
                LARGE_INTEGER li{};
                if (GetFileSizeEx(h, &li)) size = static_cast<ULONGLONG>(li.QuadPart);
                GetFileTime(h, nullptr, nullptr, &mtime);
            }
            size_t slash = rawPath.find_last_of(L"\\/");
            std::wstring name = (slash == std::wstring::npos) ? rawPath : rawPath.substr(slash + 1);
            entries.push_back(MakeEntry(rawPath, name, size, mtime, false));
        }

        std::sort(entries.begin(), entries.end(), [](const JsonValue& a, const JsonValue& b) {
            const JsonValue* na = a.find("name");
            const JsonValue* nb = b.find("name");
            std::string sa = na && na->isString() ? na->asString() : "";
            std::string sb = nb && nb->isString() ? nb->asString() : "";
            return sa < sb;
        });

        JsonValue arr = JsonValue::array();
        for (auto& e : entries) arr.push(std::move(e));
        root.set("entries", std::move(arr));
        list.push(std::move(root));
    }
    return list;
}

}  // namespace cm
