// proc.cpp — proc.list: Toolhelp snapshot + Win32 process APIs + WMI cmdline.
#include "proc.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <lmcons.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "util.hpp"
#include "wmi.hpp"

namespace cm {

namespace {

struct WmiProc {
    std::wstring exePath;
    std::wstring cmdline;
    std::wstring creationDate;
};

std::unordered_map<DWORD, WmiProc> LoadWmiProcesses() {
    std::unordered_map<DWORD, WmiProc> map;
    WmiClient wmi;
    if (!wmi.Connect()) return map;
    auto rows = wmi.Query(
        L"SELECT ProcessId, ExecutablePath, CommandLine, CreationDate FROM Win32_Process",
        {L"ProcessId", L"ExecutablePath", L"CommandLine", L"CreationDate"});
    for (const auto& r : rows) {
        auto it = r.find(L"ProcessId");
        if (it == r.end() || it->second.empty()) continue;
        DWORD pid = static_cast<DWORD>(std::stoul(it->second));
        WmiProc p;
        auto get = [&](const wchar_t* k) -> std::wstring {
            auto f = r.find(k);
            return f != r.end() ? f->second : L"";
        };
        p.exePath = get(L"ExecutablePath");
        p.cmdline = get(L"CommandLine");
        p.creationDate = get(L"CreationDate");
        map[pid] = std::move(p);
    }
    return map;
}

std::wstring GetProcessUser(HANDLE hProc) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) return L"";
    UniqueHandle tokenGuard(hToken);

    DWORD retLen = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &retLen);
    if (retLen == 0) return L"";
    std::vector<BYTE> buf(retLen);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), retLen, &retLen)) return L"";
    auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
    PSID sid = tu->User.Sid;

    DWORD nameLen = 0, domLen = 0;
    SID_NAME_USE use = SidTypeUnknown;
    LookupAccountSidW(nullptr, sid, nullptr, &nameLen, nullptr, &domLen, &use);
    if (nameLen == 0) return L"";
    std::vector<wchar_t> name(nameLen), dom(domLen + 1);
    if (!LookupAccountSidW(nullptr, sid, name.data(), &nameLen, dom.data(), &domLen, &use)) {
        return L"";
    }
    std::wstring d(dom.data(), domLen);
    std::wstring n(name.data(), nameLen);
    if (d.empty()) return n;
    return d + L"\\" + n;
}

std::wstring GetImagePath(HANDLE hProc) {
    DWORD size = 32768;
    std::vector<wchar_t> buf(size);
    if (!QueryFullProcessImageNameW(hProc, 0, buf.data(), &size)) return L"";
    return std::wstring(buf.data(), size);
}

std::wstring GetStartTime(HANDLE hProc) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(hProc, &creation, &exit, &kernel, &user)) return L"";
    return FormatFileTimeUtc(creation);
}

// Open with LIMITED first (works for image path + times on modern Windows),
// fall back to QUERY_INFORMATION for older/odd cases.
HANDLE OpenProcessForQuery(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) return h;
    return OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
}

}  // namespace

JsonValue CollectProcList() {
    JsonValue list = JsonValue::array();

    std::unordered_map<DWORD, WmiProc> wmiMap = LoadWmiProcesses();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(GetLastError())));
        list.push(std::move(err));
        return list;
    }
    UniqueHandle snapGuard(snap);

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    std::vector<PROCESSENTRY32W> entries;
    if (Process32FirstW(snap, &pe)) {
        do {
            entries.push_back(pe);
        } while (Process32NextW(snap, &pe));
    }

    // Deterministic order: by pid.
    std::sort(entries.begin(), entries.end(),
              [](const PROCESSENTRY32W& a, const PROCESSENTRY32W& b) {
                  return a.th32ProcessID < b.th32ProcessID;
              });

    for (const PROCESSENTRY32W& e : entries) {
        DWORD pid = e.th32ProcessID;
        DWORD ppid = e.th32ParentProcessID;

        std::wstring imagePath, user, startTime;
        HANDLE hProc = OpenProcessForQuery(pid);
        if (hProc) {
            UniqueHandle procGuard(hProc);
            imagePath = GetImagePath(hProc);
            user = GetProcessUser(hProc);
            startTime = GetStartTime(hProc);
        }

        std::wstring cmdline, wmiExe, wmiStart;
        auto it = wmiMap.find(pid);
        if (it != wmiMap.end()) {
            cmdline = it->second.cmdline;
            wmiExe = it->second.exePath;
            if (!it->second.creationDate.empty()) {
                wmiStart = FormatWmiDateTime(it->second.creationDate);
            }
        }
        if (imagePath.empty()) imagePath = wmiExe;
        if (startTime.empty()) startTime = wmiStart;

        JsonValue p = JsonValue::object();
        p.set("pid", JsonValue::unsignedInteger(pid));
        p.set("ppid", JsonValue::unsignedInteger(ppid));
        p.set("name", JsonValue::stringWide(std::wstring(e.szExeFile)));
        p.set("image_path", JsonValue::stringWide(imagePath));
        p.set("user", JsonValue::stringWide(user));
        p.set("cmdline", JsonValue::stringWide(cmdline));
        p.set("start_time_utc", JsonValue::stringWide(startTime));
        list.push(std::move(p));
    }
    return list;
}

}  // namespace cm
