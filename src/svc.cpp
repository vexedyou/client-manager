// svc.cpp — svc.list via Service Control Manager (EnumServicesStatusExW +
// QueryServiceConfigW). Delayed-auto is detected via the DelayedAutoStart
// registry value (QueryServiceConfig reports SERVICE_AUTO_START for both).
#include "svc.hpp"

#include <windows.h>
#include <winsvc.h>

#include <algorithm>
#include <vector>

#include "util.hpp"

namespace cm {

namespace {

const wchar_t* ServiceStateName(DWORD s) {
    switch (s) {
        case SERVICE_STOPPED: return L"STOPPED";
        case SERVICE_START_PENDING: return L"START_PENDING";
        case SERVICE_STOP_PENDING: return L"STOP_PENDING";
        case SERVICE_RUNNING: return L"RUNNING";
        case SERVICE_CONTINUE_PENDING: return L"CONTINUE_PENDING";
        case SERVICE_PAUSE_PENDING: return L"PAUSE_PENDING";
        case SERVICE_PAUSED: return L"PAUSED";
        default: return L"UNKNOWN";
    }
}

const wchar_t* StartTypeName(DWORD t, bool delayed) {
    if (delayed && t == SERVICE_AUTO_START) return L"AUTO_DELAYED";
    switch (t) {
        case SERVICE_BOOT_START: return L"BOOT";
        case SERVICE_SYSTEM_START: return L"SYSTEM";
        case SERVICE_AUTO_START: return L"AUTO";
        case SERVICE_DEMAND_START: return L"MANUAL";
        case SERVICE_DISABLED: return L"DISABLED";
        default: return L"UNKNOWN";
    }
}

bool IsDelayedAutoStart(const std::wstring& name) {
    std::wstring subkey = L"SYSTEM\\CurrentControlSet\\Services\\" + name;
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) {
        return false;
    }
    UniqueRegKey guard(h);
    DWORD v = 0, type = 0, size = sizeof(v);
    if (RegQueryValueExW(h, L"DelayedAutoStart", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&v), &size) != ERROR_SUCCESS) {
        return false;
    }
    return type == REG_DWORD && v != 0;
}

}  // namespace

JsonValue CollectSvcList() {
    JsonValue list = JsonValue::array();

    SC_HANDLE hSC = OpenSCManagerW(nullptr, nullptr,
                                   SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSC) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(GetLastError())));
        list.push(std::move(err));
        return list;
    }
    UniqueServiceHandle scGuard(hSC);

    DWORD bytesNeeded = 0, numServices = 0, resume = 0;
    EnumServicesStatusExW(hSC, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ALL,
                          nullptr, 0, &bytesNeeded, &numServices, &resume, nullptr);
    if (bytesNeeded == 0) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(GetLastError())));
        list.push(std::move(err));
        return list;
    }
    resume = 0;
    std::vector<BYTE> buf(bytesNeeded);
    if (!EnumServicesStatusExW(hSC, SC_ENUM_PROCESS_INFO, SERVICE_WIN32 | SERVICE_DRIVER, SERVICE_STATE_ALL,
                               buf.data(), bytesNeeded, &bytesNeeded, &numServices, &resume, nullptr)) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(GetLastError())));
        list.push(std::move(err));
        return list;
    }
    auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());

    std::vector<JsonValue> all;
    for (DWORD i = 0; i < numServices; ++i) {
        std::wstring name = services[i].lpServiceName ? services[i].lpServiceName : L"";
        std::wstring display = services[i].lpDisplayName ? services[i].lpDisplayName : L"";
        DWORD state = services[i].ServiceStatusProcess.dwCurrentState;

        std::wstring binaryPath, startName;
        DWORD startType = 0;
        bool haveConfig = false;
        SC_HANDLE hSvc = OpenServiceW(hSC, name.c_str(), SERVICE_QUERY_CONFIG);
        if (hSvc) {
            UniqueServiceHandle svcGuard(hSvc);
            DWORD cfgSize = 0;
            QueryServiceConfigW(hSvc, nullptr, 0, &cfgSize);
            if (cfgSize > 0) {
                std::vector<BYTE> cfgBuf(cfgSize);
                auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(cfgBuf.data());
                if (QueryServiceConfigW(hSvc, cfg, cfgSize, &cfgSize)) {
                    haveConfig = true;
                    startType = cfg->dwStartType;
                    if (cfg->lpBinaryPathName) binaryPath = cfg->lpBinaryPathName;
                    if (cfg->lpServiceStartName) startName = cfg->lpServiceStartName;
                    if (cfg->lpDisplayName && display.empty()) display = cfg->lpDisplayName;
                }
            }
        }

        bool delayed = IsDelayedAutoStart(name);

        JsonValue e = JsonValue::object();
        e.set("name", JsonValue::stringWide(name));
        e.set("display_name", JsonValue::stringWide(display));
        e.set("state", JsonValue::stringWide(std::wstring(ServiceStateName(state))));
        e.set("state_code", JsonValue::unsignedInteger(state));
        e.set("start_type", JsonValue::stringWide(std::wstring(StartTypeName(startType, delayed))));
        e.set("start_type_code", JsonValue::unsignedInteger(startType));
        e.set("delayed_auto_start", JsonValue::boolean(delayed));
        e.set("binary_path", JsonValue::stringWide(binaryPath));
        e.set("start_name", JsonValue::stringWide(startName));
        e.set("has_config", JsonValue::boolean(haveConfig));
        all.push_back(std::move(e));
    }

    std::sort(all.begin(), all.end(), [](const JsonValue& a, const JsonValue& b) {
        const JsonValue* na = a.find("name");
        const JsonValue* nb = b.find("name");
        std::string sa = na && na->isString() ? na->asString() : "";
        std::string sb = nb && nb->isString() ? nb->asString() : "";
        return sa < sb;
    });

    for (auto& e : all) list.push(std::move(e));
    return list;
}

}  // namespace cm
