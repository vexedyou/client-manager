// sysinfo.cpp — sys.info: OS, CPU, RAM, disks, NICs, uptime, user, host, domain.
// Real Win32 + IP Helper + WMI (Win32_Processor, Win32_LoggedOnUser).
#include "sysinfo.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <lmcons.h>
#define SECURITY_WIN32
#include <security.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "util.hpp"
#include "wmi.hpp"

namespace cm {

namespace {

// ---- registry helpers ----------------------------------------------------

std::wstring ReadRegString(HKEY hive, const wchar_t* subkey, const wchar_t* value) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return L"";
    UniqueRegKey guard(h);
    DWORD type = 0;
    DWORD size = 0;
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

DWORD ReadRegDword(HKEY hive, const wchar_t* subkey, const wchar_t* value, DWORD fallback = 0) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return fallback;
    UniqueRegKey guard(h);
    DWORD v = 0, type = 0, size = sizeof(v);
    if (RegQueryValueExW(h, value, nullptr, &type,
                         reinterpret_cast<LPBYTE>(&v), &size) != ERROR_SUCCESS) return fallback;
    if (type != REG_DWORD) return fallback;
    return v;
}

// ---- OS version via RtlGetVersion (accurate on Win11, unlike GetVersionEx) -

struct RtlOsVersionInfoW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};

bool GetRtlVersion(ULONG& major, ULONG& minor, ULONG& build, ULONG& platform) {
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (!h) h = LoadLibraryW(L"ntdll.dll");
    if (!h) return false;
    using RtlGetVersionFn = LONG(__stdcall*)(RtlOsVersionInfoW*);
    auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(h, "RtlGetVersion")));
    if (!fn) return false;
    RtlOsVersionInfoW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn(&vi) != 0) return false;
    major = vi.dwMajorVersion;
    minor = vi.dwMinorVersion;
    build = vi.dwBuildNumber;
    platform = vi.dwPlatformId;
    return true;
}

JsonValue CollectOs() {
    JsonValue os = JsonValue::object();
    ULONG major = 0, minor = 0, build = 0, platform = 0;
    bool haveRtl = GetRtlVersion(major, minor, build, platform);

    const wchar_t* kCurVer = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::wstring productName = ReadRegString(HKEY_LOCAL_MACHINE, kCurVer, L"ProductName");
    std::wstring displayVersion = ReadRegString(HKEY_LOCAL_MACHINE, kCurVer, L"DisplayVersion");
    std::wstring editionId = ReadRegString(HKEY_LOCAL_MACHINE, kCurVer, L"EditionID");
    std::wstring buildLabEx = ReadRegString(HKEY_LOCAL_MACHINE, kCurVer, L"BuildLabEx");
    std::wstring releaseId = ReadRegString(HKEY_LOCAL_MACHINE, kCurVer, L"ReleaseId");
    std::wstring currentBuild = ReadRegString(HKEY_LOCAL_MACHINE, kCurVer, L"CurrentBuildNumber");
    DWORD ubr = ReadRegDword(HKEY_LOCAL_MACHINE, kCurVer, L"UBR", 0);
    DWORD installDate = ReadRegDword(HKEY_LOCAL_MACHINE, kCurVer, L"InstallDate", 0);

    if (!haveRtl && !currentBuild.empty()) {
        try {
            build = static_cast<ULONG>(std::stoul(currentBuild));
            haveRtl = true;
        } catch (...) {
            // Non-numeric registry value; leave build as-is.
        }
    }

    os.set("product_name", JsonValue::stringWide(productName));
    os.set("display_version", JsonValue::stringWide(displayVersion));
    os.set("edition_id", JsonValue::stringWide(editionId));
    os.set("release_id", JsonValue::stringWide(releaseId));
    os.set("build_lab_ex", JsonValue::stringWide(buildLabEx));
    os.set("major", JsonValue::unsignedInteger(major));
    os.set("minor", JsonValue::unsignedInteger(minor));
    os.set("build", JsonValue::unsignedInteger(build));
    os.set("platform_id", JsonValue::unsignedInteger(platform));
    os.set("current_build_number", JsonValue::stringWide(currentBuild));
    os.set("ubr", JsonValue::unsignedInteger(ubr));
    // Full build string, e.g. "22631.4169".
    {
        wchar_t buf[64];
        swprintf_s(buf, L"%lu.%lu", static_cast<unsigned long>(build),
                   static_cast<unsigned long>(ubr));
        os.set("build_full", JsonValue::stringWide(std::wstring(buf)));
    }
    os.set("install_date_unix", JsonValue::unsignedInteger(installDate));
    os.set("architecture", JsonValue::stringWide([] {
        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        switch (si.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: return L"x64";
            case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64";
            case PROCESSOR_ARCHITECTURE_INTEL: return L"x86";
            default: return L"unknown";
        }
    }()));
    return os;
}

