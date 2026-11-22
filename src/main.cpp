// main.cpp — client-manager: Windows system inventory tool (MSVC, Win11).
//
// Usage: client-manager.exe <config.json> <report.json>
//
// Reads the config path from argv[1], writes one JSON report object to
// argv[2] with keys: sys.info, proc.list, net.conn, svc.list,
// persist.check, software.list, fs.list.
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "config.hpp"
#include "fs.hpp"
#include "json.hpp"
#include "net.hpp"
#include "persist.hpp"
#include "proc.hpp"
#include "software.hpp"
#include "svc.hpp"
#include "sysinfo.hpp"
#include "util.hpp"

using namespace cm;

namespace {

void PrintUsage(const wchar_t* prog) {
    fwprintf(stderr, L"Usage: %s <config.json> <report.json>\n", prog);
    fwprintf(stderr, L"  Reads inventory config from argv[1], writes JSON report to argv[2].\n");
}

size_t CountOf(const JsonValue& v) {
    return v.isArray() ? v.asArray().size() : 0;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        PrintUsage(argc > 0 ? argv[0] : L"client-manager.exe");
        return 2;
    }
    std::wstring configPath = argv[1];
    std::wstring outputPath = argv[2];

    // --- read + parse config ------------------------------------------------
    std::string configBytes;
    if (!ReadFileBytes(configPath, configBytes)) {
        fwprintf(stderr, L"error: cannot read config file: %s (%s)\n",
                 configPath.c_str(), WinErrorString(GetLastError()).c_str());
        return 1;
    }
    // Strip UTF-8 BOM if present.
    if (configBytes.size() >= 3 &&
        static_cast<unsigned char>(configBytes[0]) == 0xEF &&
        static_cast<unsigned char>(configBytes[1]) == 0xBB &&
        static_cast<unsigned char>(configBytes[2]) == 0xBF) {
        configBytes.erase(0, 3);
    }

    Config cfg;
    try {
        JsonValue parsed = ParseJson(configBytes);
        cfg = ParseConfig(parsed);
    } catch (const std::exception& e) {
        fwprintf(stderr, L"error: %hs\n", e.what());
        return 1;
    }

    // --- COM (WMI + Task Scheduler) -----------------------------------------
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool comOk = SUCCEEDED(hr);
    if (!comOk) {
        fwprintf(stderr, L"warning: CoInitializeEx failed (0x%08lX); "
                         L"WMI/task collectors may be empty\n",
                 static_cast<unsigned long>(hr));
    }

    WSADATA winsockData{};
    const bool winsockOk = WSAStartup(MAKEWORD(2, 2), &winsockData) == 0;

    // --- collect (order: sys.info, proc.list, persist.check, ...) -----------
    JsonValue root = JsonValue::object();
    root.set("generated_at_utc", JsonValue::stringWide(NowUtcIso()));

    JsonValue svcList = JsonValue::array();  // persist.check needs svc.list
    try {
        if (cfg.includeSysInfo) root.set("sys.info", CollectSysInfo());
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("sys.info", std::move(err));
    }
    try {
        if (cfg.includeProcList) root.set("proc.list", CollectProcList());
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("proc.list", std::move(err));
    }
    try {
        if (cfg.includeNetConn) root.set("net.conn", CollectNetConn());
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("net.conn", std::move(err));
    }
    try {
        if (cfg.includeSvcList || cfg.includePersistCheck) {
            svcList = CollectSvcList();
            if (cfg.includeSvcList) root.set("svc.list", svcList);
        }
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("svc.list", std::move(err));
    }
    try {
        if (cfg.includePersistCheck) root.set("persist.check", CollectPersistCheck(svcList));
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("persist.check", std::move(err));
    }
    try {
        if (cfg.includeSoftwareList) root.set("software.list", CollectSoftwareList());
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("software.list", std::move(err));
    }
    try {
        if (cfg.includeFsList) root.set("fs.list", CollectFsList(cfg.fsPaths, cfg.fsRecursive));
    } catch (const std::exception& e) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringUtf8(e.what()));
        root.set("fs.list", std::move(err));
    }

    if (winsockOk) WSACleanup();
    if (comOk) CoUninitialize();

    // --- write report ---------------------------------------------------------
    std::string report = root.dump(2);
    if (!WriteFileBytes(outputPath, report)) {
        fwprintf(stderr, L"error: cannot write report file: %s (%s)\n",
                 outputPath.c_str(), WinErrorString(GetLastError()).c_str());
        return 1;
    }

    // --- summary --------------------------------------------------------------
    auto reportCount = [&](const char* key) -> size_t {
        const JsonValue* v = root.find(key);
        return v ? CountOf(*v) : 0;
    };
    wprintf(L"Wrote report to %s\n", outputPath.c_str());
    wprintf(L"  proc.list: %zu processes\n", reportCount("proc.list"));
    wprintf(L"  net.conn: %zu connections\n", reportCount("net.conn"));
    wprintf(L"  svc.list: %zu services\n", reportCount("svc.list"));
    wprintf(L"  software.list: %zu entries\n", reportCount("software.list"));
    wprintf(L"  fs.list: %zu path(s)\n", reportCount("fs.list"));
    return 0;
}
