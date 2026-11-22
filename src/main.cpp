// main.cpp — client-manager: Windows system inventory tool (MSVC, Win11).
//
// Usage: client-manager.exe --once <config.json> <report.json>
//        client-manager.exe --beacon <config.json>
// The legacy <config.json> <report.json> form remains supported.
#include <winsock2.h>
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "config.hpp"
#include "beacon.hpp"
#include "report.hpp"
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



cm::JsonValue cm::CollectReport(const Config& cfg) {
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

    // Collect a fresh snapshot.
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
    return root;

}

int wmain(int argc, wchar_t* argv[]) {
    bool beacon = argc >= 2 && std::wstring(argv[1]) == L"--beacon";
    bool once = argc >= 2 && std::wstring(argv[1]) == L"--once";
    int configIndex = (beacon || once) ? 2 : 1;
    if ((beacon && argc != 3) || (!beacon && argc != configIndex + 2)) {
        fwprintf(stderr, L"Usage: %ls --once <config.json> <report.json>\n       %ls --beacon <config.json>\n", argv[0], argv[0]);
        return 2;
    }
    std::string configBytes;
    if (!ReadFileBytes(argv[configIndex], configBytes)) {
        fwprintf(stderr, L"error: cannot read config file: %ls\n", argv[configIndex]);
        return 1;
    }
    if (configBytes.size() >= 3 && static_cast<unsigned char>(configBytes[0]) == 0xef &&
        static_cast<unsigned char>(configBytes[1]) == 0xbb && static_cast<unsigned char>(configBytes[2]) == 0xbf)
        configBytes.erase(0, 3);
    try {
        Config cfg = ParseConfig(ParseJson(configBytes));
        if (beacon) return RunBeacon(configBytes, cfg);
        JsonValue report = CollectReport(cfg);
        if (!WriteFileBytes(argv[configIndex + 1], report.dump(2))) {
            fwprintf(stderr, L"error: cannot write report file: %ls\n", argv[configIndex + 1]);
            return 1;
        }
        wprintf(L"Wrote report to %ls\n", argv[configIndex + 1]);
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
