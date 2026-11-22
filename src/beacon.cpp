#include "beacon.hpp"
#include "report.hpp"
#include "sysinfo.hpp"
#include "json.hpp"
#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace cm {
namespace {
using Json = nlohmann::json;
struct HttpHandle {
    HINTERNET value = nullptr;
    explicit HttpHandle(HINTERNET h) : value(h) {}
    ~HttpHandle() { if (value) WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};
struct Settings {
    std::wstring host, path, token;
    INTERNET_PORT port = 0;
    std::string agentId;
    unsigned interval = 300, jitter = 0;
};
Settings ParseSettings(const std::string& bytes) {
    Json j = Json::parse(bytes);
    Settings s;
    std::string url = j.at("c2_url").get<std::string>();
    std::string token = j.at("agent_token").get<std::string>();
    s.agentId = j.at("agent_id").get<std::string>();
    s.interval = j.value("interval_sec", 300u);
    s.jitter = j.value("jitter_pct", 0u);
    if (s.agentId.empty() || token.empty() || s.interval < 1 || s.interval > 86400 || s.jitter > 100)
        throw std::runtime_error("invalid agent_id, token, interval_sec or jitter_pct");
    for (unsigned char c : token) if (c <= 32 || c >= 127) throw std::runtime_error("agent_token must contain visible ASCII only");
    s.token = Utf8ToWide(token);
    std::wstring wurl = Utf8ToWide(url);
    URL_COMPONENTSW parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1); parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1); parts.dwUserNameLength = static_cast<DWORD>(-1);
    parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
        !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength || parts.dwExtraInfoLength)
        throw std::runtime_error("c2_url must be an HTTPS URL without credentials, query or fragment");
    s.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    s.port = parts.nPort;
    if (parts.dwUrlPathLength) s.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    while (!s.path.empty() && s.path.back() == L'/') s.path.pop_back();
    s.path += L"/api/agent/report";
    return s;
}
bool Post(const Settings& s, const std::string& payload) {
    if (payload.size() > std::numeric_limits<DWORD>::max()) { fprintf(stderr,"report too large\n"); return false; }
    HttpHandle session(WinHttpOpen(L"client-manager/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) { fprintf(stderr,"WinHttpOpen error %lu\n", GetLastError()); return false; }
    WinHttpSetTimeouts(session.value, 10000, 10000, 30000, 30000);
    HttpHandle conn(WinHttpConnect(session.value, s.host.c_str(), s.port, 0));
    if (!conn.value) { fprintf(stderr,"WinHttpConnect error %lu\n", GetLastError()); return false; }
    HttpHandle req(WinHttpOpenRequest(conn.value, L"POST", s.path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!req.value) { fprintf(stderr,"WinHttpOpenRequest error %lu\n", GetLastError()); return false; }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(req.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy))) {
        fprintf(stderr,"WinHTTP redirect policy error %lu\n", GetLastError()); return false;
    }
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + s.token + L"\r\n";
    DWORD length = static_cast<DWORD>(payload.size());
    if (!WinHttpSendRequest(req.value, headers.c_str(), static_cast<DWORD>(-1),
                            const_cast<char*>(payload.data()), length, length, 0) ||
        !WinHttpReceiveResponse(req.value, nullptr)) {
        fprintf(stderr,"report upload failed (WinHTTP error %lu)\n", GetLastError()); return false;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(req.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        fprintf(stderr,"cannot read HTTP status (error %lu)\n", GetLastError()); return false;
    }
    if (status < 200 || status >= 300) { fprintf(stderr,"report upload returned HTTP %lu\n", status); return false; }
    return true;
}
std::string stringField(const Json& object, const char* key) {
    auto it = object.find(key); return it != object.end() && it->is_string() ? it->get<std::string>() : "";
}
}
int RunBeacon(const std::string& configBytes, const Config& cfg) {
    Settings settings = ParseSettings(configBytes);
    std::mt19937_64 random(std::random_device{}());
    for (;;) {
        bool success = false;
        try {
            Config reportCfg = cfg;
            reportCfg.includeSysInfo = true;
            Json report = Json::parse(CollectReport(reportCfg).dump());
            Json info = report.contains("sys.info") && report["sys.info"].is_object()
                ? report["sys.info"] : Json::object();
            if (!cfg.includeSysInfo) report.erase("sys.info");
            Json os = info.value("os", Json::object());
            std::string osName = os.is_object() ? stringField(os, "product_name") : "";
            Json payload = {{"agent_id", settings.agentId}, {"hostname", stringField(info,"hostname")},
                            {"os", osName}, {"user", stringField(info,"user")}, {"report", report}};
            success = Post(settings, payload.dump());
        } catch (const std::exception& e) { fprintf(stderr,"report collection failed: %s\n",e.what()); }
        const double spread = static_cast<double>(settings.jitter) / 100.0;
        std::uniform_real_distribution<double> dist(1.0 - spread, 1.0 + spread);
        double seconds = settings.interval * (success ? 1.0 : 2.0) * dist(random);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(std::max(1.0, seconds) * 1000)));
    }
}
}