// ---- CPU -----------------------------------------------------------------

JsonValue CollectCpu() {
    JsonValue cpu = JsonValue::object();

    // WMI Win32_Processor (model, cores, logical processors, clock).
    WmiClient wmi;
    std::wstring model, manufacturer;
    std::wstring cores, logical, enabledCores, clock, arch;
    if (wmi.Connect()) {
        auto rows = wmi.Query(
            L"SELECT Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors, "
            L"NumberOfEnabledCore, MaxClockSpeed, Architecture FROM Win32_Processor",
            {L"Name", L"Manufacturer", L"NumberOfCores", L"NumberOfLogicalProcessors",
             L"NumberOfEnabledCore", L"MaxClockSpeed", L"Architecture"});
        if (!rows.empty()) {
            model = rows[0][L"Name"];
            manufacturer = rows[0][L"Manufacturer"];
            cores = rows[0][L"NumberOfCores"];
            logical = rows[0][L"NumberOfLogicalProcessors"];
            enabledCores = rows[0][L"NumberOfEnabledCore"];
            clock = rows[0][L"MaxClockSpeed"];
            arch = rows[0][L"Architecture"];
        }
    }

    // Registry fallback for the model string.
    if (model.empty()) {
        model = ReadRegString(
            HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            L"ProcessorNameString");
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    cpu.set("model", JsonValue::stringWide(model));
    cpu.set("manufacturer", JsonValue::stringWide(manufacturer));
    cpu.set("cores", JsonValue::stringWide(cores));
    cpu.set("logical_processors", JsonValue::stringWide(logical));
    cpu.set("enabled_cores", JsonValue::stringWide(enabledCores));
    cpu.set("max_clock_mhz", JsonValue::stringWide(clock));
    cpu.set("architecture", JsonValue::stringWide(arch));
    cpu.set("system_logical_processors", JsonValue::unsignedInteger(si.dwNumberOfProcessors));
    cpu.set("page_size", JsonValue::unsignedInteger(si.dwPageSize));
    return cpu;
}

// ---- memory --------------------------------------------------------------

JsonValue CollectMemory() {
    JsonValue mem = JsonValue::object();
    MEMORYSTATUSEX st{};
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) {
        mem.set("total_bytes", JsonValue::unsignedInteger(st.ullTotalPhys));
        mem.set("available_bytes", JsonValue::unsignedInteger(st.ullAvailPhys));
        mem.set("total_virtual_bytes", JsonValue::unsignedInteger(st.ullTotalVirtual));
        mem.set("available_virtual_bytes", JsonValue::unsignedInteger(st.ullAvailVirtual));
        mem.set("total_page_file_bytes", JsonValue::unsignedInteger(st.ullTotalPageFile));
        mem.set("available_page_file_bytes", JsonValue::unsignedInteger(st.ullAvailPageFile));
        mem.set("load_percent", JsonValue::unsignedInteger(st.dwMemoryLoad));
    } else {
        mem.set("error", JsonValue::stringWide(WinErrorString(GetLastError())));
    }
    return mem;
}

// ---- disks ---------------------------------------------------------------

const wchar_t* DriveTypeName(UINT t) {
    switch (t) {
        case DRIVE_UNKNOWN: return L"UNKNOWN";
        case DRIVE_NO_ROOT_DIR: return L"NO_ROOT_DIR";
        case DRIVE_REMOVABLE: return L"REMOVABLE";
        case DRIVE_FIXED: return L"FIXED";
        case DRIVE_REMOTE: return L"REMOTE";
        case DRIVE_CDROM: return L"CDROM";
        case DRIVE_RAMDISK: return L"RAMDISK";
        default: return L"UNKNOWN";
    }
}

