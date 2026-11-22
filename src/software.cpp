// software.cpp — software.list from registry uninstall keys.
#include "software.hpp"

#include <windows.h>

#include <algorithm>
#include <vector>

#include "util.hpp"

namespace cm {

namespace {

std::wstring ReadValueString(HKEY h, const wchar_t* value) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(h, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return L"";
    if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
    DWORD dataSize = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
    if (RegQueryValueExW(h, value, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf.data()), &dataSize) != ERROR_SUCCESS) {
        return L"";
    }
    std::wstring out(buf.data());
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

void EnumUninstall(HKEY hive, const wchar_t* subkey, const wchar_t* hiveLabel,
                   std::vector<JsonValue>& out) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return;
    UniqueRegKey guard(h);

    DWORD idx = 0;
    while (true) {
        wchar_t keyName[1024];
        DWORD keyLen = static_cast<DWORD>(sizeof(keyName) / sizeof(keyName[0]));
        LONG r = RegEnumKeyExW(h, idx, keyName, &keyLen, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) break;
        ++idx;
        std::wstring key(keyName, keyLen);

        HKEY hk = nullptr;
        std::wstring full = std::wstring(subkey) + L"\\" + key;
        if (RegOpenKeyExW(hive, full.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS) continue;
        UniqueRegKey hkGuard(hk);

        std::wstring displayName = ReadValueString(hk, L"DisplayName");
        if (displayName.empty()) continue;  // skip non-app entries

        JsonValue e = JsonValue::object();
        e.set("name", JsonValue::stringWide(displayName));
        e.set("version", JsonValue::stringWide(ReadValueString(hk, L"DisplayVersion")));
        e.set("publisher", JsonValue::stringWide(ReadValueString(hk, L"Publisher")));
        e.set("install_date", JsonValue::stringWide(ReadValueString(hk, L"InstallDate")));
        e.set("install_location", JsonValue::stringWide(ReadValueString(hk, L"InstallLocation")));
        e.set("uninstall_string", JsonValue::stringWide(ReadValueString(hk, L"UninstallString")));
        e.set("quiet_uninstall_string", JsonValue::stringWide(ReadValueString(hk, L"QuietUninstallString")));
        e.set("hive", JsonValue::stringWide(std::wstring(hiveLabel)));
        e.set("key", JsonValue::stringWide(key));
        out.push_back(std::move(e));
    }
}

}  // namespace

JsonValue CollectSoftwareList() {
    std::vector<JsonValue> all;
    const wchar_t* kUninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
    const wchar_t* kUninstallWow = L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    EnumUninstall(HKEY_LOCAL_MACHINE, kUninstall, L"HKLM", all);
    EnumUninstall(HKEY_LOCAL_MACHINE, kUninstallWow, L"HKLM (Wow6432Node)", all);
    EnumUninstall(HKEY_CURRENT_USER, kUninstall, L"HKCU", all);

    std::sort(all.begin(), all.end(), [](const JsonValue& a, const JsonValue& b) {
        const JsonValue* na = a.find("name");
        const JsonValue* nb = b.find("name");
        std::string sa = na && na->isString() ? na->asString() : "";
        std::string sb = nb && nb->isString() ? nb->asString() : "";
        if (sa != sb) return sa < sb;
        const JsonValue* va = a.find("version");
        const JsonValue* vb = b.find("version");
        std::string sva = va && va->isString() ? va->asString() : "";
        std::string svb = vb && vb->isString() ? vb->asString() : "";
        return sva < svb;
    });

    JsonValue list = JsonValue::array();
    for (auto& e : all) list.push(std::move(e));
    return list;
}

}  // namespace cm