JsonValue CollectDisks() {
    JsonValue disks = JsonValue::array();
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i))) continue;
        wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        UINT type = GetDriveTypeW(root);
        if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR) continue;

        ULARGE_INTEGER avail{}, total{}, free{};
        bool haveSpace = GetDiskFreeSpaceExW(root, &avail, &total, &free) != 0;
        DWORD spaceError = haveSpace ? ERROR_SUCCESS : GetLastError();

        wchar_t volName[MAX_PATH] = {};
        wchar_t fsName[MAX_PATH] = {};
        DWORD flags = 0;
        GetVolumeInformationW(root, volName, MAX_PATH, nullptr, nullptr, &flags, fsName, MAX_PATH);

        JsonValue d = JsonValue::object();
        wchar_t letter[3] = {static_cast<wchar_t>(L'A' + i), L':', L'\0'};
        d.set("drive", JsonValue::stringWide(std::wstring(letter)));
        d.set("root", JsonValue::stringWide(std::wstring(root)));
        d.set("type", JsonValue::stringWide(std::wstring(DriveTypeName(type))));
        d.set("volume_label", JsonValue::stringWide(std::wstring(volName)));
        d.set("filesystem", JsonValue::stringWide(std::wstring(fsName)));
        d.set("volume_flags", JsonValue::unsignedInteger(flags));
        if (haveSpace) {
            d.set("total_bytes", JsonValue::unsignedInteger(total.QuadPart));
            d.set("free_bytes", JsonValue::unsignedInteger(free.QuadPart));
            d.set("available_bytes", JsonValue::unsignedInteger(avail.QuadPart));
            d.set("used_bytes", JsonValue::unsignedInteger(total.QuadPart - free.QuadPart));
        } else {
            d.set("error", JsonValue::stringWide(WinErrorString(spaceError)));
        }
        disks.push(std::move(d));
    }
    return disks;
}

// ---- NICs (IP Helper GetAdaptersAddresses) --------------------------------

std::wstring FormatMac(const BYTE* mac, ULONG len) {
    wchar_t buf[64];
    size_t pos = 0;
    for (ULONG i = 0; i < len && pos + 4 < sizeof(buf) / sizeof(buf[0]); ++i) {
        if (i) buf[pos++] = L'-';
        swprintf_s(buf + pos, sizeof(buf) / sizeof(buf[0]) - pos, L"%02X", mac[i]);
        pos += 2;
    }
    buf[pos] = L'\0';
    return std::wstring(buf);
}

std::wstring FormatSockAddr(const SOCKET_ADDRESS& sa) {
    wchar_t buf[256] = {};
    DWORD len = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    DWORD saLen = 0;
    if (sa.lpSockaddr->sa_family == AF_INET) saLen = sizeof(sockaddr_in);
    else if (sa.lpSockaddr->sa_family == AF_INET6) saLen = sizeof(sockaddr_in6);
    else return L"";
    if (WSAAddressToStringW(sa.lpSockaddr, saLen, nullptr, buf, &len) != 0) return L"";
    return std::wstring(buf);
}

JsonValue CollectNics() {
    JsonValue nics = JsonValue::array();

    ULONG size = 0;
    ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS;
    DWORD ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size);
    if (ret != ERROR_BUFFER_OVERFLOW && ret != ERROR_SUCCESS) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(ret)));
        nics.push(std::move(err));
        return nics;
    }
    std::vector<BYTE> buf(size);
    auto* adapters = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
    if (ret != ERROR_SUCCESS) {
        JsonValue err = JsonValue::object();
        err.set("error", JsonValue::stringWide(WinErrorString(ret)));
        nics.push(std::move(err));
        return nics;
    }

    for (auto* a = adapters; a; a = a->Next) {
        JsonValue nic = JsonValue::object();
        // AdapterName is ANSI (char*); adapter names are ASCII, so a UTF-8
        // decode is exact here.
        nic.set("name", JsonValue::stringWide(
                            Utf8ToWide(a->AdapterName ? std::string(a->AdapterName) : std::string())));
        nic.set("description", JsonValue::stringWide(a->Description ? std::wstring(a->Description) : L""));
        nic.set("if_index", JsonValue::unsignedInteger(a->IfIndex));
        nic.set("mtu", JsonValue::unsignedInteger(a->Mtu));
        nic.set("oper_status", JsonValue::unsignedInteger(a->OperStatus));
        nic.set("if_type", JsonValue::unsignedInteger(a->IfType));
        nic.set("mac", JsonValue::stringWide(FormatMac(a->PhysicalAddress, a->PhysicalAddressLength)));

        JsonValue v4 = JsonValue::array();
        JsonValue v6 = JsonValue::array();
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            std::wstring addr = FormatSockAddr(u->Address);
            if (addr.empty()) continue;
            if (u->Address.lpSockaddr->sa_family == AF_INET) v4.push(JsonValue::stringWide(addr));
            else if (u->Address.lpSockaddr->sa_family == AF_INET6) v6.push(JsonValue::stringWide(addr));
        }
        nic.set("ipv4", std::move(v4));
        nic.set("ipv6", std::move(v6));

        JsonValue dns = JsonValue::array();
        for (auto* d = a->FirstDnsServerAddress; d; d = d->Next) {
            std::wstring addr = FormatSockAddr(d->Address);
            if (!addr.empty()) dns.push(JsonValue::stringWide(addr));
        }
        nic.set("dns_servers", std::move(dns));

        JsonValue gw = JsonValue::array();
        for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
            std::wstring addr = FormatSockAddr(g->Address);
            if (!addr.empty()) gw.push(JsonValue::stringWide(addr));
        }
        nic.set("gateways", std::move(gw));

        nics.push(std::move(nic));
    }
    return nics;
}

// ---- user / host / domain -------------------------------------------------

std::wstring GetComputerName(COMPUTER_NAME_FORMAT fmt) {
    DWORD size = 0;
    GetComputerNameExW(fmt, nullptr, &size);
    if (size == 0) return L"";
    std::vector<wchar_t> buf(size);
    if (!GetComputerNameExW(fmt, buf.data(), &size)) return L"";
    return std::wstring(buf.data());
}

std::wstring GetCurrentUser() {
    // Prefer DOMAIN\user via GetUserNameExW(NameSamCompatible).
    DWORD size = 0;
    GetUserNameExW(NameSamCompatible, nullptr, &size);
    if (size > 0) {
        std::vector<wchar_t> buf(size);
        if (GetUserNameExW(NameSamCompatible, buf.data(), &size)) {
            return std::wstring(buf.data());
        }
    }
    size = 0;
    GetUserNameW(nullptr, &size);
    if (size > 0) {
        std::vector<wchar_t> buf(size);
        if (GetUserNameW(buf.data(), &size)) return std::wstring(buf.data());
    }
    return L"";
}

JsonValue CollectLoggedOnUsers() {
    JsonValue users = JsonValue::array();
    WmiClient wmi;
    if (!wmi.Connect()) return users;
    auto rows = wmi.Query(L"SELECT Antecedent FROM Win32_LoggedOnUser", {L"Antecedent"});
    for (const auto& r : rows) {
        auto it = r.find(L"Antecedent");
        if (it != r.end() && !it->second.empty()) {
            users.push(JsonValue::stringWide(it->second));
        }
    }
    return users;
}

}  // namespace

JsonValue CollectSysInfo() {
    JsonValue info = JsonValue::object();

    info.set("os", CollectOs());
    info.set("cpu", CollectCpu());
    info.set("memory", CollectMemory());
    info.set("disks", CollectDisks());
    info.set("nics", CollectNics());

    // Uptime + boot time.
    ULONGLONG uptimeMs = GetTickCount64();
    info.set("uptime_seconds", JsonValue::unsignedInteger(uptimeMs / 1000));
    {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER boot{};
        boot.LowPart = now.dwLowDateTime;
        boot.HighPart = now.dwHighDateTime;
        boot.QuadPart -= uptimeMs * 10000ULL;
        FILETIME bootFt{boot.LowPart, boot.HighPart};
        info.set("boot_time_utc", JsonValue::stringWide(FormatFileTimeUtc(bootFt)));
    }

    info.set("user", JsonValue::stringWide(GetCurrentUser()));
    info.set("logged_on_users", CollectLoggedOnUsers());
    info.set("hostname", JsonValue::stringWide(GetComputerName(ComputerNameNetBIOS)));
    info.set("hostname_dns", JsonValue::stringWide(GetComputerName(ComputerNameDnsHostname)));
    info.set("domain", JsonValue::stringWide(GetComputerName(ComputerNameDnsDomain)));
    info.set("domain_machine", JsonValue::stringWide(GetComputerName(ComputerNamePhysicalDnsDomain)));

    return info;
}

}  // namespace cm
